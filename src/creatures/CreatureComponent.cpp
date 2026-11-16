#include "creatures/CreatureComponent.hpp"

#include "actors/Actor.hpp"

#include <algorithm>

namespace life {

namespace {
// Picks one of a limb's 6 local faces (±X/±Y/±Z) as an attachment point,
// and returns two axes perpendicular to that face's normal to choose a
// hinge axis from (a hinge axis parallel to the face normal would let a
// limb spin in place around its own attachment rather than swing).
void pickFaceAndHingeAxes(uint32_t &rngState, vec3 parentExtents,
                          vec3 &outAnchor, vec3 outHingeChoices[2]) {
  int axis = std::min((int)(randf01(rngState) * 3.0f), 2);
  float sign = randf01(rngState) < 0.5f ? -1.0f : 1.0f;
  float extentOnAxis =
      axis == 0 ? parentExtents.x : axis == 1 ? parentExtents.y : parentExtents.z;

  vec3 faceNormal(0.0f, 0.0f, 0.0f);
  if (axis == 0)
    faceNormal.x = sign;
  else if (axis == 1)
    faceNormal.y = sign;
  else
    faceNormal.z = sign;
  outAnchor = faceNormal * extentOnAxis;

  if (axis == 0) {
    outHingeChoices[0] = vec3(0.0f, 1.0f, 0.0f);
    outHingeChoices[1] = vec3(0.0f, 0.0f, 1.0f);
  } else if (axis == 1) {
    outHingeChoices[0] = vec3(1.0f, 0.0f, 0.0f);
    outHingeChoices[1] = vec3(0.0f, 0.0f, 1.0f);
  } else {
    outHingeChoices[0] = vec3(1.0f, 0.0f, 0.0f);
    outHingeChoices[1] = vec3(0.0f, 1.0f, 0.0f);
  }
}

LimbDNA makeRandomLimb(uint32_t &rngState, int parentIndex,
                       vec3 parentExtents) {
  LimbDNA limb;
  limb.parentIndex = parentIndex;
  limb.extents = vec3(randfRange(rngState, 0.08f, 0.16f),
                      randfRange(rngState, 0.08f, 0.16f),
                      randfRange(rngState, 0.2f, 0.5f));

  vec3 hingeChoices[2];
  pickFaceAndHingeAxes(rngState, parentExtents, limb.parentAnchorLocal,
                       hingeChoices);
  limb.childAnchorLocal = vec3(0.0f, 0.0f, -1.0f) * limb.extents.z;
  limb.jointAxisLocal = hingeChoices[randf01(rngState) < 0.5f ? 0 : 1];
  limb.jointLimit = randfRange(rngState, 0.5f, 1.2f);
  return limb;
}
} // namespace

CreatureDNA makeDefaultCreatureDNA(uint32_t &rngState) {
  CreatureDNA dna;

  LimbDNA root;
  root.parentIndex = -1;
  root.extents = vec3(randfRange(rngState, 0.3f, 0.5f),
                      randfRange(rngState, 0.22f, 0.4f),
                      randfRange(rngState, 0.35f, 0.6f));
  dna.limbs.push_back(root);

  int limbCount = 2 + (int)(randf01(rngState) * 3.0f); // 2..4 starter limbs
  for (int i = 0; i < limbCount; i++)
    dna.limbs.push_back(makeRandomLimb(rngState, /*parentIndex=*/0, root.extents));

  dna.colorTint = vec3(randfRange(rngState, 0.2f, 0.7f),
                       randfRange(rngState, 0.5f, 0.9f),
                       randfRange(rngState, 0.15f, 0.5f));
  // Bootstrap the population roughly half animal / half plant; selection
  // pressure (predation vulnerability vs. metabolic cost) settles the
  // real equilibrium from here.
  dna.isMobile = randf01(rngState) < 0.5f;
  return dna;
}

DNAMutationResult mutateCreatureDNA(const CreatureDNA &parent,
                                    uint32_t &rngState) {
  DNAMutationResult result;
  result.dna = parent;
  result.sourceLimbIndex.resize(parent.limbs.size());
  for (size_t i = 0; i < parent.limbs.size(); i++)
    result.sourceLimbIndex[i] = (int)i;

  // 20% chance: scale an existing limb's extents on one axis (Step 5.4,
  // thickness/sharpness evolution). Index-preserving, so it doesn't
  // touch sourceLimbIndex.
  if (!result.dna.limbs.empty() && randf01(rngState) < kMutateScaleChance) {
    int idx = std::min((int)(randf01(rngState) * result.dna.limbs.size()),
                       (int)result.dna.limbs.size() - 1);
    int axis = std::min((int)(randf01(rngState) * 3.0f), 2);
    float factor = randfRange(rngState, 0.85f, 1.18f);
    float *comp = axis == 0   ? &result.dna.limbs[idx].extents.x
                 : axis == 1 ? &result.dna.limbs[idx].extents.y
                             : &result.dna.limbs[idx].extents.z;
    *comp = std::clamp(*comp * factor, kMinOBBExtent, kMaxOBBExtent);
  }

  // 5% chance: delete a leaf limb (Step 5.4). Never the root, and only
  // limbs with no children of their own, so no other limb is left
  // pointing at a parentIndex that no longer exists.
  if (result.dna.limbs.size() > 1 &&
      randf01(rngState) < kMutateDeleteLimbChance) {
    std::vector<int> leaves;
    for (size_t i = 1; i < result.dna.limbs.size(); i++) {
      bool hasChild = false;
      for (const auto &l : result.dna.limbs)
        if (l.parentIndex == (int)i) {
          hasChild = true;
          break;
        }
      if (!hasChild)
        leaves.push_back((int)i);
    }
    if (!leaves.empty()) {
      int which =
          leaves[(size_t)(randf01(rngState) * leaves.size()) % leaves.size()];
      result.dna.limbs.erase(result.dna.limbs.begin() + which);
      result.sourceLimbIndex.erase(result.sourceLimbIndex.begin() + which);
      for (auto &l : result.dna.limbs)
        if (l.parentIndex > which)
          l.parentIndex--;
    }
  }

  // 5% chance: add a new limb hanging off a random existing limb (Step
  // 5.4), capped so morphologies can't grow unboundedly.
  if ((int)result.dna.limbs.size() < kMaxLimbsPerCreature &&
      randf01(rngState) < kMutateAddLimbChance) {
    int parentIdx =
        std::min((int)(randf01(rngState) * result.dna.limbs.size()),
                 (int)result.dna.limbs.size() - 1);
    result.dna.limbs.push_back(makeRandomLimb(
        rngState, parentIdx, result.dna.limbs[parentIdx].extents));
    result.sourceLimbIndex.push_back(-1); // brand new, no parent brain to inherit
  }

  // Small cosmetic drift so lineages become visually distinguishable.
  result.dna.colorTint.x = std::clamp(
      result.dna.colorTint.x + randfRange(rngState, -0.03f, 0.03f), 0.05f, 1.0f);
  result.dna.colorTint.y = std::clamp(
      result.dna.colorTint.y + randfRange(rngState, -0.03f, 0.03f), 0.05f, 1.0f);
  result.dna.colorTint.z = std::clamp(
      result.dna.colorTint.z + randfRange(rngState, -0.03f, 0.03f), 0.05f, 1.0f);

  // Speciation: small chance to flip the animal/plant gene.
  if (randf01(rngState) < kMutateFlipMobilityChance)
    result.dna.isMobile = !result.dna.isMobile;

  return result;
}

LimbComponent::LimbComponent(Actor *owner, Actor *rootActor, int limbIndex)
    : Component(owner, /*updateOrder=*/50), rootActor(rootActor),
      limbIndex(limbIndex) {}

LimbComponent::~LimbComponent() = default;

CreatureComponent::CreatureComponent(Actor *owner, CreatureRecord *record)
    : Component(owner, /*updateOrder=*/50), record(record) {}

CreatureComponent::~CreatureComponent() = default;

} // namespace life
