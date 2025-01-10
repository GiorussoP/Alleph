using Godot;
using System;
using System.Diagnostics;
using System.Linq;




public partial class Player : SpriteEntity {
	
	[Export] float speed = 4;
	[Export] float sprint_boost = 4;
	[Export] float look_speed = 0.03f;
	[Export] float max_cam_dist = 20;
	[Export] float min_cam_dist = 0.1f;
	[Export] float jump_speed = 5;
	[Export] Vector3I home = new Vector3I(0,3,0);

	[Export] float power_gauge,power_gauge_limit = 10;

	private OmniLight3D light;

	/// <summary>
	/// /DEBUG
	bool debugging = false;
	/// </summary>

	private bool jumping = false, sprinting = false, falling = false, power = false;
	private float	power_delay = 1f/15f, power_timer = 0;
					

	private Vector3 move_vector = Vector3.Zero;

	private float camera_distance, desired_camera_distance;
	private Vector3 camera_target;

	private float mx,my, lx,ly, zy;

	// Called when the node enters the scene tree for the first time.
	public override void _Ready() {
		base._Ready();

		desired_camera_distance = (max_cam_dist+min_cam_dist)/2;
		camera_distance = desired_camera_distance;
		power_gauge = power_gauge_limit;
		Visible = true;

		light = GetNode<OmniLight3D>("OmniLight3D");

		addAnimationSet("walk",0,0,8);
		addAnimationSet("run",1,0,8);
		addAnimationSet("jump",2,0,7,10,false);

		playAnimation("walk");
		pauseAnimation(2);

		Position = home;
		camera.Position = Position;
	}

	// Called every frame. 'delta' is the elapsed time since the previous frame.
	public override void _Process(double delta){

		// POWER
		if(power){
			power_timer+=(float)delta;


			// PowerTick
			if(power_timer > power_delay){
				if(power_gauge > 0){
					findClosestGround(50.0f);
				}
				else{
					on_ground = false;
					setPower(false);
				}
				power_timer = 0;
			}

			// Gauge
			if(power_gauge > 0 && up_direction.AngleTo(Position) > Math.PI/4){
				// using
				power_gauge -=(float)delta;
				power_gauge = Math.Max(power_gauge,0);
				drawPowerGauge(Color.Color8(0,255,255));
			}
			else{
				// not using
				drawPowerGauge(Color.Color8(0,0,255));
			}
		}
		else{
			setPower(false);
		}
		

		// Gauge recharge
		if(!falling && power_gauge < power_gauge_limit && up_direction.AngleTo(Position) < Math.PI/4){
			power_gauge += (float)delta;
			power_gauge = Math.Min(power_gauge,power_gauge_limit);
			drawPowerGauge(Color.Color8(0,0,255));
		}

		//GD.Print("Power: ",power_gauge);


		// Camera Angle
		if(Mathf.Abs(ly)> 0.2 || Mathf.Abs(lx) > 0.2){ 
			camera.Rotate(-up_direction,lx*look_speed);
			camera.Rotate(camera.Transform.Basis.X,ly*look_speed);
		}
		camera.LookAt(camera.Position-camera.Basis.Z,camera.Basis.Y.Lerp(sprite_up.Slide(camera.Basis.Z),0.1f));

		// Camera zoom
		desired_camera_distance += zy;
		desired_camera_distance = Math.Min(max_cam_dist,Math.Max(min_cam_dist,desired_camera_distance));

		// Camera position
		if(true) {
			camera_target = Utilities.vector3Lerp(camera_target,Position+ (0.2f*camera_distance+0.4f)*up_direction,0.5f);
		}
		else {
			//camera_target += (Position-camera_target).Slide(up_direction);
		}
		camera.Position = Utilities.vector3Lerp(camera.Position, camera_target + camera_distance*camera.Transform.Basis.Z,0.5f);

		if(debugging){
			DebugDraw3D.DrawLine(Position,closest_ground,Color.Color8(255,100,100));
			DebugDraw3D.DrawLine(Position,Position + 3.0f* up_direction,Color.Color8(100,255,100));
			DebugDraw3D.DrawSphere(Position,0.5f,Color.Color8(100,100,100));
			DebugDraw3D.DrawArrow(Position,Position + 3.0f*front_direction,Color.Color8(255,255,0), 0.1f, true);
			//GD.Print(desired_camera_distance, camera_distance,camera);
		}

		base._Process(delta);
	}

