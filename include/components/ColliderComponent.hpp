#pragma once

#include "components/Component.hpp"
#include "glsl_compat.h"

enum class ColliderType { AABB, OBB, Sphere };
enum class ColliderLayer {
  Player,
  Ground,
  Hole,
  Entity,
  Enemy,
  Note,
  World,
  // Added for the life-simulation ecosystem: living creatures vs. their
  // ragdolled remains, so broad-phase passes can cheaply tell them apart
  // from generic World bodies without a component lookup.
  Creature,
  Corpse
};

class ColliderComponent : public Component {
public:
  ColliderComponent(Actor *owner, ColliderLayer layer,
                    ColliderType type = ColliderType::AABB,
                    bool isStatic = false, int updateOrder = 10);
  virtual ~ColliderComponent();

  virtual bool Intersect(const ColliderComponent &other) const = 0;
  virtual vec3 DetectCollision(const ColliderComponent &other) const = 0;
  virtual void DebugDraw(class Renderer *renderer) override = 0;

  ColliderLayer GetLayer() const { return mLayer; }
  void SetLayer(ColliderLayer layer) { mLayer = layer; }
  ColliderType GetType() const { return mType; }
  bool IsStatic() const { return mIsStatic; }
  void SetStatic(bool isStatic) { mIsStatic = isStatic; }

protected:
  ColliderLayer mLayer;
  ColliderType mType;
  bool mIsStatic;
};

class AABBCollider : public ColliderComponent {
public:
  AABBCollider(Actor *owner, ColliderLayer layer, vec3 offset, vec3 size,
               bool isStatic = true, int updateOrder = 10);
  ~AABBCollider() override;

  bool Intersect(const ColliderComponent &other) const override;
  vec3 DetectCollision(const ColliderComponent &other) const override;
  void DebugDraw(class Renderer *renderer) override;

  vec3 GetOffset() const { return mOffset; }
  vec3 GetSize() const { return mSize; }
  vec3 GetMin() const;
  vec3 GetMax() const;

private:
  vec3 mOffset;
  vec3 mSize;
};

class OBBCollider : public ColliderComponent {
public:
  OBBCollider(Actor *owner, ColliderLayer layer, vec3 offset, vec3 size,
              bool isStatic = false, int updateOrder = 10);
  ~OBBCollider() override;

  bool Intersect(const ColliderComponent &other) const override;
  vec3 DetectCollision(const ColliderComponent &other) const override;
  void DebugDraw(class Renderer *renderer) override;

  vec3 GetOffset() const { return mOffset; }
  vec3 GetSize() const { return mSize; }
  vec3 GetCenter() const;

private:
  vec3 mOffset;
  vec3 mSize;
};

class SphereCollider : public ColliderComponent {
public:
  SphereCollider(Actor *owner, ColliderLayer layer, vec3 offset, float radius,
                 bool isStatic = false, int updateOrder = 10);
  ~SphereCollider() override;

  bool Intersect(const ColliderComponent &other) const override;
  vec3 DetectCollision(const ColliderComponent &other) const override;
  void DebugDraw(class Renderer *renderer) override;

  vec3 GetOffset() const { return mOffset; }
  float GetRadius() const;
  vec3 GetCenter() const;

private:
  vec3 mOffset;
  float mRadius;
};