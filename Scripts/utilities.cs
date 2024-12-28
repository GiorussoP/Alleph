using Godot;
using System;
using System.Reflection.Metadata;





public static class Utilities {


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
    public static Vector3 vector3Lerp(Vector3 v1, Vector3 v2, float factor){
		return new Vector3(	Mathf.Lerp((float)v1.X,(float)v2.X,factor),
							Mathf.Lerp((float)v1.Y,(float)v2.Y,factor),
							Mathf.Lerp((float)v1.Z,(float)v2.Z,factor));
	}
	public static Vector3 vector3Max(Vector3 v1, Vector3 v2){
		return (v1.Length() > v2.Length()) ? v1 : v2;
	}
	public static Vector3 vector3Min(Vector3 v1, Vector3 v2){
		return (v1.Length() < v2.Length()) ? v1 : v2;
	}
}