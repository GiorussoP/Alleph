using Godot;
using System;
using System.Diagnostics;
using System.Linq;




public partial class player : SpriteEntity {
	
	[Export] float speed = 20;
	[Export] float look_speed = 0.03f;
	[Export] float max_cam_dist = 20;
	[Export] float min_cam_dist = 2;
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
		addAnimationSet("walk",0,0,8);
		addAnimationSet("run",1,0,8);
		addAnimationSet("jump",2,0,7,7,false);

		playAnimation("walk");
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
		if(true) {
			camera_target = utilities.vector3Lerp(camera_target,Position + 0.2f*camera_distance*up_direction,0.5f);
		}
		else {
			camera_target += (Position-camera_target).Slide(up_direction);
		}
		camera.Position = utilities.vector3Lerp(camera.Position, camera_target + camera_distance*camera.Transform.Basis.Z,0.5f);

	if(debugging){
		DebugDraw3D.DrawLine(Position,closest_ground,Color.Color8(255,100,100));
		DebugDraw3D.DrawLine(Position,Position + 3.0f* up_direction,Color.Color8(100,255,100));
		DebugDraw3D.DrawArrow(Position,Position + 3.0f*front_direction,Color.Color8(255,255,0), 0.1f, true);
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

		float zy = Input.GetActionRawStrength("zoom_forward") - 	Input.GetActionRawStrength("zoom_backward");
		camera_distance += zy;
		camera_distance = Math.Min(max_cam_dist,Math.Max(min_cam_dist,camera_distance));

		if(Mathf.Abs(mx) > 0.2 || Mathf.Abs(my) > 0.2 || Mathf.Abs(mz) > 0.2){
			Vector3 mv = findMoveVector(mx, my);
			if(mv != Vector3.Zero) front_direction = mv;
			LinearVelocity = (front_direction + mz*up_direction) * speed;
			//Adjust model orientation
			if(!jumping) playAnimation("run");
		}
		else {
			LinearVelocity = Vector3.Zero;
			
			if(!jumping){
				playAnimation("walk");
				pauseAnimation(2);
			}
		}

		if(Input.IsActionJustPressed("move_jump")){
			if(!jumping){
				LinearVelocity += jump_speed * up_direction;
				playAnimation("jump");
				jumping = true;
			}
		}
		if(Input.IsActionJustPressed("move_crouch")){
			jumping = false;
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

		float new_dist = camera_distance;
		
		if (result.Count > 0){
			GD.Print("Hit at point: ", result["position"]);
			new_dist = ((Vector3) result["position"] - Position).Length() - camera.Scale.X;

		}

		camera_distance = new_dist < camera_distance ? new_dist : camera_distance;

		
		base._PhysicsProcess(delta);
    }
}