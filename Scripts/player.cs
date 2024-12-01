using Godot;
using System;
using System.Diagnostics;
using System.Linq;




public partial class Player : SpriteEntity {
	
	[Export] float speed = 4;
	[Export] float sprint_boost = 4;
	[Export] float look_speed = 0.03f;
	[Export] float max_cam_dist = 20;
	[Export] float min_cam_dist = 3;
	[Export] float jump_speed = 5;

	[Export] float power_gauge = 5,power_gauge_limit = 5;

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

	public Player() {
		
		front_direction = -Basis.Z;

		desired_camera_distance = min_cam_dist/2;
		camera_distance = desired_camera_distance;
	}

	// Called when the node enters the scene tree for the first time.
	public override void _Ready() {
		base._Ready();
		addAnimationSet("walk",0,0,8);
		addAnimationSet("run",1,0,8);
		addAnimationSet("jump",2,0,7,10,false);

		playAnimation("walk");
		pauseAnimation(2);
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
					enablePower(false);
				}
				power_timer = 0;
			}

			// Gauge
			if(power_gauge > 0 && up_direction.AngleTo(Vector3.Up) > Math.PI/4){
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
			enablePower(false);
		}
		

		// Gauge recharge
		if(on_ground && power_gauge < power_gauge_limit && up_direction.AngleTo(Vector3.Up) < Math.PI/4){
			power_gauge += (float)delta;
			power_gauge = Math.Min(power_gauge,power_gauge_limit);
			drawPowerGauge(Color.Color8(0,0,255));
		}

		GD.Print("Power: ",power_gauge);


		// Camera Angle
		if(Mathf.Abs(ly)> 0.2 || Mathf.Abs(lx) > 0.2){ 
			camera.Rotate(-up_direction,lx*look_speed);
			camera.Rotate(camera.Transform.Basis.X,ly*look_speed);
		}
		camera.LookAt(camera.Position-camera.Basis.Z,utilities.vector3Lerp(camera.Basis.Y,up_direction.Slide(camera.Basis.Z),0.1f));

		// Camera zoom
		desired_camera_distance += zy;
		desired_camera_distance = Math.Min(max_cam_dist,Math.Max(min_cam_dist,desired_camera_distance));

		// Camera position
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
			DebugDraw3D.DrawSphere(Position,0.5f,Color.Color8(100,100,100));
			DebugDraw3D.DrawArrow(Position,Position + 3.0f*front_direction,Color.Color8(255,255,0), 0.1f, true);
		}

		base._Process(delta);
	}


	private void drawPowerGauge(Color c){
		Vector3 begin = Position +0.5f*up_direction + 0.5f*camera.Basis.X;
		Vector3 end = begin + (power_gauge/power_gauge_limit)*0.5f*camera.Basis.Y;

		DebugDraw3D.DrawLine(begin,end,c);
	}

	private void enablePower(bool value = true){
		if(value == true){
			power = true;

			animatedSprite3D.Modulate = Color.Color8(0,255,255,170);
			animatedSprite3D.TextureFilter = BaseMaterial3D.TextureFilterEnum.LinearWithMipmapsAnisotropic;
			animatedSprite3D.Shaded = false;
		}
		else{
			// Criar método para desabilitar poder
			up_direction = Vector3.Up;
			power = false;
			animatedSprite3D.Shaded = true;
			animatedSprite3D.TextureFilter = BaseMaterial3D.TextureFilterEnum.Nearest;
			resetSpriteColor();
		}
	}
    public override void _Input(InputEvent @event){

		if(Input.IsActionPressed("reset")) {
			Position = closest_ground;
			Velocity = Vector3.Zero;
			local_y_speed = 0;
		}
		my = Input.GetActionRawStrength("move_forward") - 	Input.GetActionRawStrength("move_backward");
		mx = Input.GetActionRawStrength("move_right") 	- 	Input.GetActionRawStrength("move_left");
		lx = Input.GetActionRawStrength("look_right") 	- 	Input.GetActionRawStrength("look_left");

		if(Input.IsActionPressed("modifier")) {
			zy = Input.GetActionRawStrength("look_down") - Input.GetActionRawStrength("look_up");
			ly = 0;
		}
		else {
			ly = Input.GetActionRawStrength("look_up") - Input.GetActionRawStrength("look_down");
			zy = 0;
		};

		if(Input.GetActionRawStrength("power") > 0){
			enablePower();
			power = true;
		}
		else {
			enablePower(false);
			power = false;
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

		if(Input.IsActionPressed("move_jump")){
			if(!jumping && on_ground){
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
    }

    private Vector3 findMoveVector() {
		return (mx*camera.Basis.X.Slide(up_direction).Normalized() - my*camera.Basis.Z.Slide(up_direction).Normalized()).Normalized();
	}


	public override void _PhysicsProcess(double delta) {

		float sp = sprinting? speed + sprint_boost : speed;
		Velocity = move_vector * sp;

		if(!on_ground && !falling&&local_y_speed <0){
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

        var query = PhysicsRayQueryParameters3D.Create(Position,Position + max_cam_dist*camera.Basis.Z.Normalized(),utilities.floor_object_mask);
        var result = GetWorld3D().DirectSpaceState.IntersectRay(query);

		float new_dist = desired_camera_distance;
		
		if (result.Count > 0){
			new_dist = ((Vector3) result["position"] - Position).Length() - camera.Scale.X;
		}

		camera_distance = new_dist < desired_camera_distance ? new_dist : desired_camera_distance;

		base._PhysicsProcess(delta);

    }
}