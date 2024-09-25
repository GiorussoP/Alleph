using Godot;
using System;
using System.Linq;



public partial class player : RigidBody3D {

	[Export] Node3D camera;
	private Vector3 wanted_velocity;
	float speed = 20;
	float look_speed = 0.03f;
	float max_cam_dist = 6;
	
	float jump_speed = 50;
	private float camera_distance;
	private float mx,my,lx,ly;
	// Called when the node enters the scene tree for the first time.
	public override void _Ready() {
		
	}

	// Called every frame. 'delta' is the elapsed time since the previous frame.
	public override void _Process(double delta){


		//Camera Angle
		if(Mathf.Abs(ly)> 0.2 || Mathf.Abs(lx) > 0.2){ 
			camera.Rotate(-Basis.Y,lx*look_speed);
			camera.Rotate(camera.Transform.Basis.X,ly*look_speed);
		}
		camera.LookAt(camera.Position-camera.Basis.Z,vector3Lerp(camera.Basis.Y,Basis.Y.Slide(camera.Basis.Z),0.5f));

		//Camera position
		Vector3 cam_pos = Position + camera_distance*(camera.Transform.Basis.Z + Basis.Y/3).Normalized();
		camera.Position = vector3Lerp(camera.Position,cam_pos,0.5f);
	}
    public override void _Input(InputEvent @event){
        
		if(Input.IsActionJustPressed("reset")) Position = Vector3.Zero;

		my = Input.GetActionRawStrength("move_forward") - Input.GetActionRawStrength("move_backward");
		mx = Input.GetActionRawStrength("move_right") - Input.GetActionRawStrength("move_left");

		ly = Input.GetActionRawStrength("look_up") - Input.GetActionRawStrength("look_down");
		lx = Input.GetActionRawStrength("look_right") - Input.GetActionRawStrength("look_left");

		float r = Input.GetActionRawStrength("move_roll_r") - Input.GetActionRawStrength("move_roll_l"); 


		if(Mathf.Abs(r) > 0.2){
			AngularVelocity += -Basis.Z*r;
		}
		
		

		if(Mathf.Abs(mx) > 0.2 || Mathf.Abs(my) > 0.2){
			LinearVelocity = findMoveVector(mx, my) * speed;
			//Adjust model orientation
		}
		else {
			LinearVelocity = Vector3.Zero;
		}
	
		if(Input.IsActionJustPressed("move_jump")) {
			LinearVelocity += Vector3.Up * jump_speed;
		}


		
    }

	private Vector3 vector3Lerp(Vector3 v1, Vector3 v2, float factor){
		return new Vector3(	Mathf.Lerp((float)v1.X,(float)v2.X,factor),
							Mathf.Lerp((float)v1.Y,(float)v2.Y,factor),
							Mathf.Lerp((float)v1.Z,(float)v2.Z,factor));
	}

	private Vector3 findMoveVector(float mx, float my) {
		Vector3 move_vector = (mx*(camera.Basis.X.Slide(Basis.Y)).Normalized() - my*(camera.Basis.Z.Slide(Basis.Y)).Normalized());

		return move_vector;
	}
	public override void _PhysicsProcess(double delta) {
        var query = PhysicsRayQueryParameters3D.Create(Position,Position + max_cam_dist*camera.Basis.Z.Normalized());
        var result = GetWorld3D().DirectSpaceState.IntersectRay(query);
		if (result.Count > 0){
			GD.Print("Hit at point: ", result["position"]);
			camera_distance = ((Vector3) result["position"] - Position).Length() - camera.Scale.X;
		}
		else {
			camera_distance = max_cam_dist;
		}
    }
}
