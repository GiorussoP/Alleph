using Godot;
using System;
using System.Reflection;


public partial class Entity : CharacterBody3D{
    protected Godot.Vector3 closest_ground;

    private PhysicsDirectBodyState3D last_state;
    protected bool on_ground = true;

    protected Vector3 up_direction;

    protected float local_y_speed = 0;
    protected Vector3 front_direction;

    public Entity() {
        closest_ground = this.GetPosition();
        front_direction = Vector3.Forward;
        up_direction = Vector3.Up;
    }

    public override void _Ready() {
	}
    public override void _PhysicsProcess(double delta) {

        local_y_speed -= Environment.gravity_acceleration * (float)delta;
        if(local_y_speed< -Environment.terminal_speed)
            local_y_speed = -Environment.terminal_speed;

        Velocity += up_direction * local_y_speed;

        on_ground = false;
        if(MoveAndSlide()){
            for(int i = 0; i < GetSlideCollisionCount(); ++i){
                var collision = GetSlideCollision(i);

                //DebugDraw3D.DrawLine(Position,collision.GetPosition(),Color.Color8(0,255,255));


                if(!on_ground && collision.GetNormal().AngleTo(up_direction) < MathF.PI/4){
                    on_ground = true;
                    closest_ground = collision.GetPosition();
                    local_y_speed = 0;
                }
            }
        }
       

        //GD.Print("Collisions = ",GetSlideCollisionCount()," LOCAL YSPEED =",local_y_speed," ON_GROUND =",on_ground);

        base._PhysicsProcess(delta);
    }

    protected Vector3 findClosestGround(float radius, short precision = 4){
        var spaceState = GetWorld3D().DirectSpaceState;

        Vector3 search_position = Position;
        Vector3 search_direction = Vector3.Zero;

        float min_dist = radius;

        Godot.Collections.Dictionary result;

        while (precision > 0){
            for(short i = -1; i <= 1; ++i){
                for(short j = -1; j <= 1; ++j){
                    for(short k = -1; k <= 1; ++k){

                        if(i==0 && j == 0 && k == 0)
                            continue;
                  
                        Vector3 dir = new Vector3(i,j,k);

                        if(dir.Dot(search_direction) < 0)
                            continue;

                        var query = PhysicsRayQueryParameters3D.Create(search_position, search_position + dir.Normalized() * min_dist,Utilities.floor_object_mask);
                        result = spaceState.IntersectRay(query);

                        if(result.Count > 0){

                            Vector3 vec = (Vector3) result["position"] - search_position;
                            float dist = vec.Length();

                            if(dist < min_dist){
                                min_dist = dist;
                                closest_ground = (Vector3)result["position"];                            
                            }
                        }
                    }
                }
            }
            //DebugDraw3D.DrawArrow(search_position,closest_ground,Color.Color8(100,100,255),0.2f,true,3.0f);
            --precision;
            if(min_dist == radius || precision <= 0)
                break;
                
            search_direction = (closest_ground - Position).Normalized();
            search_position =  0.5f * (search_position + closest_ground);
        }
        up_direction = (Position-closest_ground).Normalized();
        return closest_ground;
    }
}


