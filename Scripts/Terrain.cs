using Godot;
using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Threading.Tasks;
using System.Linq;
using System.Threading;

public partial class Terrain : StaticBody3D
{
	

	private const int 	SIZE = 5096, 
						CHUNK_SIZE = 32, // In units
						LOAD_DIST = 10, //Render distance, in chunks
						UNLOAD_BATCH_SIZE =  128;	//How many dead chunks are killed on a single frame
	private readonly float 			OUTER_RADIUS = 2048,
									INNER_RADIUS = 1024,
									MOUNTAIN_SIZE = 200,
									TRANSITION_STEEPNESS = 0.5f,
									LOW_NOISE_SCALE = 0.005f,
									HIGH_NOISE_SCALE = 0.01f;

	private Camera3D camera;
	[Export] private int seed = 0;
	private FastNoiseLite high_noise,low_noise;
	private Vector3 center, last_load = Vector3.Zero;

	private Vector3I last_load_chunk = Vector3I.Zero;
	private static int RELOAD_VALUE;
	private bool generating = false;

	//List<Chunk> dead_chunks = new List<Chunk>();
	private Octree<Chunk> loaded_chunks;
	private List<Chunk> dead_chunks;
	//private Octree<Chunk> octree = 	new Octree<Chunk>(new Aabb(-SIZE/2,-SIZE/2,-SIZE/2,new Vector3(SIZE,SIZE,SIZE)));

	private enum LODlevel {
		medium = 4,
		low = 16,
		ultra_low = 32,

		lowest = CHUNK_SIZE
	}
	private struct Chunk {
		public Vector3I id;
		public LODlevel lod;
		public MeshInstance3D mesh;
		public CollisionShape3D collision;

		public Chunk(Vector3I chunk_id, LODlevel chunk_lod){
			id = chunk_id;
			lod = chunk_lod;
			mesh = new MeshInstance3D();
			collision = new CollisionShape3D();
		}
	}
	private static readonly Vector3I[] AXIS = {
		Vector3I.Right,
		Vector3I.Up,
		Vector3I.Back
	};
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

	// Reloads a chunk that is inserted in the octree
	private void updateChunk(ref Chunk c){
		// Generating chunk
		int chunk_res = (int)c.lod, quad_count = 0;
		SurfaceTool st = new SurfaceTool();

		st.Begin(Mesh.PrimitiveType.Triangles);

		var material = new StandardMaterial3D();
		material.VertexColorUseAsAlbedo = true;
		st.SetMaterial(material);


		int SKIT_STOP = CHUNK_SIZE - chunk_res;

		int[] n_lods = {(int)calculateLOD(c.id + Vector3I.Right),(int)calculateLOD(c.id + Vector3I.Up),(int)calculateLOD(c.id + Vector3I.Back)};

		for (int x = 0; x < CHUNK_SIZE; x+=chunk_res) {
			for (int y = 0; y < CHUNK_SIZE; y+=chunk_res) { 
				for (int z = 0; z < CHUNK_SIZE; z+=chunk_res) {

					Vector3I pos = c.id * CHUNK_SIZE + new Vector3I(x, y, z);

					/*
					if((x == SKIT_STOP)||(y == SKIT_STOP)||(z == SKIT_STOP))
						createSeamlessSkirt(pos, ref st,chunk_res,n_lods,ref quad_count);
					
					else
					*/
						createMeshQuad(pos, ref st,chunk_res,ref quad_count);
					
				}
			}
		}
		st.GenerateNormals();

		// Inserting into scene if has geometry.
		if(quad_count > 0){
			c.mesh.Mesh = st.Commit();
			c.collision.Shape = c.mesh.Mesh.CreateTrimeshShape();

			CallDeferred("add_child",c.mesh);
			if(c.lod <= LODlevel.medium)
				CallDeferred("add_child",c.collision);

			//GD.Print("loaded chunk ",c.id,"lod ",c.lod);
		}
	}


	private float getSampleValue(Vector3 index){
		float 	dist = index.DistanceTo(center), 
				low_noise_value = low_noise.GetNoise3Dv(index),
				high_noise_value = high_noise.GetNoise3Dv(index),
				outer_sphere_value = dist - OUTER_RADIUS + MOUNTAIN_SIZE * low_noise_value,
				inner_sphere = dist - INNER_RADIUS + MOUNTAIN_SIZE * high_noise_value;

		//Blending outer sphere
		float 	f = blendFunction(inner_sphere,high_noise_value,INNER_RADIUS,TRANSITION_STEEPNESS,dist);
				f = blendFunction(f,outer_sphere_value,OUTER_RADIUS-MOUNTAIN_SIZE/4,TRANSITION_STEEPNESS,dist);
		return f;
	}
	
	private float blendFunction(float f, float g, float transition_point,float transition_steepness,float x){
		float s = getSigmoidValue(transition_point,transition_steepness,x);
		return s*f + (1-s)*g;
	}

