#pragma once

// Deliberately tiny (single hidden layer) neural networks used throughout
// the life simulation:
//
//  - `BrainWeights` (11 -> 8 -> 3): one per creature, drives the root
//    body's own thrust/steering — there's no joint to hinge the root
//    against, so it keeps using the original "free locomotion" policy
//    from the single-OBB creature version of this simulation.
//  - `JointBrainWeights` (6 -> 6 -> 1): one per NON-root limb, the "4 to
//    8 neuron" decentralized per-joint controller from the plan's Phase
//    2. Each only sees its own joint's local proprioception plus a
//    couple of signals broadcast from the root (CPG heartbeat, energy),
//    and outputs a single target hinge angle.
//
// Both are instantiations of the same TinyBrainWeights<In,Hid,Out>
// template below, so the forward pass / Gaussian action sampling /
// PPO-lite update are implemented exactly once. Everything here is
// header-only (all `inline`/templates) so it's safe to include from
// multiple translation units without ODR issues — unlike sdf.glsl, which
// is only ever included into Game.cpp.
//
// Policy: diagonal Gaussian over a small continuous action vector,
// mean = MLP(state), std = fixed (not learned) exploration noise.
// Actions are NOT squashed (no tanh) before sampling, which keeps the
// log-probability/gradient math a plain diagonal Gaussian instead of a
// tanh-Gaussian; callers clamp the sampled action to a sane physical
// range at the point where it's turned into a force/torque/angle.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace life {

// ---------------------------------------------------------------------
// Small xorshift32 PRNG so every creature/joint can carry its own cheap,
// deterministic stream without touching <random> engines per-instance.
// ---------------------------------------------------------------------
inline uint32_t xorshift32(uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}
inline float randf01(uint32_t &state) {
  return (xorshift32(state) >> 8) * (1.0f / 16777216.0f); // [0,1)
}
inline float randfRange(uint32_t &state, float lo, float hi) {
  return lo + randf01(state) * (hi - lo);
}
// Approximate standard-normal sample via Box-Muller.
inline float randGaussian(uint32_t &state) {
  float u1 = std::fmax(randf01(state), 1e-6f);
  float u2 = randf01(state);
  return std::sqrt(-2.0f * std::log(u1)) * std::cos(6.28318530718f * u2);
}

// ---------------------------------------------------------------------
// Generic tiny MLP: In -> tanh(Hid) -> linear(Out).
// ---------------------------------------------------------------------
template <int In, int Hid, int Out> struct TinyBrainWeights {
  float w1[In * Hid];
  float b1[Hid];
  float w2[Hid * Out];
  float b2[Out];
};

template <int In, int Hid, int Out> struct TinyBrainTransition {
  float state[In];
  float action[Out];
  float logProb;
  float reward;
};

template <int In, int Hid, int Out>
inline void randomizeTinyBrain(TinyBrainWeights<In, Hid, Out> &brain,
                               uint32_t &rngState) {
  // Small init range keeps the initial policy near-linear/near-zero so
  // newly spawned creatures/limbs don't immediately thrash at full
  // torque.
  for (float &w : brain.w1)
    w = randfRange(rngState, -0.6f, 0.6f);
  for (float &b : brain.b1)
    b = 0.0f;
  for (float &w : brain.w2)
    w = randfRange(rngState, -0.6f, 0.6f);
  for (float &b : brain.b2)
    b = 0.0f;
}

template <int In, int Hid, int Out>
inline void copyTinyBrain(TinyBrainWeights<In, Hid, Out> &dst,
                          const TinyBrainWeights<In, Hid, Out> &src) {
  dst = src;
}

template <int In, int Hid, int Out>
inline void tinyBrainForward(const TinyBrainWeights<In, Hid, Out> &brain,
                             const float *state, float *meanOut) {
  float hidden[Hid];
  for (int h = 0; h < Hid; h++) {
    float sum = brain.b1[h];
    for (int i = 0; i < In; i++)
      sum += brain.w1[i * Hid + h] * state[i];
    hidden[h] = std::tanh(sum);
  }
  for (int o = 0; o < Out; o++) {
    float sum = brain.b2[o];
    for (int h = 0; h < Hid; h++)
      sum += brain.w2[h * Out + o] * hidden[h];
    meanOut[o] = sum;
  }
}

namespace detail {

template <int In, int Hid, int Out>
inline void tinyBrainForwardCached(const TinyBrainWeights<In, Hid, Out> &brain,
                                   const float *state, float *hiddenOut,
                                   float *meanOut) {
  for (int h = 0; h < Hid; h++) {
    float sum = brain.b1[h];
    for (int i = 0; i < In; i++)
      sum += brain.w1[i * Hid + h] * state[i];
    hiddenOut[h] = std::tanh(sum);
  }
  for (int o = 0; o < Out; o++) {
    float sum = brain.b2[o];
    for (int h = 0; h < Hid; h++)
      sum += brain.w2[h * Out + o] * hiddenOut[h];
    meanOut[o] = sum;
  }
}

template <int Out>
inline float gaussianLogProb(const float *action, const float *mean,
                             float actionStd) {
  float logStd = std::log(actionStd);
  const float kLog2Pi = 1.8378770664f;
  float lp = 0.0f;
  for (int o = 0; o < Out; o++) {
    float z = (action[o] - mean[o]) / actionStd;
    lp += -0.5f * (z * z + kLog2Pi) - logStd;
  }
  return lp;
}

} // namespace detail

