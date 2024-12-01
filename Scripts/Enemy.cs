using Godot;
using System;


public partial class Enemy : SpriteEntity{


    public Enemy(int width, int height): base(width,height){
        
    }
    [Export] protected int health;
    [Export] protected int damage; 
}


