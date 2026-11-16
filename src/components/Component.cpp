#include "components/Component.hpp"

#include "Game.hpp"
#include "actors/Actor.hpp"

Component::Component(Actor *owner, int updateOrder)
    : mOwner(owner), mUpdateOrder(updateOrder), mIsEnabled(true) {
  mOwner->AddComponent(this);
}

Component::~Component() = default;

Game *Component::GetGame() const { return mOwner->GetGame(); }