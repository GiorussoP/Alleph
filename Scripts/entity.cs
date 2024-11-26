using Godot;
using System;



public partial class Entity : RigidBody3D{
    protected Vector3 closest_ground;
    protected Vector3 up_direction;
    protected Vector3 front_direction;

    public Entity(){
        closest_ground = Position;
        front_direction = Vector3.Forward;
        up_direction = Vector3.Up;
    }

    public override void _Ready() {
		
	}

    protected Vector3 findClosestGround(float radius, short precision = 4){
        var spaceState = GetWorld3D().DirectSpaceState;

        Vector3 search_position = Position;
        Vector3 search_direction = Vector3.Zero;

        float min_dist = radius;

        while (precision > 0){
            for(short i = -1; i <= 1; ++i){
                for(short j = -1; j <= 1; ++j){
                    for(short k = -1; k <= 1; ++k){

                        if(i==0 && j == 0 && k == 0)
                            continue;
                  
                        Vector3 dir = new Vector3(i,j,k);

                       

                        if(dir.Dot(search_direction) < 0)
                            continue;

                        var query = PhysicsRayQueryParameters3D.Create(search_position, search_position + dir.Normalized() * min_dist);
                        var result = spaceState.IntersectRay(query);

                        if(result.Count > 0){

                            Vector3 vec = (Vector3) result["position"] - search_position;
                            float dist = vec.Length();

                            if(dist < min_dist){
                                min_dist = dist;

                                closest_ground = (Vector3) result["position"];
                                up_direction = ((Vector3) result["normal"]).Normalized();
                            }
                        }
                    }
                }
            }
            DebugDraw3D.DrawArrow(search_position,closest_ground,Color.Color8(100,100,255),0.2f,true,3.0f);
            --precision;
            if(min_dist == radius || precision <= 0)
                break;
                
            search_direction = (closest_ground - Position).Normalized();
            search_position =  0.5f * (search_position + closest_ground);
        }
        return closest_ground;
    }
}


