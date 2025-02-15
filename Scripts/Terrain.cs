using Godot;
using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Threading.Tasks;
using System.Linq;
using System.Threading;


public partial class Terrain : StaticBody3D
{
	static private Camera3D camera;
	static Vector3 last_load = Vector3.Zero;
	static private bool busy = false;
	private readonly object lock_obj = new object();
	private HashSet<ChunkOctree.OctreeNode> loaded_chunks;
	private List<ChunkOctree.OctreeNode> pending_chunks, dead_chunks;

	static private readonly int 	
	
						CHUNK_RES = 8,	// The chunk resolution
						RELOAD_VALUE = 32,	// How far from last load to reload
						UPDATE_BATCH_SIZE = 32,	//How many chunks are updated on a single frame
						GENERATE_BATCH_SIZE = 4;	//How many chunks are generated at the same time 
	private readonly int
									SIZE = 4096,
									OUTER_RADIUS = 2000,
									INNER_RADIUS = 1024,
									MOUNTAIN_SIZE = 100;
	private readonly float
									TRANSITION_STEEPNESS = 0.2f,
									LOW_NOISE_SCALE = 0.005f,
									HIGH_NOISE_SCALE = 0.1f;

	private Vector3 center;
	[Export] private int seed = 0;
	private FastNoiseLite high_noise,low_noise;

	private static readonly Vector3I[][] QUAD_POINTS = new Vector3I[][] {
		//x axis
		new Vector3I[] {
			new Vector3I(0,0,-1),
			new Vector3I(0,-1,-1),
			new Vector3I(0,-1,0),
			new Vector3I(0,0,0)
		},
		
		// y axis
		new Vector3I[] {
			new Vector3I(0,0,-1),
			new Vector3I(0,0,0),
			new Vector3I(-1,0,0),
			new Vector3I(-1,0,-1)
		},
	
		// z axis
		new Vector3I[] {
			new Vector3I(0,0,0),
			new Vector3I(0,-1,0),
			new Vector3I(-1,-1,0),
			new Vector3I(-1,0,0)
		}
	};

	private static readonly Vector3I[][] EDGES = new Vector3I[][] {

		//Edges on min Z axis
		new Vector3I[]{new Vector3I(0,0,0),new Vector3I(1,0,0)},
		new Vector3I[]{new Vector3I(1,0,0),new Vector3I(1,1,0)},
		new Vector3I[]{new Vector3I(1,1,0),new Vector3I(0,1,0)},
		new Vector3I[]{new Vector3I(0,1,0),new Vector3I(0,0,0)},
		//Edges on max Z axis
		new Vector3I[]{new Vector3I(0,0,1),new Vector3I(1,0,1)},
		new Vector3I[]{new Vector3I(1,0,1),new Vector3I(1,1,1)},
		new Vector3I[]{new Vector3I(1,1,1),new Vector3I(0,1,1)},
		new Vector3I[]{new Vector3I(0,1,1),new Vector3I(0,0,1)},
		//Edges connecting min Z to max Z
		new Vector3I[]{new Vector3I(0,0,0),new Vector3I(0,0,1)},
		new Vector3I[]{new Vector3I(1,0,0),new Vector3I(1,0,1)},
		new Vector3I[]{new Vector3I(1,1,0),new Vector3I(1,1,1)},
		new Vector3I[]{new Vector3I(0,1,0),new Vector3I(0,1,1)},
	};

	private void generateNoiseTextures(int seed) { 
		low_noise = new FastNoiseLite(); 
		low_noise.SetSeed(seed);//(int)GD.Randi()
		low_noise.SetNoiseType(FastNoiseLite.NoiseTypeEnum.Perlin); 
		low_noise.SetFrequency(LOW_NOISE_SCALE);

		high_noise = new FastNoiseLite(); 
		high_noise.SetSeed(seed);//(int)GD.Randi()
		high_noise.SetNoiseType(FastNoiseLite.NoiseTypeEnum.Perlin); 
		high_noise.SetFrequency(HIGH_NOISE_SCALE);

		GD.Print("Generated Noise Textures");
	}

	private float getSampleValue(Vector3 index){
		float 	dist = index.DistanceTo(center), 
				low_noise_value = low_noise.GetNoise3Dv(index),
				high_noise_value = high_noise.GetNoise3Dv(index),
				outer_sphere_value = dist - OUTER_RADIUS + MOUNTAIN_SIZE * low_noise_value,
				inner_sphere = dist - INNER_RADIUS + MOUNTAIN_SIZE * high_noise_value;

		//Blending outer sphere
		float 	f = Util.blendFunction(inner_sphere,high_noise_value,INNER_RADIUS,TRANSITION_STEEPNESS,dist);
				f = Util.blendFunction(f,outer_sphere_value,OUTER_RADIUS-MOUNTAIN_SIZE/4,TRANSITION_STEEPNESS,dist);
		return f;
	}
	

