using Godot;
using System;
using System.Drawing;
using System.Threading.Tasks;
public partial class Terrain : StaticBody3D
{
	

	private static readonly int SIZE = 1024, CHUNK_SIZE = 16, LOAD_DIST = 2;

	private static readonly float 	OUTER_RADIUS = 500,
									INNER_RADIUS = 100,
									TRANSITION_OFFSET = 5,
									TRANSITION_SMOOTHNESS = 0.08f,	// Between 0 and 1
									NOISE_SCALE = 0.07f;
	private  SurfaceTool st = new SurfaceTool();
	private FastNoiseLite noise;
	private Vector3 center;
	[Export] public Node3D player;

	private struct Chunk {
		public MeshInstance3D mesh;
		public CollisionShape3D collision;

		public Chunk(){
			mesh = new MeshInstance3D();
			collision = new CollisionShape3D();
		}
	}
	private Octree<Chunk> octree = 	new Octree<Chunk>(
									new Aabb(-float.MaxValue/2,-float.MaxValue/2,-float.MaxValue/2,
									new Vector3(float.MaxValue,float.MaxValue,float.MaxValue)));
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
		noise.SetSeed((int)GD.Randi());
		noise.SetNoiseType(FastNoiseLite.NoiseTypeEnum.Perlin); 
		noise.SetFrequency(NOISE_SCALE);

