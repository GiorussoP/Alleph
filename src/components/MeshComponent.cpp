#include "components/MeshComponent.hpp"

MeshComponent::MeshComponent(class Actor *owner, ModelData *model,
                             bool alwaysLoaded, double maxLoadDist,
                             int updateOrder)
    : Component(owner, updateOrder), model(model), alwaysLoaded(alwaysLoaded),
      maxLoadDist(maxLoadDist) {
  pos[0] = pos[1] = pos[2] = 0.0;
  rot[0] = rot[1] = rot[2] = 0.0f;
  rot[3] = 1.0f;
  scale[0] = scale[1] = scale[2] = 1.0;
  color[0] = color[1] = color[2] = 1.0f;
}

MeshComponent::~MeshComponent() = default;