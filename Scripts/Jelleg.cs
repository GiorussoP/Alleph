using Godot;
using System;


public partial class Jelleg : Enemy{

	[Export] private bool test = false;

	public Jelleg(): base(32,32){}

    public override void _Ready() {
		base._Ready();
		addAnimationSet("idle",0,0,2,2);
		playAnimation("idle");

		GD.Print("JELLEG READY");
	}

}