		GD.Print("Generated Noise Texture");
	}

	private void loadChunk(Vector3I chunk){
		if(!octree.Find(chunk,out var data)){
			Chunk c = new Chunk();

			GD.Print("loading chunk ",chunk);
			st.Begin(Mesh.PrimitiveType.Triangles);
			for (int x = 0; x < CHUNK_SIZE; ++x) { 
				for (int y = 0; y < CHUNK_SIZE; ++y) { 
					for (int z = 0; z < CHUNK_SIZE; ++z) { 
						createMeshQuad(new Vector3I(x, y, z) + chunk * CHUNK_SIZE); 
					}
				}
			}
			c.mesh.Mesh =  st.Commit();
			c.collision.Shape = c.mesh.Mesh.CreateTrimeshShape();

			octree.Insert(chunk,c);
			CallDeferred("add_child",c.mesh);
			CallDeferred("add_child",c.collision);
		}
	}
	private void UnloadChunk(Vector3I chunk) { 
		if (octree.Remove(chunk, out var chunk_struct)) { 
			chunk_struct.mesh.QueueFree();
			chunk_struct.collision.QueueFree();
		}
	}

	private float getSampleValue(Vector3 index){
		float 	dist = index.DistanceTo(center), 
				value = noise.GetNoise3Dv(index),
				outer_sphere = dist - OUTER_RADIUS,
				inner_sphere = dist - INNER_RADIUS;

		float s = 1-(1/(1+TRANSITION_SMOOTHNESS*MathF.Exp(OUTER_RADIUS+TRANSITION_OFFSET-dist)));

		float f = (1-s)*outer_sphere + s*value;

		s = 1-(1/(1+TRANSITION_SMOOTHNESS*MathF.Exp(INNER_RADIUS-dist)));

		return (1-s)*f + s*inner_sphere;
	}
	

	public override void _Input(InputEvent @event){
		if(Input.IsActionJustPressed("debug_action_1")) {
			generateChunksAt(player.Position);
			//loadChunk(new Vector3I(SIZE,SIZE,SIZE)/2);
			//generateMesh();
		}
	}

	// Called when the node enters the scene tree for the first time.
	public override void _Ready()
	{
		Position = SIZE/2 * new Vector3I(-1,-1,-1);
		center = 0.5f * new Vector3(SIZE,SIZE,SIZE);

		generateNoiseTexture();

		// Setting layers
		CollisionLayer = 	((uint)Utilities.collision_layers.Floor); 
		CollisionMask = 	((uint)Utilities.collision_layers.Player) &
							((uint)Utilities.collision_layers.Object) &
							((uint)Utilities.collision_layers.Entity);
	}

	// Called every frame. 'delta' is the elapsed time since the previous frame.
	public override void _Process(double delta) {
		DebugDraw3D.DrawBoxAb(Position,Position + new Vector3(SIZE,SIZE,SIZE),Vector3.Up,Godot.Color.Color8(255,255,0));
	}

	private async void generateChunksAt(Vector3 position) {
		Vector3I start = (((Vector3I)position.Round()+ new Vector3I(SIZE,SIZE,SIZE)/2)-new Vector3I(LOAD_DIST,LOAD_DIST,LOAD_DIST)/2)/CHUNK_SIZE;

		for (int x = -LOAD_DIST; x <= LOAD_DIST; ++x) {
			for (int y = -LOAD_DIST; y <= LOAD_DIST; ++y) {
				for (int z = -LOAD_DIST; z <= LOAD_DIST; ++z) {
					Vector3I pos = (start + new Vector3I(x,y,z)); Chunk data;
					if(!octree.Find(pos,out data)){
						await Task.Run(() =>loadChunk(pos));
					}
				}
			}
		}
	}
	private async void generateMesh(){

		for(int x = 0; x < SIZE/CHUNK_SIZE; ++x){
			for(int y = 0; y < SIZE/CHUNK_SIZE; ++y){
				for(int z = 0; z < SIZE/CHUNK_SIZE; ++z){
					await Task.Run(()=>loadChunk(new Vector3I(x,y,z)));
				}
			}
		}
		GD.Print("Generated Mesh");
	}
	private void createMeshQuad(Vector3I pos){
		for (uint i = 0; i < 3; ++i){
			var dir = AXIS[i];
			float v1 = getSampleValue(pos);
			float v2 = getSampleValue(pos+dir);

			if (v1 < 0 && v2 >=0)
				addQuad(ref pos,i);
			else if (v1 >=0 && v2 <0)
				addRQuad(ref pos,i);
		}
	}

	private void addQuad(ref Vector3I pos, uint axis_index){
		var points = getQuadPoints(pos,axis_index);

		addVertex(points[0]);
		addVertex(points[1]);
		addVertex(points[2]);

		addVertex(points[0]);
		addVertex(points[2]);
		addVertex(points[3]);
	}
	private void addRQuad(ref Vector3I pos, uint axis_index){
		var points = getQuadPoints(pos,axis_index);

		addVertex(points[0]);
		addVertex(points[2]);
		addVertex(points[1]);

		addVertex(points[0]);
		addVertex(points[3]);
		addVertex(points[2]);
	}

	private Vector3I[] getQuadPoints(Vector3I pos,uint axis_index){
		return new Vector3I[] {
			pos + QUAD_POINTS[axis_index][0],
			pos + QUAD_POINTS[axis_index][1],
			pos + QUAD_POINTS[axis_index][2],
			pos + QUAD_POINTS[axis_index][3]
		};
	}

	private Vector3 getSurfacePosition(Vector3I pos){
		var total = Vector3.Zero;
		var surface_edge_count = 0;

		foreach(var edge in EDGES){
			var pos_a = (Vector3) (pos + edge[0]);
			var sample_a = getSampleValue(pos_a);
			var pos_b = (Vector3) (pos + edge[1]);
			var sample_b = getSampleValue(pos_b);

			if(sample_a * sample_b <= 0){
				++surface_edge_count;
				total += pos_a.Lerp(pos_b,Math.Abs(sample_a)/(Math.Abs(sample_a) + Math.Abs(sample_b)));
			}
		}

		if(surface_edge_count == 0)
			return ((Vector3)pos) + Vector3.One * 0.5f;

		return total / surface_edge_count;
	}

	private void addVertex(Vector3I pos){
		var sample_value = getSampleValue(pos);
		var surface_pos = getSurfacePosition(pos);
		var surface_gradient = getSurfaceGradient(pos, sample_value);

		st.SetNormal(surface_gradient);
		st.AddVertex(surface_pos);
	}

	private Vector3 getSurfaceGradient(Vector3I pos, float sample_value){
		return 
			new Vector3(getSampleValue(pos + AXIS[0]) - sample_value,
						getSampleValue(pos + AXIS[1]) - sample_value,
						getSampleValue(pos + AXIS[2]) - sample_value).Normalized();
	}
}