// Samples action ~ N(mean, actionStd^2) and returns its log-probability
// under that Gaussian (used later as the PPO "old" log-prob).
template <int In, int Hid, int Out>
inline float sampleTinyAction(const TinyBrainWeights<In, Hid, Out> & /*brain*/,
                              const float *mean, float *actionOut,
                              uint32_t &rngState, float actionStd) {
  for (int o = 0; o < Out; o++)
    actionOut[o] = mean[o] + actionStd * randGaussian(rngState);
  return detail::gaussianLogProb<Out>(actionOut, mean, actionStd);
}

// A handful of clipped-surrogate policy-gradient passes over the buffer
// ("PPO-lite"): no critic/value network, advantage is just the reward
// normalized within the buffer. Small enough networks/buffers that a few
// full epochs are cheap. Gradients are derived by hand for this exact
// 2-layer MLP rather than via autodiff.
template <int In, int Hid, int Out>
inline void
tinyPpoUpdate(TinyBrainWeights<In, Hid, Out> &brain,
             const std::vector<TinyBrainTransition<In, Hid, Out>> &buffer,
             float actionStd, float learningRate = 0.05f,
             float clipEps = 0.2f, int epochs = 4) {
  if (buffer.empty())
    return;

  float meanR = 0.0f;
  for (const auto &t : buffer)
    meanR += t.reward;
  meanR /= (float)buffer.size();
  float varR = 0.0f;
  for (const auto &t : buffer)
    varR += (t.reward - meanR) * (t.reward - meanR);
  varR /= (float)buffer.size();
  float stdR = std::sqrt(varR) + 1e-4f;

  for (int epoch = 0; epoch < epochs; epoch++) {
    for (const auto &t : buffer) {
      float advantage = (t.reward - meanR) / stdR;

      float hidden[Hid];
      float mean[Out];
      detail::tinyBrainForwardCached<In, Hid, Out>(brain, t.state, hidden,
                                                    mean);

      float newLogProb = detail::gaussianLogProb<Out>(t.action, mean, actionStd);
      float ratio = std::exp(std::min(newLogProb - t.logProb, 20.0f));

      bool improvingPastClip =
          (advantage >= 0.0f && ratio > 1.0f + clipEps) ||
          (advantage < 0.0f && ratio < 1.0f - clipEps);
      float dL_dLogProb = improvingPastClip ? 0.0f : ratio * advantage;
      if (dL_dLogProb == 0.0f)
        continue;

      float dL_dMean[Out];
      for (int o = 0; o < Out; o++) {
        float dLogProb_dMean = (t.action[o] - mean[o]) / (actionStd * actionStd);
        dL_dMean[o] = dL_dLogProb * dLogProb_dMean;
      }

      float dL_dHidden[Hid] = {};
      for (int o = 0; o < Out; o++) {
        brain.b2[o] += learningRate * dL_dMean[o];
        for (int h = 0; h < Hid; h++) {
          dL_dHidden[h] += dL_dMean[o] * brain.w2[h * Out + o];
          brain.w2[h * Out + o] += learningRate * dL_dMean[o] * hidden[h];
        }
      }

      for (int h = 0; h < Hid; h++) {
        float dTanh = 1.0f - hidden[h] * hidden[h];
        float dL_dPreact = dL_dHidden[h] * dTanh;
        brain.b1[h] += learningRate * dL_dPreact;
        for (int i = 0; i < In; i++)
          brain.w1[i * Hid + h] += learningRate * dL_dPreact * t.state[i];
      }
    }
  }
}

// Backward pass turning each transition's single-step reward into a
// discounted return: G[t] = reward[t] + gamma * G[t+1]. Overwrites
// `reward` in place (safe: callers only ever read it once, right before
// clearing the buffer). This is what actually lets an action get credit
// for energy gained several ticks later — e.g. moving toward a grass
// patch pays a movement cost NOW but only earns grazing income several
// ticks later, and with raw single-step reward that entire causal chain
// is invisible to the optimizer, so standing still — a smaller but
// immediate, guaranteed win every single tick — looks strictly better.
template <int In, int Hid, int Out>
inline void
computeDiscountedReturns(std::vector<TinyBrainTransition<In, Hid, Out>> &buffer,
                         float gamma) {
  float runningReturn = 0.0f;
  for (auto it = buffer.rbegin(); it != buffer.rend(); ++it) {
    runningReturn = it->reward + gamma * runningReturn;
    it->reward = runningReturn;
  }
}