	public override void _Input(InputEvent @event){
		if(Input.IsActionJustPressed("debug_action_1")) {
			if(!busy){
				busy = true;
				last_load = camera.Position;
				Vector3 pos = Position;
				Task.Run(() => reloadChunks(pos,last_load));
			}
		}
		if(Input.IsActionJustPressed("debug_action_2")) {
			foreach(var chunk in loaded_chunks){
				if(chunk.mesh != null)
					chunk.mesh.CreateConvexCollision();
				//dead_chunks.Add(chunk);
			}
			//loaded_chunks.Clear();
			//updateChunkBatch();
		}
	}

	// Called when the node enters the scene tree for the first time.
	public override void _Ready()
	{
		Position = Vector3.Zero;

		loaded_chunks = new HashSet<ChunkOctree.OctreeNode>();
		pending_chunks = new List<ChunkOctree.OctreeNode>();
		dead_chunks= new List<ChunkOctree.OctreeNode>();

		GetViewport().DebugDraw = Viewport.DebugDrawEnum.Wireframe;
		camera = GetViewport().GetCamera3D();


		center = Position;
		last_load = camera.Position;

		generateNoiseTextures(seed);

		// Setting layers
		CollisionLayer = (int)Util.collision_layers.Floor;
		CollisionMask = (int)Util.collision_layers.Player; //| Util.collision_layers.Raycast | Util.collision_layers.Object | Util.collision_layers.Entity);
	}


	public async Task reloadChunks(Vector3 terrain_position, Vector3 cam_pos){
		SemaphoreSlim semaphore = new SemaphoreSlim(GENERATE_BATCH_SIZE);
		ConcurrentBag<Task> chunk_tasks = new ConcurrentBag<Task>();
		lock(lock_obj){
			// Find the chunks to generate and erase
			GD.Print("Generating");
			ChunkOctree tree = new ChunkOctree((Vector3I)terrain_position- new Vector3I(SIZE,SIZE,SIZE)/2,SIZE);
			tree.Insert(cam_pos);
			var leaf_nodes = tree.getLeafNodes();
			GD.Print("FOUND LEAF NODES");
		
			
	
			// Chunks that need to be deleted
			var dead = new HashSet<ChunkOctree.OctreeNode>(loaded_chunks);
			dead.ExceptWith(leaf_nodes);
			dead_chunks = dead.ToList();

			// Chunks that need to be generated
			var pending = new HashSet<ChunkOctree.OctreeNode>(leaf_nodes);
			pending.ExceptWith(loaded_chunks);
			pending_chunks = pending.ToList();

			foreach (var chunk in leaf_nodes){
				if(loaded_chunks.Contains(chunk)){
					var found = loaded_chunks.FirstOrDefault(obj => obj.Equals(chunk));
					chunk.mesh = found.mesh;
				}
				else
					// Start generation
					// Start generation with semaphore
					chunk_tasks.Add(Task.Run(async () =>
					{
						await semaphore.WaitAsync();
						try {
							await generateChunk(chunk);
						}
						finally
						{
							semaphore.Release();
						}
					}));
					//chunk_tasks.Add(Task.Run(() =>  generateChunk(chunk)));
			}

			loaded_chunks = leaf_nodes;
		}
		GD.Print("AWAITING");
		await Task.WhenAll(chunk_tasks);
		GD.Print("FINISHED");
		busy = false;
	}

	public override void _Process(double delta) {
	
		if(!busy){
			
			if(dead_chunks.Count > 0 || pending_chunks.Count > 0){
				busy = true;
				updateChunkBatch();
			}
			else if((camera.Position - last_load).LengthSquared() > RELOAD_VALUE * RELOAD_VALUE){
				busy = true;
				
				last_load = camera.Position;
				Vector3 pos = Position;
				Task.Run(() => reloadChunks(pos,last_load));
			}
			else {
				foreach(var chunk in loaded_chunks){
					int amnt = (int)(chunk.width/(SIZE/2) * 255f);
					DebugDraw3D.DrawAabbAb(chunk.position,chunk.position+new Vector3(chunk.width,chunk.width,chunk.width),Godot.Color.Color8((byte)amnt,(byte)(255-amnt),0));
				}
			}
		}
	}

	private void updateChunkBatch(){


		int n = Math.Min(dead_chunks.Count,UPDATE_BATCH_SIZE);
		GD.Print("KILLING ",n);
		foreach (var chunk in dead_chunks.Take(n)){
			if(chunk.mesh != null){
				chunk.mesh.CallThreadSafe("queue_free");
				chunk.mesh = null;
			}
		}
		dead_chunks.RemoveRange(0,n);

		n = Math.Min(pending_chunks.Count,UPDATE_BATCH_SIZE);
		GD.Print("LOADING ",n);
		foreach (var chunk in pending_chunks.Take(n)){
			if(chunk.mesh != null){
				CallThreadSafe("add_child",chunk.mesh);
				//chunk.mesh.CreateConvexCollision();
			}
		}
		pending_chunks.RemoveRange(0,n);
		busy = false;
		//GD.Print(loaded_chunks.Count," LOADED");
	}

