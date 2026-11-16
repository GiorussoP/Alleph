#pragma once

#include "components/Component.hpp"
#include "glsl_compat.h"

class RigidBodyComponent : public Component {
public:
  RigidBodyComponent(class Actor *owner, float mass = 1.0f,
                     float friction = 0.0f, bool applyGravity = false,
                     int updateOrder = 10);
  virtual ~RigidBodyComponent();

  void Update(float deltaTime) override;

  void ApplyForce(const vec3 &force);
  void ApplyImpulse(const vec3 &impulse);

  void SetMass(float mass) { mMass = mass; }
  float GetMass() const { return mMass; }

  vec3 GetVelocity() const { return mVelocity; }
  void SetVelocity(const vec3 &velocity) { mVelocity = velocity; }

  bool mApplyGravity;
  float mMass;
  float mFriction;
  vec3 mVelocity;
  vec3 mAcceleration;

  int lastPortalIndex = -1;

  float pos[3];

  float type;
  float extents[3];
  float mass;
  float vel[3];
  float restitution;
  float rot[4];
  float ang_vel[3];
  float pad;
};