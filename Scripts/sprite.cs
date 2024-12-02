using Godot;
using System;
using System.Collections.Generic;



public abstract partial class SpriteEntity : Entity {


    private struct AnimationExecution{
        public string name;
        public int frame_start;
        public int frame_end;
    };

    protected Node3D camera;

    [Export] private Texture2D sprite_sheet;

    private int frame_width;
    private int frame_height;

    private Queue<AnimationExecution> animation_queue = new Queue<AnimationExecution>();

    private bool playing_queue = false;

    private int end_frame = -1;
    
    private string current_animation = "none";
    bool paused = false;

    private Color original_modulate;

    private string[] directions = {"n","ne","e","se","s"};

    public AnimatedSprite3D animatedSprite3D;

    public SpriteEntity(int width = 64, int height = 80) {
        frame_width = width;
        frame_height = height;
    }

    public override void _Ready() {
        base._Ready();
        camera = GetViewport().GetCamera3D();
        animatedSprite3D = GetNode<AnimatedSprite3D>("AnimatedSprite3D");
        animatedSprite3D.SpriteFrames = new SpriteFrames();

        animatedSprite3D.TextureFilter = BaseMaterial3D.TextureFilterEnum.Nearest;
        animatedSprite3D.CastShadow = Godot.GeometryInstance3D.ShadowCastingSetting.DoubleSided;
        animatedSprite3D.AlphaCut = AnimatedSprite3D.AlphaCutMode.OpaquePrepass;
        animatedSprite3D.GIMode = AnimatedSprite3D.GIModeEnum.Dynamic;
        animatedSprite3D.Shaded = true;
        animatedSprite3D.DoubleSided = false;
        animatedSprite3D.NoDepthTest = false;   
        animatedSprite3D.Transparent = true;
        animatedSprite3D.VisibilityRangeEnd = 50.0f;

        original_modulate = animatedSprite3D.Modulate;

    }


    public override void _Input(InputEvent @event){
        base._Input(@event);  
    }

    protected void setTransparent(bool value = true){
        if(value){
            animatedSprite3D.CastShadow = GeometryInstance3D.ShadowCastingSetting.ShadowsOnly;
            animatedSprite3D.DoubleSided = true;
        }
        else{
            animatedSprite3D.CastShadow = GeometryInstance3D.ShadowCastingSetting.DoubleSided;
            animatedSprite3D.DoubleSided = false;
        }
    }

    public override void _Process(double delta){

       

        // Turn to camera
        animatedSprite3D.LookAt(Position-camera.Basis.Z.Slide(up_direction),up_direction);

        // Animation Finished
        if(animatedSprite3D.Frame == end_frame && animatedSprite3D.FrameProgress > 0.5){
            GD.Print("Animation ",current_animation," ended");
            pauseAnimation();

            if(playing_queue){
                if(animation_queue.Count !=0){
                    AnimationExecution animation = animation_queue.Dequeue();
                    playAnimation(animation.name,animation.frame_start,animation.frame_end);
                    GD.Print("DEQUEUED ",animation.name);
                }
                else playing_queue = false;
            }
        }
    }

    public override void _PhysicsProcess(double delta) {
        Vector3 direction = front_direction.Slide(camera.Transform.Basis.Y);
        float angle = direction.SignedAngleTo(-camera.Transform.Basis.Z,up_direction);
        Vector3 scale_vector = angle <= Math.PI/6 ? new Vector3(-1,1,1) : new Vector3(1,1,1);
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

        if(paused) 
            animatedSprite3D.Pause();

        base._PhysicsProcess(delta);
    }

    // Adds an animation for all 5 directions.
    protected void addAnimationSet(string name, int row = 0,int collumn = 0, int n_frames = 8, int fps = 10,bool repeat = true){
       
        for(int j = 0; j < 5; ++j){

            GD.Print("Adding animation set "+name+"_"+directions[j]);
            animatedSprite3D.SpriteFrames.AddAnimation(name+"_"+directions[j]);
            animatedSprite3D.SpriteFrames.SetAnimationSpeed(name+"_"+directions[j],fps);
            animatedSprite3D.SpriteFrames.SetAnimationLoop(name+"_"+directions[j],repeat);

            for(int i = 0; i < n_frames; ++i){
                Rect2 region = new Rect2(collumn * frame_width + j*n_frames*frame_width + i*frame_width, row * frame_height,frame_width,frame_height);

                var atlas_texture = new AtlasTexture();
                atlas_texture.Atlas = sprite_sheet;
                atlas_texture.Region = region;

                animatedSprite3D.SpriteFrames.AddFrame(name+"_"+directions[j], atlas_texture);
            }
        }
    }

    protected void queueAnimation(string name, int from_frame = -1,int to_frame = -1){
        AnimationExecution animation = new AnimationExecution();
        animation.name = name;
        animation.frame_start = from_frame;
        animation.frame_end = to_frame;
        animation_queue.Enqueue(animation);
    }

    protected void playQueuedAnimations(){
        playing_queue = true;
    }
 
    protected void playAnimation(string name, int from_frame = -1,int to_frame = -1){

        current_animation = name;
        paused = false;

        if(from_frame >= 0)
            animatedSprite3D.SetFrameAndProgress(from_frame,0);

        if(to_frame < 0){
            if(animatedSprite3D.SpriteFrames.GetAnimationLoop(name+"_"+directions[0]) == true){
                end_frame = -1;
            }
            else{
                end_frame = animatedSprite3D.SpriteFrames.GetFrameCount(name+"_"+directions[0])-1;
            }
        }
        else end_frame = to_frame;
    }

    public void resetSpriteColor(){
        animatedSprite3D.Modulate = original_modulate;
    }
    protected void pauseAnimation(int frame = -1){
        if(frame >= 0)
            animatedSprite3D.SetFrameAndProgress(frame,0);
        paused = true;
    }
}