	private void setPower(bool value = true){
		if(value == true){
			power = true;
			animatedSprite3D.Modulate = Color.Color8(0,255,255,170);
			animatedSprite3D.Shaded = false;
		}
		else{
			up_direction = Position.Normalized();
			power = false;
			resetSpriteColor();
		}
		light.Visible = value;
	}
	private void drawPowerGauge(Color c){
		Vector3 begin = Position +0.5f*up_direction + 0.5f*camera.Basis.X;
		Vector3 end = begin + (power_gauge/power_gauge_limit)*0.5f*camera.Basis.Y;

		DebugDraw3D.DrawLine(begin,end,c);
	}

    public override void _Input(InputEvent @event){

		if(Input.IsActionPressed("reset")) {
			Position = home;
			Velocity = Vector3.Zero;
			local_y_speed = 0;
		}
		my = Input.GetActionRawStrength("move_forward") - 	Input.GetActionRawStrength("move_backward");
		mx = Input.GetActionRawStrength("move_right") 	- 	Input.GetActionRawStrength("move_left");
		lx = Input.GetActionRawStrength("look_right") 	- 	Input.GetActionRawStrength("look_left");
		ly = Input.GetActionRawStrength("look_up") - Input.GetActionRawStrength("look_down");

		if(Input.IsActionPressed("modifier") && Mathf.Abs(ly) > 0.2) {
			zy = -ly;
			ly = 0;
		}
		else {
			zy = 0;
		};

		if(power_gauge > 0 && Input.GetActionRawStrength("power") > 0){
			setPower(true);
		}
		else {
			setPower(false);
		}	

		if((Mathf.Abs(mx) > 0.2 || Mathf.Abs(my) > 0.2)){
			move_vector = findMoveVector();
			// Animation
			front_direction = move_vector;
			if(on_ground) {
				if(sprinting)
					playAnimation("run");
				else
					playAnimation("walk");
			}
		}
		else {
			move_vector = Vector3.Zero;
			sprinting = false;
			if(on_ground){
				playAnimation("walk",2);
				pauseAnimation();
			}
		}

		if(desired_camera_distance == 0){
			front_direction = -camera.Basis.Z.Slide(up_direction);
			setTransparent();
		}
		else{
			setTransparent(false);
		}

		if(Input.IsActionPressed("move_jump")){
			if(!jumping && !falling){
				jumping = true;
				on_ground = false;
				local_y_speed = jump_speed;
				
				playAnimation("jump",0,2);
			}
		}
		if(Input.IsActionJustPressed("debug_action_1")) {
			findClosestGround(50);
			debugging = !debugging;
		}

		if(Input.IsActionPressed("move_sprint")) {
			sprinting = true;
		}
		base._Input(@event);
    }

    private Vector3 findMoveVector() {
		return (mx*camera.Basis.X.Slide(up_direction).Normalized() - my*camera.Basis.Z.Slide(up_direction).Normalized()).Normalized();
	}


	public override void _PhysicsProcess(double delta) {

		float sp = sprinting? speed + sprint_boost : speed;
		Velocity = move_vector * sp;

		if(!on_ground && !falling&&local_y_speed <-3){
			playAnimation("jump",2,4);
			falling = true;
		}
		else if(falling && on_ground){
			falling = false;
			jumping = false;
			if(move_vector == Vector3.Zero){
				playAnimation("jump",4,6);
			}
			else{
				playAnimation("run");
			}
		}
		else if (on_ground)
			jumping = false;
		

		// CAMERA

		

        var query = PhysicsRayQueryParameters3D.Create(camera_target,camera_target + max_cam_dist*camera.Basis.Z.Normalized(),Utilities.floor_object_mask);
        var result = GetWorld3D().DirectSpaceState.IntersectRay(query);

		float new_dist = desired_camera_distance;
		
		if (result.Count > 0){
			new_dist = ((Vector3) result["position"] - Position).Length() - 2.0f;
		}

		camera_distance = new_dist < desired_camera_distance ? new_dist : desired_camera_distance;

		base._PhysicsProcess(delta);

    }
}