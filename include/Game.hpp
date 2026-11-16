#pragma once

#include <SDL3/SDL.h>
#include <string>
#include <unordered_set>
#include <vector>

class Actor;
class Camera;
class Renderer;
class Scene;

class Game {
public:
  Game();

  bool Initialize();
  void RunLoop();
  void Shutdown();
  void Quit() { mIsRunning = false; }

  void LoadScene(Scene *scene);
  void UpdateActors(float deltaTime);
  void FindActiveActors();
  void AddActor(Actor *actor);
  void RemoveActor(Actor *actor);
  void AddAlwaysActive(Actor *actor);
  void RemoveAlwaysActive(Actor *actor);

  Renderer *GetRenderer() { return mRenderer; }
  Camera *GetCamera() const { return mCamera; }
  bool IsDebugging() const { return mIsDebugging; }
  Uint32 GetTicksCount() const { return mTicksCount; }
  Scene *GetCurrentScene() { return mCurrentScene; }

  bool IsPaused() const { return mIsPaused; }
  void SetPaused(bool paused) { mIsPaused = paused; }

private:
  void ProcessInput();
  void UpdateGame(float deltaTime);
  void GenerateOutput();
  void CheckCollisions();

  std::vector<Actor *> mActors;
  std::vector<Actor *> mPendingActors;
  std::unordered_set<Actor *> mAlwaysActiveActors;
  std::vector<Actor *> mActiveActors;

  Renderer *mRenderer;
  Camera *mCamera;
  Scene *mCurrentScene;
  Scene *mPendingScene;

  Uint32 mTicksCount;
  bool mIsRunning;
  bool mIsDebugging;
  bool mIsPaused;
  bool mUpdatingActors;
};