	private float getSigmoidValue(float transition_point,float transition_steepness,float x){
		return 1/(1+MathF.Exp(transition_steepness*(x-transition_point)));
	}
	

	public override void _Input(InputEvent @event){
		if(Input.IsActionJustPressed("debug_action_1")) {
			

			Task.Run(() => generateMesh());
			//loadChunk(new Vector3I(SIZE,SIZE,SIZE)/2);
			//generateMesh();
		}
		if(Input.IsActionJustPressed("debug_action_2")) {
		
			//Task.Run(() => generateMesh());
			//loadChunk(new Vector3I(SIZE,SIZE,SIZE)/2);
			//generateMesh();
		}
	}

	// Called when the node enters the scene tree for the first time.
	public override void _Ready()
	{
		
		loaded_chunks = new Octree<Chunk>(new Aabb(float.MaxValue/2,-float.MaxValue/2,-float.MaxValue/2, new Vector3(float.MaxValue,float.MaxValue,float.MaxValue)));
		dead_chunks = new List<Chunk>();
		camera = GetViewport().GetCamera3D();
		center = Position;
		RELOAD_VALUE = (CHUNK_SIZE * CHUNK_SIZE)/4;

		generateNoiseTextures(seed);

		// Setting layers
		CollisionLayer = 	((uint)Utilities.collision_layers.Floor); 
		CollisionMask = 	((uint)Utilities.collision_layers.Player) &
							((uint)Utilities.collision_layers.Object) &
							((uint)Utilities.collision_layers.Entity);
	}
	private void killDeadChunks(){
		if(dead_chunks.Count > 0){
			int n = dead_chunks.Count;//Math.Min(dead_chunks.Count,UNLOAD_BATCH_SIZE);
			for(int i=0;i<n; ++i){
				Chunk c = dead_chunks[i];

				//GD.Print("Deleted chunk ", c.id);
				c.mesh.CallDeferred("queue_free");
				if(c.lod <= LODlevel.medium)
					c.collision.CallDeferred("queue_free");
			}
			dead_chunks.RemoveRange(0,n);
			GD.Print("killing dead chunks");
		}
	}

	// Called every frame. 'delta' is the elapsed time since the previous frame.
	public override void _Process(double delta) {
		if(!generating){

			if((camera.Position - last_load).LengthSquared() > RELOAD_VALUE){
				generating = true;
				Vector3 pos = camera.Position, dir = camera.Basis.Z;

				Task.Run(() => generateChunksAt(pos,dir));
			}
		}
		DebugDraw3D.DrawBoxAb(Position- new Vector3(SIZE,SIZE,SIZE)/2,Position + new Vector3(SIZE,SIZE,SIZE)/2,Vector3.Up,Godot.Color.Color8(255,255,0));
		
	}

	private Vector3I getChunkAt(Vector3 position){
		return (Vector3I)(position/CHUNK_SIZE).Floor();
	}


	private async Task generateChunksAt(Vector3 position,Vector3 look_dir) {
		last_load_chunk = getChunkAt(position); last_load = position;
		GD.Print("start:",last_load_chunk);

		ConcurrentBag<Task> chunk_tasks = new ConcurrentBag<Task>();

		//-new Vector3(LOAD_DIST,LOAD_DIST,LOAD_DIST)/2

		// A * for chunk loading, the heuristic is the looking direction (first the forward ones)
		Octree<bool> visited = new Octree<bool>(new Aabb(float.MaxValue/2,-float.MaxValue/2,-float.MaxValue/2, new Vector3(float.MaxValue,float.MaxValue,float.MaxValue)));
		var queue = new PriorityQueue<Vector3I,float>();



		queue.Enqueue(last_load_chunk,0f);
		visited.Insert(last_load_chunk,true);

		while(queue.Count > 0) {
			Vector3I chunk_id = queue.Dequeue();
			LODlevel lod = calculateLOD(chunk_id,last_load_chunk);

			if(loaded_chunks.Find(chunk_id,out Chunk c)){
				if(c.lod != lod){
					//dead_chunks.Add(c);
					c.mesh.CallDeferred("queue_free");
					if(c.lod <=LODlevel.medium)
						c.collision.CallDeferred("queue_free");
					
					c = new Chunk(chunk_id,lod);
					loaded_chunks.Replace(chunk_id,c);
					chunk_tasks.Add(Task.Run(() => updateChunk(ref c)));
				}
			}
			else{
				c = new Chunk(chunk_id,lod);
				loaded_chunks.Insert(chunk_id,c);
				chunk_tasks.Add(Task.Run(() => updateChunk(ref c)));
			}


			for(int i = -1;i <=1; ++i){
				for(int j = -1; j <= 1; ++j){
					for(int k = -1; k <= 1; ++k){
						if(i==0 && j==0 && k == 0)
							continue;


						Vector3I next = chunk_id + new Vector3I(i,j,k);
						Vector3I relative_vector = next-last_load_chunk;

						if(relative_vector.LengthSquared() <= LOAD_DIST * LOAD_DIST && !visited.Find(next,out var d)){
							visited.Insert(next,true);
							float heuristic = look_dir.Dot(relative_vector);
							queue.Enqueue(next,relative_vector.LengthSquared()+heuristic);
						}
					}
				}
			}
		}
		await Task.WhenAll(chunk_tasks.ToArray());

		List<Vector3> far_chunks = new List<Vector3>();
		loaded_chunks.Iterate((pos, c) => { 
			if((pos - last_load_chunk).LengthSquared() > LOAD_DIST * LOAD_DIST)
				far_chunks.Add(pos);
		});
		foreach(Vector3I id in far_chunks){
			loaded_chunks.Remove(id,out Chunk c);
			dead_chunks.Add(c);
		}
		
		GD.Print("Updated ",chunk_tasks.Count," chunks");
		killDeadChunks();
		GD.Print("Killed dead chunks");
		generating = false;
	}

	
	private LODlevel calculateLOD(Vector3I chunk_id){
		return calculateLOD(chunk_id,last_load_chunk);
	}

