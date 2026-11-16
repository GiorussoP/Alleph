#pragma once

#include "components/Component.hpp"
#include "creatures/Brain.hpp"
#include "glsl_compat.h"

#include <cstdint>
#include <vector>

class Actor;
class RigidBodyComponent;
class OBBCollider;

namespace life {

// ---------------------------------------------------------------------
// Energy-economy tunables (Phase 1 of the plan). Grouped here so the
// ecosystem free functions in Game.cpp and the components below share
// one source of truth.
// ---------------------------------------------------------------------
constexpr float kBasalMetabolicConst = 1.1f;   // C_basal — was 0.06; grazing dwarfed this before, making idling free
constexpr float kMovementCostConst = 0.15f;    // scales root action -> E_move
constexpr float kGrassEnergyRate = 4.0f;       // C_grass
constexpr float kPredationAlpha = 0.04f;       // alpha
constexpr float kScavengeBeta = 2.0f;          // beta (per unit volume) — raised so a kill is clearly worth it

// Dense shaping reward for actually covering ground, on top of the
// sparse delta-energy signal (Step 4.1). Only ever added for
// dna.isMobile creatures — see kMutateFlipMobilityChance below — since
// it exists specifically to bootstrap locomotion before the discounted
// return (Brain.hpp's computeDiscountedReturns) has had time to learn,
// on its own, that movement can pay off several ticks later. Kept small
// relative to typical grazing income so it nudges rather than dominates
// the true energy objective.
constexpr float kMovementShapingWeight = 0.02f;

constexpr float kStartEnergy = 20.0f;
constexpr float kMaxEnergyForSensor = 100.0f; // just for input normalization
constexpr float kReproduceThreshold = 45.0f;
constexpr float kReproduceOffspringShare = 0.5f; // parent keeps the other half

constexpr int kPPOBufferMaxSize = 500;

constexpr float kVisionRange = 45.0f;
constexpr float kVisionCosHalfAngle = 0.35f; // ~70 deg half-angle cone
constexpr float kMaxForwardSpeed = 20.0f;
constexpr float kMaxAngularSpeed = 6.0f;
constexpr float kCPGFrequency = 1.1f; // Hz

constexpr float kMinOBBExtent = 0.15f;
constexpr float kMaxOBBExtent = 3.0f;

// --- Articulated-body / joint tunables (Phase 2) ---
constexpr int kMaxLimbsPerCreature = 8; // includes the root
constexpr float kJointKp = 10.0f;        // PD proportional gain, driving currentAngle -> target
constexpr float kJointKd = 2.5f;         // PD damping gain
constexpr float kJointMoveCostConst = 0.05f; // scales |commanded accel| * |rate| -> E_move
constexpr float kMaxJointAngularRate = 8.0f; // rad/s, for proprioception normalization

// Morphological mutation odds, verbatim from Phase 4 Step 5.4.
constexpr float kMutateAddLimbChance = 0.05f;
constexpr float kMutateDeleteLimbChance = 0.05f;
constexpr float kMutateScaleChance = 0.20f;

// Chance a child flips mobility relative to its parent — the speciation
// event between the "animal" and "plant" lineages (see CreatureDNA::isMobile).
constexpr float kMutateFlipMobilityChance = 0.03f;

// --- Simulation-LOD tunables ---
// Only creatures within `kActivationRadius` of the camera are ever
// actually simulated (physics + brains + energy economy); everything
// else is a dormant CreatureRecord frozen in place. `kDeactivationRadius`
// is intentionally larger than `kActivationRadius` (hysteresis) so a
// creature sitting near the boundary doesn't flicker in and out every
// frame.
constexpr int kMaxActiveCreatures = 28;
constexpr float kActivationRadius = 220.0f;
constexpr float kDeactivationRadius = 300.0f;

enum class CreatureTag : uint8_t { Living, Corpse };

// One OBB in the DAG. `parentIndex == -1` marks the root (index 0);
// every other limb's parentIndex must reference an earlier index in the
// same CreatureDNA::limbs array (enforced by mutateCreatureDNA / the DNA
// generator), which keeps a single forward pass enough to do forward
// kinematics for the whole skeleton.
struct LimbDNA {
  int parentIndex = -1;
  vec3 extents = vec3(0.22f, 0.18f, 0.3f);
  vec3 parentAnchorLocal = vec3(0.0f, 0.0f, 1.0f); // point on the PARENT, in the parent's local space
  vec3 childAnchorLocal = vec3(0.0f, 0.0f, -1.0f); // point on THIS limb, in its own local space, that meets the parent anchor
  vec3 jointAxisLocal = vec3(1.0f, 0.0f, 0.0f);    // hinge axis, expressed in the PARENT's local space
  float jointLimit = 0.9f;                         // max |angle| (radians) the joint's PD controller can reach
};

struct CreatureDNA {
  std::vector<LimbDNA> limbs; // limbs[0] is always the root (parentIndex == -1)
  vec3 colorTint = vec3(0.6f, 0.85f, 0.3f);

