#include "components/ColliderComponent.hpp"

#include "actors/Actor.hpp"

ColliderComponent::ColliderComponent(Actor *owner, ColliderLayer layer,
                                     ColliderType type, bool isStatic,
                                     int updateOrder)
    : Component(owner, updateOrder), mLayer(layer), mType(type),
      mIsStatic(isStatic) {}

ColliderComponent::~ColliderComponent() = default;

AABBCollider::AABBCollider(Actor *owner, ColliderLayer layer, vec3 offset,
                           vec3 size, bool isStatic, int updateOrder)
    : ColliderComponent(owner, layer, ColliderType::AABB, isStatic,
                        updateOrder),
      mOffset(offset), mSize(size) {}

AABBCollider::~AABBCollider() = default;

bool AABBCollider::Intersect(const ColliderComponent &other) const {
  return false;
}
vec3 AABBCollider::DetectCollision(const ColliderComponent &other) const {
  return vec3(0.0f, 0.0f, 0.0f);
}
void AABBCollider::DebugDraw(class Renderer *renderer) {}
vec3 AABBCollider::GetMin() const {
  return GetOwner()->GetPosition() + mOffset - mSize;
}
vec3 AABBCollider::GetMax() const {
  return GetOwner()->GetPosition() + mOffset + mSize;
}

OBBCollider::OBBCollider(Actor *owner, ColliderLayer layer, vec3 offset,
                         vec3 size, bool isStatic, int updateOrder)
    : ColliderComponent(owner, layer, ColliderType::OBB, isStatic, updateOrder),
      mOffset(offset), mSize(size) {}

OBBCollider::~OBBCollider() = default;
bool OBBCollider::Intersect(const ColliderComponent &other) const {
  return false;
}
vec3 OBBCollider::DetectCollision(const ColliderComponent &other) const {
  return vec3(0.0f, 0.0f, 0.0f);
}
void OBBCollider::DebugDraw(class Renderer *renderer) {}
vec3 OBBCollider::GetCenter() const {
  return GetOwner()->GetPosition() + mOffset;
}

SphereCollider::SphereCollider(Actor *owner, ColliderLayer layer, vec3 offset,
                               float radius, bool isStatic, int updateOrder)
    : ColliderComponent(owner, layer, ColliderType::Sphere, isStatic,
                        updateOrder),
      mOffset(offset), mRadius(radius) {}

SphereCollider::~SphereCollider() = default;
bool SphereCollider::Intersect(const ColliderComponent &other) const {
  return false;
}
vec3 SphereCollider::DetectCollision(const ColliderComponent &other) const {
  return vec3(0.0f, 0.0f, 0.0f);
}
void SphereCollider::DebugDraw(class Renderer *renderer) {}
float SphereCollider::GetRadius() const { return mRadius; }
vec3 SphereCollider::GetCenter() const {
  return GetOwner()->GetPosition() + mOffset;
}