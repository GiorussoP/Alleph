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
	
						INITIAL_CHUNK_SIZE = 120, // In units
						CHUNK_DETAIL = 15,	//How many vertices should a chunk have in each direction
						N_LEVELS = 3,
						RELOAD_VALUE = INITIAL_CHUNK_SIZE/2,



						LOAD_DIST = 64, //Render distance, in chunks
						CHUNK_BATCH_SIZE =  4,		//How many chunks are updated on a single frame
						COLLISION_BATCH_SIZE = 1;
	private readonly float 			OUTER_RADIUS = 2048,
									INNER_RADIUS = 1024,
									MOUNTAIN_SIZE = 200,
									TRANSITION_STEEPNESS = 0.2f,
									LOW_NOISE_SCALE = 0.005f,
									HIGH_NOISE_SCALE = 0.01f;
	/*

	
	*/

	private Camera3D camera;
	[Export] private int seed = 0;
	private FastNoiseLite high_noise,low_noise;
	private Vector3 center, last_load = Vector3.Zero;
	private bool busy = false;


	private List<Chunk> pending_chunks, loaded_chunks, collision_chunks, dead_chunks;

	private struct Chunk {
		public Vector3I position;
		public int size;
		public MeshInstance3D mesh;
		public CollisionShape3D collision;

		public Chunk(Vector3I pos, int chunk_size){
			position = pos;
			size = chunk_size;
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
/*
	// Reloads a chunk that is inserted in the octree
	private void updateChunk(ref Chunk c){
		// busy chunk
		int chunk_res = (int)c.lod, quad_count = 0;
		SurfaceTool st = new SurfaceTool();

		st.Begin(Mesh.PrimitiveType.Triangles);

		var material = new StandardMaterial3D();
		material.VertexColorUseAsAlbedo = true;
		st.SetMaterial(material);


		int SKIT_STOP = INITIAL_CHUNK_SIZE - chunk_res;

		int[] n_lods = {(int)calculateLOD(c.id + Vector3I.Right),(int)calculateLOD(c.id + Vector3I.Up),(int)calculateLOD(c.id + Vector3I.Back)};

		for (int x = 0; x < INITIAL_CHUNK_SIZE; x+=chunk_res) {
			for (int y = 0; y < INITIAL_CHUNK_SIZE; y+=chunk_res) { 
				for (int z = 0; z < INITIAL_CHUNK_SIZE; z+=chunk_res) {

					Vector3I pos = c.id * INITIAL_CHUNK_SIZE + new Vector3I(x, y, z);

					/*
					if((x == SKIT_STOP)||(y == SKIT_STOP)||(z == SKIT_STOP))
						createSeamlessSkirt(pos, ref st,chunk_res,n_lods,ref quad_count);
					
					else
			
					createMeshQuad(pos, ref st,chunk_res,ref quad_count);
					
				}
			}
		}
		st.GenerateNormals();

		// Inserting into scene if has geometry.
		if(quad_count > 0){
			c.mesh.Mesh = st.Commit();
			c.mesh.LodBias = 0.01f;
			c.collision.Shape = c.mesh.Mesh.CreateTrimeshShape();

			CallDeferred("add_child",c.mesh);
			if(c.lod <= LODlevel.medium)
				CallDeferred("add_child",c.collision);

			//GD.Print("loaded chunk ",c.id,"lod ",c.lod);
		}
	
	}
	*/



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
			
			CollisionLayer = (uint)Util.collision_layers.Floor;
			CollisionMask = (uint)Util.collision_layers.Player;
			

			//loadChunk(new Vector3I(SIZE,SIZE,SIZE)/2);
			//generateMesh();
		}
		if(Input.IsActionJustPressed("debug_action_2")) {
			CollisionLayer = 0;
			CollisionMask = 0;
		}
	}

	// Called when the node enters the scene tree for the first time.
	public override void _Ready()
	{
		GetViewport().DebugDraw = Viewport.DebugDrawEnum.Wireframe;

		loaded_chunks = new List<Chunk>();//new Octree<Chunk>(new Aabb(float.MaxValue/2,-float.MaxValue/2,-float.MaxValue/2, new Vector3(float.MaxValue,float.MaxValue,float.MaxValue)));
		dead_chunks = new List<Chunk>();
		pending_chunks = new List<Chunk>();
		collision_chunks = new List<Chunk>();

		camera = GetViewport().GetCamera3D();
		center = Position;
		last_load = camera.Position;


		
		//CallDeferred("add_child",c.mesh);
		//CallDeferred("add_child",c.collision);

		generateNoiseTextures(seed);

		// Setting layers


		CollisionLayer = 0;
		CollisionMask = 0;
		//CollisionLayer = 	0;//((uint)Util.collision_layers.Floor);
		//CollisionMask = 	0;//((uint)Util.collision_layers.Player) &((uint)Util.collision_layers.Object) & ((uint)Util.collision_layers.Entity);
	}

	// Called every frame. 'delta' is the elapsed time since the previous frame.

	private Vector3I getChunkAt(Vector3 position){
		return (Vector3I)(position/INITIAL_CHUNK_SIZE).Floor() * INITIAL_CHUNK_SIZE;
	}
	public override void _Process(double delta) {

		//GD.Print("Frame delay: ",delta);
		if(!busy){
			if(pending_chunks.Count > 0){
				busy = true;
				//updateChunkBatch();
				Task.Run(() => updateChunkBatch());
			}

			/*
			else if(collision_chunks.Count > 0){
				busy = true;

				Task.Run(() => updateCollisionBatch());
			}
			*/
	
			else if((camera.Position - last_load).LengthSquared() > RELOAD_VALUE * RELOAD_VALUE){
				busy = true;
				Vector3 pos = camera.Position;

				Task.Run(() => generateChunksAt(pos));
			}
		}
		DebugDraw3D.DrawBoxAb(Position- new Vector3(SIZE,SIZE,SIZE)/2,Position + new Vector3(SIZE,SIZE,SIZE)/2,Vector3.Up,Godot.Color.Color8(255,255,0));
	}


	private void updateChunkBatch(){
		

		int n = Math.Min(pending_chunks.Count,CHUNK_BATCH_SIZE);
		for(int i = 0; i < n; ++i){

			CallDeferred("add_child",pending_chunks[i].mesh);
			if(pending_chunks[i].size == INITIAL_CHUNK_SIZE){
				CallDeferred("add_child",pending_chunks[i].collision);
				//collision_chunks.Add(pending_chunks[i]);
			}
			loaded_chunks.Add(pending_chunks[i]);
		}
		pending_chunks.RemoveRange(0,n);
		GD.Print("Loaded ",n);

		n = Math.Min(dead_chunks.Count,CHUNK_BATCH_SIZE);
		for(int i = 0; i < n; ++i){


			dead_chunks[i].mesh.CallDeferred("queue_free");
			if(dead_chunks[i].size == INITIAL_CHUNK_SIZE){
				dead_chunks[i].collision.SetDeferred("disabled",true);
				dead_chunks[i].collision.CallDeferred("queue_free");
			}
				
		}
		dead_chunks.RemoveRange(0,n);
		GD.Print("Killed ",n);

		

		busy = false;
	}

	private void updateCollisionBatch(){
		int n = Math.Min(collision_chunks.Count,COLLISION_BATCH_SIZE);
		for(int i = 0; i < n; ++i){
			collision_chunks[i].collision.SetDeferred("disabled",false);//.collision.Disabled = false;//CallDeferred("Disabled",false);
		}
		collision_chunks.RemoveRange(0,n);
		GD.Print("Collision enabled ",n);
		busy = false;
	}
	private void generateChunk(ref Chunk c){
		int chunk_res = c.size/CHUNK_DETAIL;
		int quad_count = 0;

		SurfaceTool st = new SurfaceTool();
		st.Begin(Mesh.PrimitiveType.Triangles);

		var material = new StandardMaterial3D();
		material.VertexColorUseAsAlbedo = true;
		st.SetMaterial(material);


		for (int x = 0; x < c.size; x+=chunk_res) {
			for (int y = 0; y < c.size; y+=chunk_res) {
				for (int z = 0; z < c.size; z+=chunk_res) {

					Vector3I pos =  c.position + new Vector3I(x, y, z);
					//GD.Print("i");
					createMeshQuad(pos, ref st,chunk_res,ref quad_count);
				}
			}
		}
		st.GenerateNormals();
		if(quad_count > 0){
			c.mesh.Mesh = st.Commit();
			c.collision.Disabled = true;
			if(c.size == INITIAL_CHUNK_SIZE)
				c.collision.Shape = c.mesh.Mesh.CreateTrimeshShape();

			//GD.Print("generated chunk: ",c.position,", size: ", c.size);
		}
	}
	private async void generateChunksAt(Vector3 position) {
		GD.Print("Generating");
		last_load = position;
		//Cleaning octree

		dead_chunks = loaded_chunks;
		loaded_chunks = new List<Chunk>();//new Octree<Chunk>(new Aabb(float.MaxValue/2,-float.MaxValue/2,-float.MaxValue/2, new Vector3(float.MaxValue,float.MaxValue,float.MaxValue)));
		ConcurrentBag<Task> chunk_tasks = new ConcurrentBag<Task>();


		int chunk_size = INITIAL_CHUNK_SIZE;
		Vector3I start_pos = getChunkAt(position);

		Chunk c = new Chunk(start_pos,chunk_size);
		pending_chunks.Add(c);
		chunk_tasks.Add(Task.Run(()=>generateChunk(ref c)));
		


		int last_chunk_size = 0;
		Vector3I last_offset = Vector3I.Zero;
		for(int level = 0; level < N_LEVELS; ++level, chunk_size*=3){
			int chunk_res = chunk_size/CHUNK_DETAIL;

			Vector3I offset = last_offset +  new Vector3I(last_chunk_size,last_chunk_size,last_chunk_size);
			
			for(int i = -1;i <=1; ++i){
				for(int j = -1; j <= 1; ++j){
					for(int k = -1; k <= 1; ++k){
						if(i==0 && j==0 && k == 0)
							continue;

						
						// Pensar melhor essa matemática, mas tá quase!
						Vector3I pos = start_pos +chunk_size*new Vector3I(i,j,k) - offset;
						Chunk a = new Chunk(pos,chunk_size);
						//generateChunk(ref a);
						pending_chunks.Add(a);
						chunk_tasks.Add(Task.Run(() => generateChunk(ref a)));
					}
				}
			}

			last_chunk_size = chunk_size;
			last_offset = offset;
		}
		GD.Print("Awaiting");
		await Task.WhenAll(chunk_tasks.ToArray());
		GD.Print("Done");
		busy = false;
		//busy = false;
	}

	/*
	private async Task generateMesh(){
	

		ConcurrentBag<Task> chunk_tasks  = new ConcurrentBag<Task>();

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
	*/
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