  // The animal/plant speciation gene. Mobile creatures run the full
  // root-locomotion + per-joint brain loop and get the movement-shaping
  // reward; non-mobile ("plant") creatures never query those brains at
  // all — they just sit, graze, age, and reproduce via the GA layer
  // alone, making them a defenseless (no fleeing, no counter-predation
  // beyond passive thinness-based spiking) but cheap-to-run food source
  // for mobile creatures to hunt down and scavenge. Inherited faithfully
  // with a small flip chance (kMutateFlipMobilityChance) each generation,
  // so lineages can speciate either direction over time.
  bool isMobile = true;
};

// Random starter body plan: a root torso plus a handful of limbs
// attached directly to it at randomized anchor points/axes. Used to seed
// brand-new (generation 0) creatures.
CreatureDNA makeDefaultCreatureDNA(uint32_t &rngState);

// Result of mutating a parent's DNA (Step 5.4): the mutated DNA, plus a
// same-length mapping from each *child* limb back to the *parent* limb
// it came from (-1 if the limb is newly added by an add-limb mutation).
// Reproduction uses this mapping to know which joint brains can be
// Lamarckian-inherited verbatim vs. need a fresh random brain.
struct DNAMutationResult {
  CreatureDNA dna;
  std::vector<int> sourceLimbIndex;
};
DNAMutationResult mutateCreatureDNA(const CreatureDNA &parent,
                                    uint32_t &rngState);

// ---------------------------------------------------------------------
// CreatureRecord: the ALWAYS-RESIDENT state for one creature, whether or
// not it's currently being simulated. The planet can hold thousands of
// these; only up to kMaxActiveCreatures at a time also have a live Actor
// tree (root Actor + one Actor per limb, each with a RigidBodyComponent
// + OBBCollider) and get touched by physics/brains/energy each frame.
//
// Simplification: a dormant creature does not age, eat, move, or think
// — it's frozen exactly as it was the moment it went dormant, and picks
// back up from there once it re-enters the activation radius. This is a
// deliberate scope decision (see the integration notes) rather than an
// attempt at offline/statistical simulation of the whole planet's
// population; the hook to add a cheap "catch-up" model later is
// `updateActivation()` in Game.cpp, right where a record is reactivated.
// ---------------------------------------------------------------------
struct CreatureRecord {
  CreatureDNA dna;
  BrainWeights rootBrain;
  std::vector<JointBrainWeights> jointBrains; // index-matched w/ dna.limbs; [0] (root) unused
  std::vector<float> limbAngles;              // index-matched w/ dna.limbs; [0] unused, persists hinge pose across activation cycles

  float energy = kStartEnergy;
  CreatureTag tag = CreatureTag::Living;
  int generation = 0;
  uint32_t rngState = 1;

  vec3 pos = vec3(0.0f, 0.0f, 0.0f);
  vec4 rot = vec4(0.0f, 0.0f, 0.0f, 1.0f);
  float cpgPhase = 0.0f;
  float ageSeconds = 0.0f;

  bool active = false;
  Actor *rootActor = nullptr; // only valid while active

  bool IsAlive() const { return tag == CreatureTag::Living; }
};

// ---------------------------------------------------------------------
// Live-only components. These are constructed when a CreatureRecord is
// activated and destroyed (along with their Actor) when it's
// deactivated; the record itself is what survives in between.
// ---------------------------------------------------------------------

// Lives on every limb Actor, INCLUDING the root (limbIndex == 0), so
// lookups don't need a special case to tell "am I part of a creature".
class LimbComponent : public Component {
public:
  LimbComponent(Actor *owner, Actor *rootActor, int limbIndex);
  ~LimbComponent() override;

  Actor *rootActor;
  int limbIndex;

  // Joint runtime state — meaningless for limbIndex == 0 (the root has
  // no parent to hinge against; it's driven directly by CreatureComponent's
  // root locomotion brain instead).
  RigidBodyComponent *parentBody = nullptr; // cached, nullptr for the root
  float currentAngle = 0.0f;                // radians
  float angularRate = 0.0f;                 // radians/sec
  std::vector<JointBrainTransition> jointBuffer; // transient: discarded on deactivate
  bool lastContact = false;
};

// Lives only on the root Actor.
class CreatureComponent : public Component {
public:
  CreatureComponent(Actor *owner, CreatureRecord *record);
  ~CreatureComponent() override;

  bool IsAlive() const { return record->IsAlive(); }

  CreatureRecord *record; // never null while this component exists
  std::vector<BrainTransition> rootBuffer; // transient: discarded on deactivate
  std::vector<Actor *> limbActors;         // index-matched w/ record->dna.limbs; [0] == owner

  bool lastOnGrass = false;
};

} // namespace life
