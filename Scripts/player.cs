using Godot;
using System;
using System.Diagnostics;
using System.Linq;




public partial class player : SpriteEntity {
	
	[Export] float speed = 20;
	[Export] float look_speed = 0.03f;
	[Export] float max_cam_dist = 3;
	[Export] float jump_speed = 50;


	/// <summary>
	/// /DEBUG
	bool debugging = false;
	/// </summary>

	private bool jumping = false;
	private float camera_distance;
	private Vector3 camera_target;

	private float mx,my, mz, lx,ly;

	public player() {
		
		front_direction = -Basis.Z;
	}

	// Called when the node enters the scene tree for the first time.
	public override void _Ready() {
		base._Ready();
		pauseAnimation(2);
	}

	// Called every frame. 'delta' is the elapsed time since the previous frame.
	public override void _Process(double delta){
		//Camera Angle
		if(Mathf.Abs(ly)> 0.2 || Mathf.Abs(lx) > 0.2){ 
			camera.Rotate(-up_direction,lx*look_speed);
			camera.Rotate(camera.Transform.Basis.X,ly*look_speed);
		}
		camera.LookAt(camera.Position-camera.Basis.Z,utilities.vector3Lerp(camera.Basis.Y,up_direction.Slide(camera.Basis.Z),0.1f));



		//Camera position
		if(!jumping) {
			camera_target = utilities.vector3Lerp(camera_target,Position + 0.2f*camera_distance*up_direction,0.5f);
		}
		else {
			camera_target += (Position-camera_target).Slide(up_direction);
		}
		camera.Position = utilities.vector3Lerp(camera.Position, camera_target + camera_distance*camera.Transform.Basis.Z,0.5f);

	if(debugging){
		DebugDraw3D.DrawLine(Position,closest_ground,Color.Color8(255,100,100));
		DebugDraw3D.DrawLine(Position,Position + 5.0f* up_direction,Color.Color8(100,255,100));
		DebugDraw3D.DrawArrow(Position,Position + 5.0f*front_direction,Color.Color8(255,255,0), 0.1f, true);
	}
		




		base._Process(delta);
	}
    public override void _Input(InputEvent @event){

		if(Input.IsActionJustPressed("reset"))
			Position = Vector3.Zero;

		my = Input.GetActionRawStrength("move_forward") - 	Input.GetActionRawStrength("move_backward");
		mx = Input.GetActionRawStrength("move_right") 	- 	Input.GetActionRawStrength("move_left");
		mz = Input.GetActionRawStrength("move_jump") 	- 	Input.GetActionRawStrength("move_crouch");

		ly = Input.GetActionRawStrength("look_up") 		- 	Input.GetActionRawStrength("look_down");
		lx = Input.GetActionRawStrength("look_right") 	- 	Input.GetActionRawStrength("look_left");

		if(Mathf.Abs(mx) > 0.2 || Mathf.Abs(my) > 0.2 || Mathf.Abs(mz) > 0.2){
			front_direction = findMoveVector(mx, my);
			LinearVelocity = (front_direction + mz*up_direction) * speed;
			//Adjust model orientation
			playAnimation("walk");
		}
		else {
			LinearVelocity = Vector3.Zero;
			pauseAnimation(2);
		}

		if(Input.IsActionJustPressed("move_jump")){
			if(!jumping){
				LinearVelocity += jump_speed * up_direction;
				//jumping = true;
			}
		}

		if(Input.IsActionJustPressed("debug_action_1")) {
			findClosestGround(50);
			debugging = !debugging;
		}
    }

    private Vector3 findMoveVector(float mx, float my) {
		Vector3 move_vector = mx*camera.Basis.X.Slide(up_direction).Normalized() - my*camera.Basis.Z.Slide(up_direction).Normalized();
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

		
		base._PhysicsProcess(delta);
    }
}