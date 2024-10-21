using Godot;
using System;





public partial class utilities {
    public static Vector3 vector3Lerp(Vector3 v1, Vector3 v2, float factor){
		return new Vector3(	Mathf.Lerp((float)v1.X,(float)v2.X,factor),
							Mathf.Lerp((float)v1.Y,(float)v2.Y,factor),
							Mathf.Lerp((float)v1.Z,(float)v2.Z,factor));
	}
}