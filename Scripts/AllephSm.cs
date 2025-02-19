using Godot;
using System;

[Tool]
public partial class AllephSm : Node3D
{
	[Export] Node3D sun, moon, sunlight, moonlight;
	// Called when the node enters the scene tree for the first time.
	public override void _Ready() {
		sun.Position = new Vector3(15000, 0,0);
		moonlight.Position = Vector3.Zero;
		sunlight.Position = Vector3.Zero;
	}

	// Called every frame. 'delta' is the elapsed time since the previous frame.
	public override void _Process(double delta) {
		sun.Position = sun.Position.Rotated(Vector3.Up,(float)delta*0.002617994f);
		moon.Position = -sun.Position;

		moonlight.LookAt(sun.Position,Vector3.Up);
		sunlight.LookAt(moon.Position,Vector3.Up);
	}
}
