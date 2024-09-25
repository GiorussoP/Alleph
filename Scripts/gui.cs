using Godot;
using System;

public partial class gui : Node
{

	private bool fullscreen = DisplayServer.WindowGetMode() == DisplayServer.WindowMode.Fullscreen;
	// Called when the node enters the scene tree for the first time.
	public override void _Ready()
	{
	}

	// Called every frame. 'delta' is the elapsed time since the previous frame.
	public override void _Process(double delta)
	{
	}

	public override void _Input(InputEvent @event){
		if(@event.IsActionPressed("switch_fullscreen")){
			if(!fullscreen){
				DisplayServer.WindowSetMode(DisplayServer.WindowMode.Fullscreen);
				fullscreen = true;
			}
			else {
				DisplayServer.WindowSetMode(DisplayServer.WindowMode.Windowed);
				fullscreen = false;
			}
		}
	}
}
