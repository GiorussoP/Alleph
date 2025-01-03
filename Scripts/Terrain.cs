using Godot;
using System;
using System.Drawing;
using System.Threading.Tasks;
public partial class Terrain : StaticBody3D
{
	
	private  SurfaceTool st = new SurfaceTool();

	private FastNoiseLite noise;
	[Export] private int SIZE = 256;
	[Export] private float SCALE = 0.1f;

	private Vector3 center;
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

	private static readonly  Vector3I[][] EDGES = new Vector3I[][] {

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
		noise.SetFrequency(0.02f);

		GD.Print("Generated Noise Texture");
	}


	// Called when the node enters the scene tree for the first time.
	public override async void _Ready()
	{
		

		var mesh_instance = GetNode<MeshInstance3D>("MeshInstance3D");
		var collision_shape = GetNode<CollisionShape3D>("CollisionShape3D");
		center = 0.5f * new Vector3(SIZE,SIZE,SIZE);

		st.Begin(Mesh.PrimitiveType.Triangles);
		st.SetColor(Colors.Blue);
		st.SetSmoothGroup(0);

		await Task.Run(() => generateNoiseTexture());
		await Task.Run(() => generateMesh());

		var mesh = st.Commit();
		mesh_instance.Mesh = mesh;

		// Setting collision shap
		var shape = new ConvexPolygonShape3D();

		collision_shape.Shape = mesh.CreateTrimeshShape();

		// Setting layers
		CollisionLayer = 	((uint)Utilities.collision_layers.Floor); 
		CollisionMask = 	((uint)Utilities.collision_layers.Player) & 
							((uint)Utilities.collision_layers.Object) & 
							((uint)Utilities.collision_layers.Entity);

		

		GD.Print("Finished Generating Terrain");
	}

	// Called every frame. 'delta' is the elapsed time since the previous frame.
	public override void _Process(double delta) {
		DebugDraw3D.DrawBoxAb(Position,Position + new Vector3(SIZE,SIZE,SIZE),Vector3.Up,Godot.Color.Color8(255,255,0));
	}

	private float getSampleValue(Vector3 index){
		float radius = (SIZE-5)/2;
		float dist = index.DistanceTo(center);
		return (dist < (radius))? noise.GetNoise3Dv(index): dist - radius; //index.DistanceTo(Vector3.Zero)-19.0f;
	}

	private void generateMesh(){

		for(int x = 0; x < SIZE; ++x){
			for(int y = 0; y < SIZE; ++y){
				for(int z = 0; z < SIZE; ++z){
					createMeshQuad(new Vector3I(x,y,z));
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
