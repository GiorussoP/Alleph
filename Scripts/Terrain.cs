using Godot;
using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Threading.Tasks;
using System.Linq;
using System.Threading;

[Tool]
public partial class Terrain : StaticBody3D
{
	private Node3D load_anchor;
	static Vector3 last_load = Vector3.Zero;
	static private bool busy = false;
	private readonly object lock_obj = new object();
	private HashSet<ChunkOctree.OctreeNode> loaded_chunks;
	private List<ChunkOctree.OctreeNode> pending_chunks, dead_chunks;

	static private readonly int 	
	
						CHUNK_RES = 10,	// The chunk resolution
						RELOAD_VALUE = 25,	// How far from last load to reload
						UPDATE_BATCH_SIZE = 32,	//How many chunks are updated on a single frame
						GENERATE_BATCH_SIZE = 4;	//How many chunks are generated at the same time 
	[Export] private int
									SIZE = 6400,
									OUTER_RADIUS = 3000,
									INNER_RADIUS = 2000,
									MOUNTAIN_SIZE = 150;
	[Export] private float
									TRANSITION_STEEPNESS = 0.2f,
									LOW_NOISE_SCALE = 0.005f,
									CONTINENT_NOISE_SCALE = 0.001f,
									HIGH_NOISE_SCALE = 0.01f;

	private Vector3 center;
	[Export] private int seed = 0;
	private FastNoiseLite high_noise,low_noise, continent_noise;

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

		continent_noise = new FastNoiseLite(); 
		continent_noise.SetSeed(seed);//(int)GD.Randi()
		continent_noise.SetNoiseType(FastNoiseLite.NoiseTypeEnum.Perlin); 
		continent_noise.SetFrequency(CONTINENT_NOISE_SCALE);

