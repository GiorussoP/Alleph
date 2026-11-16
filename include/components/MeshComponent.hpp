#pragma once

#include "components/Component.hpp"

struct ModelData;

class MeshComponent : public Component {
public:
  MeshComponent(class Actor *owner, ModelData *model = nullptr,
                bool alwaysLoaded = true, double maxLoadDist = 0.0,
                int updateOrder = 100);
  ~MeshComponent() override;

  ModelData *model;
  double pos[3];
  float rot[4];
  double scale[3];
  float color[3];
  bool alwaysLoaded;
  double maxLoadDist;
};