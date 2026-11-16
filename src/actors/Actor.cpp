#include "actors/Actor.hpp"

#include "Game.hpp"
#include "components/Component.hpp"

#include <algorithm>

Actor::Actor(Game *game)
    : mGame(game), mState(ActorState::Active), mPosition(0.0f, 0.0f, 0.0f),
      mScale(1.0f, 1.0f, 1.0f), mRotation(0.0f, 0.0f, 0.0f, 1.0f) {
  if (mGame) {
    mGame->AddActor(this);
  }
}

Actor::~Actor() {
  if (mGame) {
    mGame->RemoveActor(this);
  }

  std::vector<Component *> componentsToDelete = mComponents;
  mComponents.clear();
  for (auto component : componentsToDelete) {
    delete component;
  }
}

void Actor::Update(float deltaTime) {
  if (mState == ActorState::Active) {
    for (auto component : mComponents) {
      if (component->IsEnabled()) {
        component->Update(deltaTime);
      }
    }
    OnUpdate(deltaTime);
  }
}

void Actor::ProcessInput() {
  if (mState == ActorState::Active) {
    for (auto component : mComponents) {
      if (component->IsEnabled()) {
        component->ProcessInput();
      }
    }
    OnProcessInput();
  }
}

void Actor::AddComponent(Component *component) {
  int order = component->GetUpdateOrder();
  auto iter = mComponents.begin();
  for (; iter != mComponents.end(); ++iter) {
    if (order < (*iter)->GetUpdateOrder()) {
      break;
    }
  }
  mComponents.insert(iter, component);
}

void Actor::SetPosition(const vec3 pos) { mPosition = pos; }