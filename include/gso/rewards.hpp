#pragma once
// Reward functions (docs/02-framework.md, Section 2.6).
//
// A decision is rewarded when it leaves the Current Context Memory, with what happened after it.
// Built-in rewards, selected with the parameter reward.type:
//   eq3(alpha=0.75, beta=1-alpha)        Eq. 3 of the article
//   levels(best=6, guide=3, current=1)   points by kind of improvement (6/1/0 of the article: guide=0)
// Your own: gso::register_reward("name", factory), then reward.type = "name(...)".

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <string>

#include "params.hpp"

namespace gso {

// What a reward function sees about one decision.
struct RewardInput {
  double cost_before;        // f(S) when the decision was taken
  double best_before;        // f(S*) when the decision was taken
  double guide_cost_before;  // cost of the guide of its region when the decision was taken
  double cost_now;           // f(S) when the decision is evaluated
  double best_now;           // f(S*) when the decision is evaluated
};

using RewardFn = std::function<double(const RewardInput&)>;

namespace rewards {

// Eq. 3 of the article:
//   r = alpha * (f_before - f_now) / |f_before|  +  beta * (f*_now - f_now) / |f*_now|
// The denominators are in absolute value so that the formula also works with negative costs
// (maximization problems written as minimization); for positive costs it is exactly Eq. 3.
inline RewardFn eq3(double alpha, double beta) {
  return [alpha, beta](const RewardInput& in) {
    auto relative = [](double diff, double base) { return diff / (std::abs(base) > 1e-12 ? std::abs(base) : 1.0); };
    return alpha * relative(in.cost_before - in.cost_now, in.cost_before) +
           beta * relative(in.best_now - in.cost_now, in.best_now);
  };
}

// Points by kind of improvement: a new best solution, better than the guide of its region,
// better than the solution it started from; 0 otherwise. The reward is the highest level reached.
// levels(best=6, guide=0, current=1) is the 6/1/0 reward of the reference implementation (GSOS_RL)
// used for the article's experiments.
inline RewardFn levels(double best, double guide, double current) {
  return [=](const RewardInput& in) {
    double r = 0.0;
    if (in.best_now < in.best_before) r = std::max(r, best);
    if (in.cost_now < in.guide_cost_before) r = std::max(r, guide);
    if (in.cost_now < in.cost_before) r = std::max(r, current);
    return r;
  };
}

}  // namespace rewards

using RewardFactory = std::function<RewardFn(const Spec&)>;

namespace detail {
inline std::map<std::string, RewardFactory>& reward_registry() {
  static std::map<std::string, RewardFactory> registry = {
      {"eq3",
       [](const Spec& s) {
         s.allow_only({"alpha", "beta"});
         const double alpha = s.number("alpha", 0.75);
         return rewards::eq3(alpha, s.number("beta", 1.0 - alpha));
       }},
      {"levels",
       [](const Spec& s) {
         s.allow_only({"best", "guide", "current"});
         return rewards::levels(s.number("best", 6), s.number("guide", 3), s.number("current", 1));
       }},
  };
  return registry;
}
}  // namespace detail

inline void register_reward(const std::string& name, RewardFactory factory) {
  detail::reward_registry()[name] = std::move(factory);
}

inline RewardFn make_reward(const Spec& spec) {
  auto& registry = detail::reward_registry();
  const auto it = registry.find(spec.name);
  if (it == registry.end()) {
    std::string list;
    for (const auto& kv : registry) list += (list.empty() ? "" : ", ") + kv.first;
    throw ConfigError("reward.type: unknown reward '" + spec.name + "'. Available: " + list);
  }
  return it->second(spec);
}

}  // namespace gso
