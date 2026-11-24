#pragma once
// Guide selection: which guide a regional search starts from (docs/02-framework.md, Section 2.2).
//
// A policy sees, for every guide, its cost and how many regional searches have started from it
// since it entered the memory, and returns the slot of the chosen guide.
// Built-in policies, selected with the parameter search.guide_selection:
//   geometric(p=0.25)  guides ranked by cost; position k chosen with probability proportional to
//                      (1-p)^k (the article's text)
//   ucb(c=1.4)         argmax  quality + c * sqrt(2 ln(V + 1) / (v + 1)), quality = 1 for the best
//                      guide and 0 for the worst, v = visits of the guide, V = all visits
//                      (the implementation used in the article's experiments)
//   uniform            any guide with the same probability
//   best               always the best guide
//   value(a=1, b=1)    Thompson sampling over the value of each region: a draw from
//                      Beta(a + wins, b + losses) per guide, the largest is chosen. wins and losses
//                      count the regional searches from the guide that did and did not improve it
//                      (or the best solution), discounted by search.value_discount, so the value
//                      follows the region as it is exploited: live regions are searched, exhausted
//                      ones are left
// Your own: gso::register_guide_selection("name", factory).

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <numeric>
#include <string>
#include <vector>

#include "params.hpp"
#include "rng.hpp"

namespace gso {

// What a guide-selection policy sees about one guide.
struct GuideView {
  double cost;
  long long visits;  // regional searches started from this guide since it entered the memory
  double wins = 0;   // of those, the ones that improved it or the best solution (discounted)
  double losses = 0; // the ones that did not (discounted)
};

// (guides, rng) -> slot of the chosen guide
using GuideSelection = std::function<int(const std::vector<GuideView>& guides, Rng& rng)>;

namespace guide_selection {

// Slots ordered by cost, best first.
inline std::vector<int> by_cost(const std::vector<GuideView>& g) {
  std::vector<int> order(g.size());
  std::iota(order.begin(), order.end(), 0);
  std::stable_sort(order.begin(), order.end(), [&](int a, int b) { return g[a].cost < g[b].cost; });
  return order;
}

// Geometric distribution over the ranking, truncated to the number of guides, sampled by inversion.
inline GuideSelection geometric(double p) {
  return [p](const std::vector<GuideView>& g, Rng& rng) {
    const int n = static_cast<int>(g.size());
    int k = 0;
    if (p < 1.0 && n > 1) {
      const double q = 1.0 - p;
      const double mass = 1.0 - std::pow(q, n);  // probability of positions 0..n-1
      k = static_cast<int>(std::floor(std::log1p(-rng.uniform() * mass) / std::log(q)));
      k = std::min(std::max(k, 0), n - 1);
    }
    return by_cost(g)[k];
  };
}

// Quality (normalized cost) plus a UCB1 exploration bonus on the number of visits.
inline GuideSelection ucb(double c) {
  return [c](const std::vector<GuideView>& g, Rng&) {
    double best = std::numeric_limits<double>::infinity(), worst = -best;
    long long visits = 0;
    for (const auto& v : g) {
      best = std::min(best, v.cost);
      worst = std::max(worst, v.cost);
      visits += v.visits;
    }
    const double range = worst > best ? worst - best : 1.0;
    const double log_v = std::log(static_cast<double>(visits) + 1.0);
    int chosen = 0;
    double top = -std::numeric_limits<double>::infinity();
    for (int i = 0; i < static_cast<int>(g.size()); ++i) {
      const double quality = 1.0 - (g[i].cost - best) / range;
      const double score = quality + c * std::sqrt(2.0 * log_v / (static_cast<double>(g[i].visits) + 1.0));
      if (score > top) {
        top = score;
        chosen = i;
      }
    }
    return chosen;
  };
}

inline GuideSelection uniform() {
  return [](const std::vector<GuideView>& g, Rng& rng) { return static_cast<int>(rng.below(g.size())); };
}

// Thompson sampling over the (discounted) success of the regional searches from each guide.
inline GuideSelection value(double a, double b) {
  return [a, b](const std::vector<GuideView>& g, Rng& rng) {
    int chosen = 0;
    double top = -1;
    for (int i = 0; i < static_cast<int>(g.size()); ++i) {
      const double draw = rng.beta(a + g[i].wins, b + g[i].losses);
      if (draw > top) {
        top = draw;
        chosen = i;
      }
    }
    return chosen;
  };
}

inline GuideSelection best() {
  return [](const std::vector<GuideView>& g, Rng&) { return by_cost(g)[0]; };
}

}  // namespace guide_selection

using GuideSelectionFactory = std::function<GuideSelection(const Spec&)>;

namespace detail {
inline std::map<std::string, GuideSelectionFactory>& guide_selection_registry() {
  static std::map<std::string, GuideSelectionFactory> registry = {
      {"geometric",
       [](const Spec& s) {
         s.allow_only({"p"});
         const double p = s.number("p", 0.25);
         if (p <= 0 || p > 1) throw ConfigError("geometric(p): p must be in (0, 1]");
         return guide_selection::geometric(p);
       }},
      {"ucb",
       [](const Spec& s) {
         s.allow_only({"c"});
         const double c = s.number("c", 1.4);
         if (c < 0) throw ConfigError("ucb(c): c must be >= 0");
         return guide_selection::ucb(c);
       }},
      {"uniform", [](const Spec& s) { s.allow_only({}); return guide_selection::uniform(); }},
      {"best", [](const Spec& s) { s.allow_only({}); return guide_selection::best(); }},
      {"value",
       [](const Spec& s) {
         s.allow_only({"a", "b"});
         const double a = s.number("a", 1.0), b = s.number("b", 1.0);
         if (a <= 0 || b <= 0) throw ConfigError("value(a, b): a and b must be > 0");
         return guide_selection::value(a, b);
       }},
  };
  return registry;
}
}  // namespace detail

inline void register_guide_selection(const std::string& name, GuideSelectionFactory factory) {
  detail::guide_selection_registry()[name] = std::move(factory);
}

inline GuideSelection make_guide_selection(const Spec& spec) {
  auto& registry = detail::guide_selection_registry();
  const auto it = registry.find(spec.name);
  if (it == registry.end()) {
    std::string list;
    for (const auto& kv : registry) list += (list.empty() ? "" : ", ") + kv.first;
    throw ConfigError("search.guide_selection: unknown policy '" + spec.name + "'. Available: " + list);
  }
  return it->second(spec);
}

}  // namespace gso