	private LODlevel calculateLOD(Vector3I chunk_id,Vector3I viewing_from){
		float dist = (float)(chunk_id - viewing_from).LengthSquared()/(LOAD_DIST*LOAD_DIST);

		if(dist <= 0.25){
			return LODlevel.medium;
		}
		
		return LODlevel.lowest;
	}
	private async Task generateMesh(){
		int n = SIZE/CHUNK_SIZE;
		float surface = OUTER_RADIUS/CHUNK_SIZE/2;
		ConcurrentBag<Task> chunk_tasks = new ConcurrentBag<Task>();
		for(int x = -n; x < n; ++x){
			for(int y = -n; y < n; ++y){
				for(int z = -n; z < n; ++z){
					Vector3I chunk_id = new Vector3I(x,y,z);
					if(chunk_id.DistanceTo((Vector3I)center) >= surface){
						var c = new Chunk(chunk_id,LODlevel.lowest);
						chunk_tasks.Add(Task.Run(() => updateChunk(ref c)));
					}
				}
			}
		}
		await Task.WhenAll(chunk_tasks.ToArray());
		GD.Print("Generated Mesh");
	}
	private void createMeshQuad(Vector3I pos,ref SurfaceTool st,int chunk_res,ref int quad_count){
		for (uint i = 0; i < 3; ++i){
			var dir = AXIS[i] * chunk_res;
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

	private void createSeamlessSkirt(Vector3I pos, ref SurfaceTool st, int chunk_lod,int[] n_lods, ref int quad_count) { 
		for (uint i = 0; i < 3; ++i) { 
			/*
			var dirHighRes = AXIS[i] * highRes;
			var dirLowRes = AXIS[i] * lowRes;
			float v1 = getSampleValue(pos); 
			float v2 = getSampleValue(pos + dirHighRes);
			if (v1 < 0 && v2 >= 0) { 
				AddSkirtQuad(ref pos, i, ref st, highRes, lowRes); 
			} 
			else if (v1 >= 0 && v2 < 0) { 
				AddRSkirtQuad(ref pos, i, ref st, highRes, lowRes); 
			} 
			*/
		}
	}

	private void AddSkirtQuad(ref Vector3I pos, uint axisIndex, ref SurfaceTool st, int highRes, int lowRes) { 
		var pointsHighRes = getQuadPoints(pos, axisIndex, highRes); 
		var pointsLowRes = getQuadPoints(pos, axisIndex, lowRes); 

		addVertex(pointsHighRes[0], ref st,lowRes);
		addVertex(pointsLowRes[1], ref st,lowRes);
		addVertex(pointsLowRes[2], ref st,lowRes);

		addVertex(pointsHighRes[0], ref st,lowRes);
		addVertex(pointsHighRes[2], ref st,lowRes);
		addVertex(pointsLowRes[2], ref st,lowRes);
	} 
	private void AddRSkirtQuad(ref Vector3I pos, uint axisIndex, ref SurfaceTool st, int highRes, int lowRes) { 
		var pointsHighRes = getQuadPoints(pos, axisIndex, highRes); 
		var pointsLowRes = getQuadPoints(pos, axisIndex, lowRes); 

		addVertex(pointsHighRes[0], ref st,lowRes);
		addVertex(pointsHighRes[2], ref st,lowRes);
		addVertex(pointsLowRes[1], ref st,lowRes);

		addVertex(pointsHighRes[0], ref st,lowRes);
		addVertex(pointsLowRes[2], ref st,lowRes);
		addVertex(pointsHighRes[2], ref st,lowRes);
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
			new Vector3(getSampleValue(pos + AXIS[0]*chunk_res) - sample_value,
						getSampleValue(pos + AXIS[1]*chunk_res) - sample_value,
						getSampleValue(pos + AXIS[2]*chunk_res) - sample_value).Normalized();
	}
}
