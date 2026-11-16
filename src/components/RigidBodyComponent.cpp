#include "components/RigidBodyComponent.hpp"

#include "actors/Actor.hpp"

RigidBodyComponent::RigidBodyComponent(class Actor *owner, float mass,
                                       float friction, bool applyGravity,
                                       int updateOrder)
    : Component(owner, updateOrder), mApplyGravity(applyGravity), mMass(mass),
      mFriction(friction), mVelocity(0.0f, 0.0f, 0.0f),
      mAcceleration(0.0f, 0.0f, 0.0f) {
  pos[0] = pos[1] = pos[2] = 0.0f;
  type = 0.0f;
  extents[0] = extents[1] = extents[2] = 1.0f;
  mass = mMass;
  vel[0] = vel[1] = vel[2] = 0.0f;
  restitution = 0.5f;
  rot[0] = rot[1] = rot[2] = 0.0f;
  rot[3] = 1.0f;
  ang_vel[0] = ang_vel[1] = ang_vel[2] = 0.0f;
  pad = 0.0f;
}

RigidBodyComponent::~RigidBodyComponent() = default;

void RigidBodyComponent::Update(float deltaTime) { (void)deltaTime; }

void RigidBodyComponent::ApplyForce(const vec3 &force) {
  if (mMass > 0.0f) {
    mAcceleration += force / mMass;
  }
}

void RigidBodyComponent::ApplyImpulse(const vec3 &impulse) {
  if (mMass > 0.0f) {
    mVelocity += impulse / mMass;
    vel[0] = mVelocity.x;
    vel[1] = mVelocity.y;
    vel[2] = mVelocity.z;
  }
}