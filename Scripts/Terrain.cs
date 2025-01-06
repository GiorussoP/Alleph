using Godot;
using System;
using System.Collections.Generic;
using System.Collections.Concurrent;
using System.Threading.Tasks;
using System.Linq;

public partial class Terrain : StaticBody3D
{
	

	private static readonly int SIZE = 1024, 
								CHUNK_SIZE = 32, 
								LOAD_DIST = 10, //Render distance, in chunks 
								UNLOAD_BATCH_SIZE = 4;
	private readonly float 			OUTER_RADIUS = 512,
									INNER_RADIUS = 50,
									TRANSITION_OFFSET = 5,
									TRANSITION_SMOOTHNESS = 0.08f,	// Between 0 and 1
									NOISE_SCALE = 0.01f;

	[Export] public Node3D camera;
	private FastNoiseLite noise;
	private Vector3 center, last_load = Vector3.Zero;
	private static int RELOAD_VALUE;
	private bool generating = false;

	List<Chunk> dead_chunks = new List<Chunk>();

	private Octree<Chunk> octree = 	new Octree<Chunk>(new Aabb(-SIZE/2,-SIZE/2,-SIZE/2,new Vector3(SIZE,SIZE,SIZE)));

	private enum LODlevel {
		ultra = 1,
		high = 2,
		medium = 4,
		low = 8
	}
	private struct Chunk {
		public bool active;
		public LODlevel lod;
		public MeshInstance3D mesh;
		public CollisionShape3D collision;