	private async Task generateChunk(ChunkOctree.OctreeNode c){
		int chunk_res = (int)c.width/CHUNK_RES;
		int quad_count = 0;

		SurfaceTool st = new SurfaceTool();
		st.Begin(Mesh.PrimitiveType.Triangles);

		var material = new StandardMaterial3D();
		material.VertexColorUseAsAlbedo = true;
		st.SetMaterial(material);


		for (int x = 0; x < c.width; x+=chunk_res) {
			for (int y = 0; y < c.width; y+=chunk_res) {
				for (int z = 0; z < c.width; z+=chunk_res) {

					Vector3I pos =  (Vector3I)c.position + new Vector3I(x, y, z);
					//GD.Print("i");
					createMeshQuad(pos, ref st,chunk_res,ref quad_count);
				}
			}
		}
		st.GenerateNormals();
		if(quad_count > 0){
			c.mesh = new MeshInstance3D();
			c.mesh.CallThreadSafe("set_mesh",st.Commit());
		}
	}

	private void createMeshQuad(Vector3I pos,ref SurfaceTool st,int chunk_res,ref int quad_count){
		for (uint i = 0; i < 3; ++i){
			var dir = Util.AXIS[i] * chunk_res;
			float v1 = getSampleValue(pos);
			float v2 = getSampleValue(pos+dir);
			if (v1 < 0 && v2 >=0){
				addQuad(ref pos,i,ref st,chunk_res);
				++quad_count;
			}
			else if (v1 >=0 && v2 <0) {
				addRQuad(ref pos,i,ref st,chunk_res);
				++quad_count;
			}
		}
	}
	private void addQuad(ref Vector3I pos, uint axis_index,ref SurfaceTool st,int chunk_res){
		var points = getQuadPoints(pos,axis_index,chunk_res);


		addVertex(points[0],ref st,chunk_res);
		addVertex(points[1],ref st,chunk_res);
		addVertex(points[2],ref st,chunk_res);

		addVertex(points[0],ref st,chunk_res);
		addVertex(points[2],ref st,chunk_res);
		addVertex(points[3],ref st,chunk_res);
	}
	private void addRQuad(ref Vector3I pos, uint axis_index,ref SurfaceTool st,int chunk_res){
		var points = getQuadPoints(pos,axis_index,chunk_res);

		addVertex(points[0],ref st,chunk_res);
		addVertex(points[2],ref st,chunk_res);
		addVertex(points[1],ref st,chunk_res);

		addVertex(points[0],ref st,chunk_res);
		addVertex(points[3],ref st,chunk_res);
		addVertex(points[2],ref st,chunk_res);
	}

	private Vector3I[] getQuadPoints(Vector3I pos,uint axis_index,int chunk_res){
		return new Vector3I[] {
			pos + QUAD_POINTS[axis_index][0] * chunk_res,
			pos + QUAD_POINTS[axis_index][1] * chunk_res,
			pos + QUAD_POINTS[axis_index][2] * chunk_res,
			pos + QUAD_POINTS[axis_index][3] * chunk_res
		};
	}

	private Vector3 getSurfacePosition(Vector3I pos, int chunk_res){
		var total = Vector3.Zero;
		var surface_edge_count = 0;

		foreach(var edge in EDGES){
			var pos_a = (Vector3) (pos + edge[0] * chunk_res);
			var sample_a = getSampleValue(pos_a);
			var pos_b = (Vector3) (pos + edge[1] * chunk_res);
			var sample_b = getSampleValue(pos_b);

			if(sample_a * sample_b <= 0){
				++surface_edge_count;
				total += pos_a.Lerp(pos_b,Math.Abs(sample_a)/(Math.Abs(sample_a) + Math.Abs(sample_b)));
			}
		}

		if(surface_edge_count == 0)
			return ((Vector3)pos) + Vector3.One * 0.5f * chunk_res;

		return total / surface_edge_count;
	}

	private void addVertex(Vector3I pos,ref SurfaceTool st,int chunk_res){
		var sample_value = getSampleValue(pos);
		var surface_pos = getSurfacePosition(pos, chunk_res);
		var surface_gradient = getSurfaceGradient(pos, sample_value,chunk_res);
		Color surface_color = getColorValue(pos);

		st.SetColor(surface_color);
		//st.SetNormal(surface_gradient);
		st.AddVertex(surface_pos);
	}
	private Color getColorValue(Vector3I pos){
		return Colors.DarkSlateGray.Lerp(Colors.Salmon,(float)pos.Length()/(SIZE/2));
	}

	private Vector3 getSurfaceGradient(Vector3I pos, float sample_value,int chunk_res){
		return 
			new Vector3(getSampleValue(pos + Util.AXIS[0]*chunk_res) - sample_value,
						getSampleValue(pos + Util.AXIS[1]*chunk_res) - sample_value,
						getSampleValue(pos + Util.AXIS[2]*chunk_res) - sample_value).Normalized();
	}
}
