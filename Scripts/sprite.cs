using Godot;
using System;

public abstract partial class SpriteEntity : Entity {
    [Export] protected Node3D camera;

    private int frame_width;
    private int frame_height;
    
    private string current_animation = "none";
    bool paused = false;

    private string[] directions = {"n","ne","e","se","s"};

    [Export] private Texture2D sprite_sheet;

    public AnimatedSprite3D animatedSprite3D;

    public SpriteEntity(int width = 64, int height = 80) {
        frame_width = width;
        frame_height = height;
    }

    public override void _Ready() {
        //camera = GetViewport().GetCamera3D();
        animatedSprite3D = GetNode<AnimatedSprite3D>("AnimatedSprite3D");
        animatedSprite3D.SpriteFrames = new SpriteFrames();


        addAnimationSet("walk",0,0,2);

        animatedSprite3D.TextureFilter = BaseMaterial3D.TextureFilterEnum.Nearest;
    }
    public override void _Process(double delta){

        animatedSprite3D.LookAt(camera.Position,up_direction);
    }

    public override void _PhysicsProcess(double delta) {
        Vector3 direction = front_direction.Slide(camera.Transform.Basis.Y);
        float angle = direction.SignedAngleTo(-camera.Transform.Basis.Z,up_direction);
        Vector3 scale_vector = angle <= 0 ? new Vector3(1,1,1) : new Vector3(-1,1,1);
        angle = Math.Abs(angle);
        string dir_name = "";
        if(angle < Math.PI/6)
            dir_name = directions[0];
        else if(angle < Math.PI/3)
            dir_name = directions[1];
        else if(angle < 2*Math.PI/3)
            dir_name = directions[2];
        else if(angle < 5*Math.PI/6)
            dir_name = directions[3];
        else
            dir_name = directions[4];
        


        var frame = animatedSprite3D.Frame;
        var progress = animatedSprite3D.FrameProgress;
        animatedSprite3D.Play(current_animation+"_" + dir_name);
        animatedSprite3D.SetFrameAndProgress(frame,progress);
        animatedSprite3D.Scale = scale_vector;

        if(paused)  animatedSprite3D.Pause();

        base._PhysicsProcess(delta);
    }

    // Adds an animation for all 5 directions.
    protected void addAnimationSet(string name, int row = 0,int collumn = 0, int n_frames = 8, int fps = 10){
       

        for(int j = 0; j < 5; ++j){

             GD.Print("Adding animation "+name+"_"+directions[j]);
            animatedSprite3D.SpriteFrames.AddAnimation(name+"_"+directions[j]);
            animatedSprite3D.SpriteFrames.SetAnimationSpeed(name+"_"+directions[j],fps);

            for(int i = 0; i < n_frames; ++i){
                Rect2 region = new Rect2(collumn * frame_width + j*n_frames*frame_width + i*frame_width, row * frame_height,frame_width,frame_height);

                var atlas_texture = new AtlasTexture();
                atlas_texture.Atlas = sprite_sheet;
                atlas_texture.Region = region;

                animatedSprite3D.SpriteFrames.AddFrame(name+"_"+directions[j], atlas_texture);
            }
        }
    }
    
    protected void playAnimation(string name, bool reset = false){

        current_animation = name;
        paused = false;
    }
    protected void pauseAnimation(){
        paused = true;
    }
    
}