		public Chunk(){
			active = false;
			lod = LODlevel.low;
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

	private void generateNoiseTexture() { 
		noise = new FastNoiseLite(); 
		noise.SetSeed(10);//(int)GD.Randi()
		noise.SetNoiseType(FastNoiseLite.NoiseTypeEnum.Perlin); 
		noise.SetFrequency(NOISE_SCALE);

		GD.Print("Generated Noise Texture");
	}

	// Reloads a chunk that is inserted in the octree
	private void loadChunk(Vector3I chunk,LODlevel detail){
	

		bool found = false;
		if(!octree.Find(chunk, out Chunk c)){
			c = new Chunk();
		}
		else {
			if(c.lod == detail)
				return;
			found = true;
			dead_chunks.Add(c);
			c = new Chunk();
		}


		// Generating chunk
		int chunk_res = (int)detail, quad_count = 0;
		SurfaceTool st = new SurfaceTool();


		

		st.Begin(Mesh.PrimitiveType.Triangles);

		var material = new StandardMaterial3D();
		material.VertexColorUseAsAlbedo = true;
		st.SetMaterial(material);

		for (int x = 0; x < CHUNK_SIZE; x+=chunk_res) {
			for (int y = 0; y < CHUNK_SIZE; y+=chunk_res) { 
				for (int z = 0; z < CHUNK_SIZE; z+=chunk_res) {
					Vector3I pos = new Vector3I(x, y, z) + chunk * CHUNK_SIZE;
					createMeshQuad(pos, ref st,chunk_res,ref quad_count);
				}
			}
		}

		// Inserting into scene if has geometry.
		if(quad_count > 0){
			c.lod = detail;
			c.mesh.Mesh = st.Commit();
			c.collision.Shape = c.mesh.Mesh.CreateTrimeshShape();
			//UnloadChunk(ref c);

			if(!found)
				octree.Insert(chunk, c);

			CallDeferred("add_child",c.mesh);
			CallDeferred("add_child",c.collision);

			GD.Print("loaded chunk ",chunk);
		}
	}
	private void UnloadChunk(Vector3I chunk) {
		if(octree.Remove(chunk, out var data)){
			//chunks_for_deletion.Add(data);
			//data.mesh.QueueFree();
			//data.collision.QueueFree();
		};
	}

	private float getSampleValue(Vector3 index){
		float 	dist = index.DistanceTo(center), 
				value = noise.GetNoise3Dv(index),
				outer_sphere = dist - OUTER_RADIUS,
				inner_sphere = dist - INNER_RADIUS;

		//Blending outer sphere
		float s = 1-(1/(1+TRANSITION_SMOOTHNESS*MathF.Exp(OUTER_RADIUS+TRANSITION_OFFSET-dist)));
		float f = (1-s)*outer_sphere + s*value;

		//Blending inner sphere
		s = 1-(1/(1+TRANSITION_SMOOTHNESS*MathF.Exp(INNER_RADIUS-dist)));
		return (1-s)*f + s*inner_sphere;
	}
	

	public override void _Input(InputEvent @event){
		if(Input.IsActionJustPressed("debug_action_1")) {
			

			//Task.Run(() => generateMesh());
			//loadChunk(new Vector3I(SIZE,SIZE,SIZE)/2);
			//generateMesh();
		}
		if(Input.IsActionJustPressed("debug_action_2")) {
			var pos = getChunkAt(camera.Position);
			if(octree.Remove(pos, out var c)){
				UnloadChunk(pos);
			};
		
			//Task.Run(() => generateMesh());
			//loadChunk(new Vector3I(SIZE,SIZE,SIZE)/2);
			//generateMesh();
		}
	}

	// Called when the node enters the scene tree for the first time.
	public override void _Ready()
	{
		center = Position;
		RELOAD_VALUE = CHUNK_SIZE * CHUNK_SIZE;

		generateNoiseTexture();

		// Setting layers
		CollisionLayer = 	((uint)Utilities.collision_layers.Floor); 
		CollisionMask = 	((uint)Utilities.collision_layers.Player) &
							((uint)Utilities.collision_layers.Object) &
							((uint)Utilities.collision_layers.Entity);
	}

	// Called every frame. 'delta' is the elapsed time since the previous frame.
	public override void _Process(double delta) {
		if(!generating){
			if(dead_chunks.Count >= UNLOAD_BATCH_SIZE){
				GD.Print("Removing ",dead_chunks.Count," chunks");
				for(int i = 0; i < UNLOAD_BATCH_SIZE; ++i){
					dead_chunks[i].mesh.CallDeferred("queue_free");
					dead_chunks[i].collision.CallDeferred("queue_free");
				}
				dead_chunks.RemoveRange(0,UNLOAD_BATCH_SIZE);

				//Task.Run(() => deleteDeadChunks());
			}
			{
				if((camera.Position - last_load).LengthSquared() > RELOAD_VALUE){
					generating = true;
					Vector3 pos = camera.Position, dir = camera.Basis.Z;

					Task.Run(() => generateChunksAt(pos,dir));
				}
			}
		}

		DebugDraw3D.DrawBoxAb(Position- new Vector3(SIZE,SIZE,SIZE)/2,Position + new Vector3(SIZE,SIZE,SIZE)/2,Vector3.Up,Godot.Color.Color8(255,255,0));
	}

	private Vector3I getChunkAt(Vector3 position){
		return (Vector3I)(position/CHUNK_SIZE).Floor();
	}

	private void deleteDeadChunks(){
		
	}

	private async Task generateChunksAt(Vector3 position,Vector3 look_dir) {
		Vector3I start = getChunkAt(position); last_load = position;
		GD.Print("start:",start);

		ConcurrentBag<Task> chunk_tasks = new ConcurrentBag<Task>();

		//-new Vector3(LOAD_DIST,LOAD_DIST,LOAD_DIST)/2


		

		
		

		
		// A * for chunk loading, the heuristic is the looking direction (first the forward ones)
		Octree<bool> visited = new Octree<bool>(new Aabb(-SIZE/2,-SIZE/2,-SIZE/2,new Vector3(SIZE,SIZE,SIZE)));
		visited.Insert(new Vector3(0,0,0),true);
		var queue = new PriorityQueue<Vector3I,float>();
		queue.Enqueue(new Vector3I(0,0,0),0f); 
		while(queue.Count > 0) {
			Vector3I id = queue.Dequeue();
			//GD.Print("Dequeueing",id);

			LODlevel lod = LODlevel.low; float dist = id.Length()/(float)LOAD_DIST;
		
			/*
			if(dist < 0.75)
				if(dist < 0.5)
					if(dist < 0.25)
						lod = LODlevel.ultra;
					else
						lod = LODlevel.high;
				else
					lod = LODlevel.medium;
			*/

			chunk_tasks.Add(Task.Run(() =>loadChunk(start+id,lod)));

			for(int i = -1;i <=1; ++i){
				for(int j = -1; j <= 1; ++j){
					for(int k = -1; k <= 1; ++k){

						if(i==0 && j==0 && k == 0)
							continue;

						Vector3I neighbour = id + new Vector3I(i,j,k);
						dist = neighbour.Length();

						if(dist <=LOAD_DIST){
							if(!visited.Find(neighbour, out var c)){
								visited.Insert(neighbour, true);

								float heuristic = look_dir.Dot(neighbour);
								queue.Enqueue(neighbour,dist+heuristic);
							}
						}
					}
				}
			}
			//GD.Print("Q:",queue.Count);
		}
		await Task.WhenAll(chunk_tasks.ToArray());

		// After chunk generation is complete, remove faraway chunks from the octree and mark them as dead.
		List<Vector3> chunks_removed = new List<Vector3>();
		octree.Iterate((chunk_pos, chunk) => { 
			if((chunk_pos - start).LengthSquared() > LOAD_DIST * LOAD_DIST){
				chunks_removed.Add(chunk_pos);
			}
		});
		foreach(Vector3I chunk in chunks_removed){
			if(octree.Remove(chunk, out Chunk c)){
				dead_chunks.Add(c);
			}
			else GD.PrintErr("COULDNT REMOVE FROM OCTREE");
		}
		GD.Print("Finished generating");
		
		generating = false;

		/*
		for (int x = -LOAD_DIST; x <= LOAD_DIST;++x) {
			for (int y = -LOAD_DIST; y <= LOAD_DIST; ++y) {
				for (int z = -LOAD_DIST; z <= LOAD_DIST; ++z) {
					Vector3I pos = new Vector3I(x,y,z);
					float dist = pos.Length()/LOAD_DIST;


					LODlevel detail = LODlevel.low;
					
					
					if(dist > 0.75){
						detail = LODlevel.low;
					}
					else if(dist > 0.5){
						detail = LODlevel.low;
					}
					else if(dist > 0.25){
						detail = LODlevel.high;
					}
					else
						detail = LODlevel.ultra;


					
				}
			}
		}

		*/
	}
	private void generateMesh(){

		for(int x = 0; x < SIZE/CHUNK_SIZE; ++x){
			for(int y = 0; y < SIZE/CHUNK_SIZE; ++y){
				for(int z = 0; z < SIZE/CHUNK_SIZE; ++z){
				Task load_chunks = Task.Run(() => loadChunk(new Vector3I(x,y,z),LODlevel.low));
				}
			}
		}
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

	private void createSeamlessSkirt(Vector3I pos, ref SurfaceTool st, int highRes, int lowRes) { 
		for (uint i = 0; i < 3; ++i) { 
			var dirHighRes = AXIS[i] * highRes; 
			var dirLowRes = AXIS[i] * lowRes; 
			float v1 = getSampleValue(pos); 
			float v2 = getSampleValue(pos + dirHighRes); 
			float v3 = getSampleValue(pos + dirLowRes); 
			if (v1 < 0 && v2 >= 0) { 
				AddSkirtQuad(ref pos, i, ref st, highRes, lowRes); 
			} 
			else if (v1 >= 0 && v2 < 0) { 
				AddRSkirtQuad(ref pos, i, ref st, highRes, lowRes); 
			} 
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
		st.SetNormal(surface_gradient);
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
