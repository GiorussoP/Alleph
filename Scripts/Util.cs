using Godot;
using System;
using System.Reflection.Metadata;





public static class Util {
	public enum collision_layers : uint {
		Player = 1,
		Object = 2,
		Floor = 4,
		Entity = 8,
		Raycast = 16
	}
	public static float lerp(float firstFloat, float secondFloat, float by) {
     	return firstFloat * (1 - by) + secondFloat * by;
	}
	public const int floor_object_mask = 0b110;

	public static Vector3 vector3MaxLength(Vector3 v1, Vector3 v2){
		return (v1.Length() > v2.Length()) ? v1 : v2;
	}
	public static Vector3 vector3MinLength(Vector3 v1, Vector3 v2){
		return (v1.Length() < v2.Length()) ? v1 : v2;
	}
	public static float blendFunction(float f, float g, float transition_point,float transition_steepness,float x){
		float s = getSigmoidValue(transition_point,transition_steepness,x);
		return s*f + (1-s)*g;
	}
	public static float getSigmoidValue(float transition_point,float transition_steepness,float x){
		return 1/(1+MathF.Exp(transition_steepness*(x-transition_point)));
	}
}