// Per-tick discount factor. Not physically time-scaled (it discounts per
// simulation step, not per elapsed second), which is an approximation
// given `dt` varies slightly frame to frame — fine in practice since dt
// is capped and fairly stable. 0.98 gives an effective horizon of
// ~1/(1-gamma) ~= 50 ticks, roughly a second or two of real payoff delay
// (finding better grass, closing on prey) bridged back to the actions
// that caused it.
constexpr float kDiscountGamma = 0.98f;

// ---------------------------------------------------------------------
// Root locomotion brain: senses proprioception + energy + a forward
// vision cone, outputs [forwardThrust, yawTorque, pitchTorque].
// ---------------------------------------------------------------------
constexpr int kBrainInputs = 11;
constexpr int kBrainHidden = 8;
constexpr int kBrainOutputs = 3;
constexpr float kActionStd = 0.35f;

using BrainWeights = TinyBrainWeights<kBrainInputs, kBrainHidden, kBrainOutputs>;
using BrainTransition =
    TinyBrainTransition<kBrainInputs, kBrainHidden, kBrainOutputs>;

enum StateIndex {
  kStateForwardSpeed = 0,  // proprioception: forward speed / max speed
  kStateAngularSpeed = 1,  // proprioception: angular speed / max ang speed
  kStateContact = 2,       // 1.0 if resting on solid ground, else 0.0
  kStateCPG = 3,           // central pattern generator heartbeat signal
  kStateEnergy = 4,        // internal energy, normalized [0,1]
  kStateVisionGrass = 5,   // 1.0 if foot-point is on grass, else 0.0
  kStateVisionLiving = 6,  // 1.0 if nearest visible actor is a living creature
  kStateVisionCorpse = 7,  // 1.0 if nearest visible actor is a corpse
  kStateVisionRelVel = 8,  // closing speed toward nearest visible actor
  kStateVisionDist = 9,    // distance to nearest visible actor, normalized
  kStateBias = 10,         // constant 1.0 bias input
};

inline void randomizeBrain(BrainWeights &b, uint32_t &rng) {
  randomizeTinyBrain(b, rng);
}
inline void copyBrain(BrainWeights &dst, const BrainWeights &src) {
  copyTinyBrain(dst, src);
}
inline void brainForward(const BrainWeights &b, const float *state,
                         float *meanOut) {
  tinyBrainForward(b, state, meanOut);
}
inline float sampleAction(const BrainWeights &b, const float *mean,
                          float *actionOut, uint32_t &rngState) {
  return sampleTinyAction(b, mean, actionOut, rngState, kActionStd);
}
inline void ppoUpdate(BrainWeights &b, const std::vector<BrainTransition> &buf,
                      float learningRate = 0.05f, float clipEps = 0.2f,
                      int epochs = 4) {
  tinyPpoUpdate(b, buf, kActionStd, learningRate, clipEps, epochs);
}

// ---------------------------------------------------------------------
// Per-joint brain: local proprioception + a couple of root-broadcast
// signals, outputs a single target hinge angle (fraction of the joint's
// DNA-defined limit). This is the actual "4 to 8 neuron" decentralized
// controller the plan describes for each joint in the DAG.
// ---------------------------------------------------------------------
constexpr int kJointBrainInputs = 6;
constexpr int kJointBrainHidden = 6;
constexpr int kJointBrainOutputs = 1;
constexpr float kJointActionStd = 0.3f;

using JointBrainWeights =
    TinyBrainWeights<kJointBrainInputs, kJointBrainHidden, kJointBrainOutputs>;
using JointBrainTransition =
    TinyBrainTransition<kJointBrainInputs, kJointBrainHidden,
                        kJointBrainOutputs>;

enum JointStateIndex {
  kJointStateAngle = 0,   // current hinge angle / jointLimit, in [-1,1]
  kJointStateRate = 1,    // angular rate / max rate, normalized
  kJointStateContact = 2, // 1.0 if this limb itself is touching ground
  kJointStateCPG = 3,     // heartbeat broadcast from the root
  kJointStateEnergy = 4,  // internal energy broadcast from the root, [0,1]
  kJointStateBias = 5,
};

inline void randomizeJointBrain(JointBrainWeights &b, uint32_t &rng) {
  randomizeTinyBrain(b, rng);
}
inline void copyJointBrain(JointBrainWeights &dst,
                           const JointBrainWeights &src) {
  copyTinyBrain(dst, src);
}
inline void jointBrainForward(const JointBrainWeights &b, const float *state,
                              float *meanOut) {
  tinyBrainForward(b, state, meanOut);
}
inline float sampleJointAction(const JointBrainWeights &b, const float *mean,
                               float *actionOut, uint32_t &rngState) {
  return sampleTinyAction(b, mean, actionOut, rngState, kJointActionStd);
}
inline void jointPpoUpdate(JointBrainWeights &b,
                           const std::vector<JointBrainTransition> &buf,
                           float learningRate = 0.05f, float clipEps = 0.2f,
                           int epochs = 4) {
  tinyPpoUpdate(b, buf, kJointActionStd, learningRate, clipEps, epochs);
}

} // namespace life
