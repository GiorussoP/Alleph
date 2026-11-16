#pragma once

#include "glsl_compat.h"

#include <vector>

class Game;
class Component;
class ColliderComponent;

enum class ActorState { Active, Paused, Destroy };

class Actor {
public:
  Actor(Game *game);
  virtual ~Actor();

  void Update(float deltaTime);
  void ProcessInput();

  const vec3 &GetPosition() const { return mPosition; }
  virtual void SetPosition(const vec3 pos);

  vec3 GetScale() const { return mScale; }
  void SetScale(const vec3 scale) { mScale = scale; }

  vec4 GetRotation() const { return mRotation; }
  void SetRotation(const vec4 rotation) { mRotation = rotation; }

  ActorState GetState() const { return mState; }
  void SetState(ActorState state) { mState = state; }

  Game *GetGame() { return mGame; }

  const std::vector<Component *> &GetComponents() const { return mComponents; }

  virtual void OnCollision(vec3 penetration, ColliderComponent *other) {}

  template <typename T> T *GetComponent() const {
    for (auto c : mComponents) {
      if (auto typed = dynamic_cast<T *>(c)) {
        return typed;
      }
    }
    return nullptr;
  }

protected:
  virtual void OnUpdate(float deltaTime) {}
  virtual void OnProcessInput() {}

  Game *mGame;
  ActorState mState;
  vec3 mPosition;
  vec3 mScale;
  vec4 mRotation;

  std::vector<Component *> mComponents;

private:
  friend class Component;
  void AddComponent(Component *component);
};