		GD.Print("Generated Noise Textures");
	}

	private float getSampleValue(Vector3 index){
		float 	dist = index.DistanceTo(center),
				continent_noise_value = continent_noise.GetNoise3Dv(index),
				low_noise_value = low_noise.GetNoise3Dv(index),
				high_noise_value = high_noise.GetNoise3Dv(index),
				outer_sphere_value = dist - OUTER_RADIUS + MOUNTAIN_SIZE * low_noise_value + 2*MOUNTAIN_SIZE * continent_noise_value,
				inner_sphere = dist - INNER_RADIUS + MOUNTAIN_SIZE * high_noise_value;

		//Blending outer sphere
		float 	f = Util.blendFunction(inner_sphere,high_noise_value,INNER_RADIUS,TRANSITION_STEEPNESS,dist);
				f = Util.blendFunction(f,outer_sphere_value,OUTER_RADIUS-MOUNTAIN_SIZE/4,TRANSITION_STEEPNESS,dist);
		return f;
	}
	

	public override void _Input(InputEvent @event){
		if(Input.IsActionJustPressed("debug_action_1")) {
			foreach(var chunk in loaded_chunks){
				int amnt = (int)(chunk.width/(SIZE/2) * 255f);
				DebugDraw3D.DrawAabbAb(chunk.position,chunk.position+new Vector3(chunk.width,chunk.width,chunk.width),Godot.Color.Color8((byte)amnt,(byte)(255-amnt),5));
			}
		}
		if(Input.IsActionJustPressed("debug_action_2")) {
			
			//loaded_chunks.Clear();
			//updateChunkBatch();
		}
	}

	// Called when the node enters the scene tree for the first time.
	public override void _Ready()
	{
		GD.Print("Preparing Terrain");
		Position = Vector3.Zero;

		loaded_chunks = new HashSet<ChunkOctree.OctreeNode>();
		pending_chunks = new List<ChunkOctree.OctreeNode>();
		dead_chunks= new List<ChunkOctree.OctreeNode>();

		GetViewport().DebugDraw = Viewport.DebugDrawEnum.Wireframe;
		if(!Engine.IsEditorHint()){
			load_anchor = GetViewport().GetCamera3D();
		}
		else{
			load_anchor = EditorInterface.Singleton.GetEditorViewport3D().GetCamera3D();
		}

		center = Position;
		last_load = load_anchor.Position;

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
					chunk.collision = found.collision;
				}
				else
					// Start generation
					// Start generation with semaphore
					chunk_tasks.Add(Task.Run(async () =>
					{
						await semaphore.WaitAsync();
						try {
							await generateChunk(tree,chunk);
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
			else if((load_anchor.Position - last_load).LengthSquared() > RELOAD_VALUE * RELOAD_VALUE){
				busy = true;
				
				last_load = load_anchor.Position;
				Vector3 pos = Position;
				Task.Run(() => reloadChunks(pos,last_load));
			}
		}
	}

	private void updateChunkBatch(){
		int n = Math.Min(dead_chunks.Count,UPDATE_BATCH_SIZE);
		GD.Print("KILLING ",n);
		foreach (var chunk in dead_chunks.Take(n)){
			if(chunk.mesh != null){
				chunk.mesh.CallThreadSafe("queue_free");
				chunk.collision.CallThreadSafe("queue_free");
			}
		}
		dead_chunks.RemoveRange(0,n);

		n = Math.Min(pending_chunks.Count,UPDATE_BATCH_SIZE);
		GD.Print("LOADING ",n);
		foreach (var chunk in pending_chunks.Take(n)){
			CallThreadSafe("add_child",chunk.mesh);
			CallThreadSafe("add_child",chunk.collision);
		}
		pending_chunks.RemoveRange(0,n);
		busy = false;
		//GD.Print(loaded_chunks.Count," LOADED");
	}

	private Task generateChunk(ChunkOctree o, ChunkOctree.OctreeNode c){
		int chunk_res = (int)c.width/CHUNK_RES;
		int quad_count = 0;

		SurfaceTool st = new SurfaceTool();
		st.Begin(Mesh.PrimitiveType.Triangles);

		var material = new StandardMaterial3D();
		material.VertexColorUseAsAlbedo = true;
		st.SetMaterial(material);

		/*
		// Getting every chunk neighbor
		List<ChunkOctree.OctreeNode> neighbors = new List<ChunkOctree.OctreeNode>();
		foreach (var dir in Util.DIRECTIONS) {
			var neighbor = o.findNeighborChunk(c, dir);
			if (neighbor != null) neighbors.Add(neighbor);
		}
		*/

		for (int x = 0; x < c.width; x+=chunk_res) {
			for (int y = 0; y < c.width; y+=chunk_res) {
				for (int z = 0; z < c.width; z+=chunk_res) {

					Vector3I pos =  (Vector3I)c.position + new Vector3I(x, y, z);
					//GD.Print("i");
					createMeshQuad(pos, ref st,chunk_res,ref quad_count);
					// Add skirts for neighboring chunks with different LODs

					/*
					foreach (var neighbor in neighbors) {

						if (neighbor.width != c.width) {
							int neighbor_res = 
							int highLODRes = Math.Max(c.width, neighbor.width);
							int lowLODRes = Math.Min(c.width, neighbor.width);

							generateSkirt(st,c, neighborChunk, dir, highLODRes, lowLODRes);
						}
					}
					*/
				}
			}
		}
		if(quad_count > 0){
			st.GenerateNormals();
			c.mesh.CallThreadSafe("set_mesh",st.Commit());
			c.collision.Shape = c.mesh.Mesh.CreateTrimeshShape();
		}

		return Task.CompletedTask;
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
		Color surface_color = getColorValue(pos,surface_gradient);

		st.SetColor(surface_color);
		//st.SetNormal(surface_gradient);
		st.AddVertex(surface_pos);
	}
	private Color getColorValue(Vector3I pos,Vector3 normal){
		float 	ln = low_noise.GetNoise3Dv(pos),
				hn = high_noise.GetNoise3Dv(pos),
				dist = (float)pos.Length(),
				dir = normal.Dot(((Vector3)pos).Normalized());


		Color basecolor;
		if(dist > OUTER_RADIUS + MOUNTAIN_SIZE)
			basecolor = Colors.DarkSlateGray.Lerp(Colors.White,Math.Max(dir/2,0)).Lerp(Colors.DarkSlateBlue,Math.Max(0,-dir/2));
		else if(dist > OUTER_RADIUS - MOUNTAIN_SIZE / 2)
			basecolor =  Colors.DarkSlateGray.Lerp(Colors.DarkGreen.Lerp(Colors.SandyBrown,ln),Math.Max(0,dir/4));
		else
			basecolor = Colors.SaddleBrown.Lerp(Colors.DarkSlateGray,dist/OUTER_RADIUS) + Colors.DarkGray*dir/4;

		return basecolor; //+ ln * Colors.Yellow - hn * Colors.Yellow;
	}

	private Vector3 getSurfaceGradient(Vector3I pos, float sample_value,int chunk_res){
		return 
			new Vector3(getSampleValue(pos + Util.AXIS[0]*chunk_res) - sample_value,
						getSampleValue(pos + Util.AXIS[1]*chunk_res) - sample_value,
						getSampleValue(pos + Util.AXIS[2]*chunk_res) - sample_value).Normalized();
	}

private void generateSkirt(SurfaceTool st, ChunkOctree.OctreeNode chunk,ChunkOctree.OctreeNode neighborChunk, Vector3I direction, int highLODRes, int lowLODRes) {
	Vector3 posA = chunk.position + (Vector3)(direction * highLODRes);
    Vector3 posB = chunk.position + (Vector3)(direction * highLODRes);
    Vector3 posALow = neighborChunk.position + (Vector3)(direction * lowLODRes);
    Vector3 posBLow = neighborChunk.position + (Vector3)(direction * lowLODRes);

   

    st.AddVertex(posA);
    st.AddVertex(posB);
    st.AddVertex(posBLow);

    st.AddVertex(posA);
    st.AddVertex(posBLow);
    st.AddVertex(posALow);
}

	private void addSkirts(SurfaceTool st, ChunkOctree octree, ChunkOctree.OctreeNode chunk) {
		// Loop through the edges of the chunk
		foreach (var direction in Util.DIRECTIONS) {
			var neighborChunk = octree.findNeighborChunk(chunk, direction); // Implement this method to find the neighboring chunk
			GD.Print("NC: ",neighborChunk);
			if (neighborChunk != null && neighborChunk.width != chunk.width) {
				int highLODRes = Math.Max(chunk.width/CHUNK_RES, neighborChunk.width/CHUNK_RES);
				int lowLODRes = Math.Min(chunk.width/CHUNK_RES, neighborChunk.width/CHUNK_RES);

				generateSkirt(st, chunk, neighborChunk,direction, highLODRes, lowLODRes);
			}
		}
	}
}
