#pragma once

class Actor;
class Renderer;
class Game;

class Component {
public:
  Component(Actor *owner, int updateOrder = 100);
  virtual ~Component();

  virtual void Update(float deltaTime) {}
  virtual void ProcessInput() {}
  virtual void DebugDraw(Renderer *renderer) {}

  int GetUpdateOrder() const { return mUpdateOrder; }
  Actor *GetOwner() const { return mOwner; }
  Game *GetGame() const;

  void SetEnabled(bool enabled) { mIsEnabled = enabled; }
  bool IsEnabled() const { return mIsEnabled; }

protected:
  Actor *mOwner;
  int mUpdateOrder;
  bool mIsEnabled;
};