#pragma once
// General Context Memory (GCM): the guides, their zones, rings and agents
// (docs/02-framework.md, Sections 2.3 and 2.4).

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <vector>

#include "agents.hpp"
#include "config.hpp"
#include "guide_selection.hpp"
#include "problem.hpp"
#include "rng.hpp"

namespace gso {

// The five ways a new solution can update the memory (Figure 3 of the article).
enum class Case : int {
  A = 0,  // known region, better than its guide: replaces the guide (the region drifts)
  B = 1,  // known region, not better: mapped to a ring of the guide
  C = 2,  // new region, better than the worst elite guide: replaces it
  D = 3,  // new region, an expired diversity guide exists: replaces it
  E = 4,  // new region, memory saturated: anchored to the outermost ring of the closest guide
};

enum class Zone : unsigned char { Elite, Diversity };

template <class S>
struct Guide {
  S solution;
  double cost;
  std::uint64_t id;  // changes every time the slot receives a new solution (contextual validation)
  Zone zone;
  double born;       // when it entered the memory, in the unit of gcm.tenure
  long long visits;  // regional searches started from it since it entered the memory
  double wins = 0;   // regional searches from it that improved it or the best (discounted)
  double losses = 0; // regional searches from it that did not (discounted)
};

// Where a solution is: the guide (slot) and ring whose agent decides, and how the memory changed.
struct Location {
  int slot;
  int ring;
  double d_min;
  Case kase;
};

template <class P>
class GCM {
 public:
  using Solution = typename P::Solution;
  using AgentMaker = std::function<std::unique_ptr<Agent>()>;

  // decay_lambda < 0: agents of a replaced guide are reset; otherwise they keep this fraction.
  GCM(P& problem, const Config& cfg, AgentMaker make_agent, double decay_lambda)
      : problem_(problem),
        size_(cfg.gcm.size),
        elite_ratio_(cfg.gcm.elite_ratio),
        tenure_(cfg.gcm.tenure),
        expiry_first_(cfg.gcm.update == "expiry_first"),
        drift_(cfg.gcm.case_a == "drift"),
        delta_max_(cfg.region.delta_max),
        delta_ratio_(cfg.region.delta_ratio),
        reference_adapter_(cfg.region.delta_reference == "adapter"),
        rings_(cfg.region.rings),
        global_agent_(cfg.agent.scope == "global"),
        shuffled_agents_(cfg.agent.scope == "shuffled"),
        decay_lambda_(decay_lambda),
        make_agent_(std::move(make_agent)) {}

  // Builds gcm.size guides with the adapter. The best round(elite_ratio * size) are elite.
  void initialize(Rng& rng, double now) {
    guides_.clear();
    agents_.clear();
    for (int i = 0; i < size_; ++i) {
      Solution s = problem_.make_guide(rng);
      const double c = problem_.cost(s);
      guides_.push_back(Guide<Solution>{std::move(s), c, next_id_++, Zone::Diversity, now, 0});
      if (!global_agent_)
        for (int r = 0; r < rings_; ++r) agents_.push_back(make_agent_());
    }
    if (global_agent_) agents_.push_back(make_agent_());  // agent.scope = global: one agent for all
    const auto& order = ranking();
    const int elite = static_cast<int>(std::lround(elite_ratio_ * size_));
    for (int k = 0; k < elite; ++k) guides_[order[k]].zone = Zone::Elite;
    if (delta_max_ < 0 && delta_ratio_ > 0) delta_max_ = delta_ratio_ * reference_distance();
  }

  // Radius of the regions (fixed once the guides are built when region.delta_ratio is used).
  double delta_max() const { return delta_max_; }

  // Reference of region.delta_ratio: the adapter's max_distance(), or the largest distance between
  // the initial guides.
  double reference_distance() {
    if (reference_adapter_) {
      if constexpr (detail::has_max_distance<P>::value) {
        return problem_.max_distance();
      } else {
        throw ConfigError("region.delta_reference = adapter needs `double max_distance() const` in the adapter");
      }
    }
    double largest = 0;
    for (int i = 0; i < size(); ++i)
      for (int j = i + 1; j < size(); ++j)
        largest = std::max(largest, problem_.distance(guides_[i].solution, guides_[j].solution));
    return largest;
  }

  // Locates s (cost f) and updates the memory (Section 2.4) with the rule gcm.update:
  //   cases         the article's text: cases A-E, elite and diversity zones
  //   expiry_first  the implementation used in the article's experiments: an expired guide is
  //                 replaced first; then the guide of the region if s is better; then the worst
  //                 guide if s is outside every region and better than it (no zones)
  //
  // `hint`: a slot that is probably the closest (the guide the regional search started from). It is
  // measured first, so that a bounded distance (distance(a, b, limit)) can stop early against the
  // others. The result does not depend on it: ties go to the lowest slot, as in a plain scan.
  Location locate_and_update(const Solution& s, double f, double now, int hint = 0) {
    int closest = hint;
    double d_min = distance(s, guides_[hint].solution, std::numeric_limits<double>::infinity());
    for (int i = 0; i < size(); ++i) {
      if (i == hint) continue;
      const double d = distance(s, guides_[i].solution, d_min);
      if (d < d_min || (d == d_min && i < closest)) {
        d_min = d;
        closest = i;
      }
    }
    if (expiry_first_) {
      if (const int x = first_expired(now); x >= 0) {
        replace(x, s, f, guides_[x].zone, now);
        return {x, 0, d_min, Case::D};
      }
    }
    if (d_min <= delta_max_) {
      if (f < guides_[closest].cost) {
        if (drift_) move_guide(closest, s, f, now);
        else replace(closest, s, f, guides_[closest].zone, now);
        return {closest, 0, d_min, Case::A};
      }
      return {closest, ring_of(d_min), d_min, Case::B};
    }
    if (expiry_first_) {
      if (const int w = worst(); f < guides_[w].cost) {
        replace(w, s, f, guides_[w].zone, now);
        return {w, 0, d_min, Case::C};
      }
      return {closest, rings_ - 1, d_min, Case::E};
    }
    if (const int w = worst_elite(); w >= 0 && f < guides_[w].cost) {
      replace(w, s, f, Zone::Elite, now);
      return {w, 0, d_min, Case::C};
    }
    if (const int x = oldest_expired(now); x >= 0) {
      replace(x, s, f, Zone::Diversity, now);
      return {x, 0, d_min, Case::D};
    }
    return {closest, rings_ - 1, d_min, Case::E};
  }

  // Ring of s with respect to the guide in `slot` (for search.agent_of = start).
  int ring_in(int slot, const Solution& s) {
    if (rings_ == 1) return 0;
    return ring_of(problem_.distance(s, guides_[slot].solution));
  }

  // A regional search starts from the guide in `slot`.
  void visit(int slot) { ++guides_[slot].visits; }

  // A regional search from the guide in `slot` ended: it did (success) or did not improve the guide
  // or the best solution. Earlier outcomes are discounted by `discount` (search.value_discount).
  void record_search(int slot, bool success, double discount) {
    Guide<Solution>& g = guides_[slot];
    g.wins = discount * g.wins + (success ? 1.0 : 0.0);
    g.losses = discount * g.losses + (success ? 0.0 : 1.0);
  }

  // Cost and visits of every guide, for the guide-selection policy.
  const std::vector<GuideView>& views() {
    views_.resize(guides_.size());
    for (std::size_t i = 0; i < guides_.size(); ++i)
      views_[i] = GuideView{guides_[i].cost, guides_[i].visits, guides_[i].wins, guides_[i].losses};
    return views_;
  }

  int size() const { return static_cast<int>(guides_.size()); }
  int rings() const { return rings_; }
  const Guide<Solution>& guide(int slot) const { return guides_[slot]; }
  Agent& agent(int slot, int ring) {
    return global_agent_ ? *agents_[0] : *agents_[static_cast<std::size_t>(slot) * rings_ + ring];
  }

  // The agent that takes a decision in (slot, ring): its own (region), the single one (global), or
  // one drawn at random from the gcm.size * region.rings agents (shuffled; only then is rng used).
  int agent_index(int slot, int ring, Rng& rng) {
    if (global_agent_) return 0;
    if (shuffled_agents_) return static_cast<int>(rng.below(agents_.size()));
    return slot * rings_ + ring;
  }
  Agent& agent_at(int index) { return *agents_[static_cast<std::size_t>(index)]; }

  // Slots ordered by cost, best first. The returned reference is valid until the next call.
  const std::vector<int>& ranking() {
    ranking_.resize(guides_.size());
    std::iota(ranking_.begin(), ranking_.end(), 0);
    std::stable_sort(ranking_.begin(), ranking_.end(),
                     [this](int a, int b) { return guides_[a].cost < guides_[b].cost; });
    return ranking_;
  }

  // region.on_saturation = replace: the region of `slot` is abandoned and a new guide takes the slot
  // (its agents follow agent.on_replace, as in any replacement).
  void renew(int slot, const Solution& s, double f, double now) { replace(slot, s, f, guides_[slot].zone, now); }

  int best_slot() const {
    int best = 0;
    for (int i = 1; i < size(); ++i)
      if (guides_[i].cost < guides_[best].cost) best = i;
    return best;
  }

 private:
  double distance(const Solution& a, const Solution& b, double limit) {
    if constexpr (detail::has_bounded_distance<P, Solution>::value) {
      return problem_.distance(a, b, limit);
    } else {
      (void)limit;
      return problem_.distance(a, b);
    }
  }

  // Ring of a solution at distance d: floor(n_rings * d / delta_max), clamped to the last ring.
  int ring_of(double d) const {
    if (delta_max_ <= 0) return 0;
    const int r = static_cast<int>(rings_ * d / delta_max_);
    return std::min(std::max(r, 0), rings_ - 1);
  }

  int worst() const {
    int w = 0;
    for (int i = 1; i < size(); ++i)
      if (guides_[i].cost > guides_[w].cost) w = i;
    return w;
  }

  // First guide (in slot order) whose tenure has run out, in any zone; -1 if none.
  int first_expired(double now) const {
    for (int i = 0; i < size(); ++i)
      if (now - guides_[i].born >= tenure_) return i;
    return -1;
  }

  int worst_elite() const {
    int worst = -1;
    for (int i = 0; i < size(); ++i)
      if (guides_[i].zone == Zone::Elite && (worst < 0 || guides_[i].cost > guides_[worst].cost)) worst = i;
    return worst;
  }

  int oldest_expired(double now) const {
    int oldest = -1;
    for (int i = 0; i < size(); ++i) {
      const auto& g = guides_[i];
      if (g.zone == Zone::Diversity && now - g.born >= tenure_ && (oldest < 0 || g.born < guides_[oldest].born))
        oldest = i;
    }
    return oldest;
  }

  // gcm.case_a = drift: the region keeps its identity (id) and its agents; everything else as in
  // replace(), so that guide selection and tenure behave the same with renew and drift. The value of
  // the region (wins, losses) is kept: it is the same region.
  void move_guide(int slot, const Solution& s, double f, double now) {
    Guide<Solution>& g = guides_[slot];
    g.solution = s;
    g.cost = f;
    g.born = now;
    g.visits = 0;
  }

  void replace(int slot, const Solution& s, double f, Zone zone, double now) {
    guides_[slot] = Guide<Solution>{s, f, next_id_++, zone, now, 0};
    if (global_agent_ || shuffled_agents_) return;  // these agents are not tied to any guide
    for (int r = 0; r < rings_; ++r) {
      Agent& a = agent(slot, r);
      if (decay_lambda_ < 0) a.reset();
      else a.decay(decay_lambda_);
    }
  }

  P& problem_;
  int size_;
  double elite_ratio_, tenure_;
  bool expiry_first_;
  bool drift_;
  double delta_max_, delta_ratio_;
  bool reference_adapter_;
  int rings_;
  bool global_agent_;
  bool shuffled_agents_;  // agent.scope = shuffled
  double decay_lambda_;
  AgentMaker make_agent_;
  std::vector<Guide<Solution>> guides_;
  std::vector<std::unique_ptr<Agent>> agents_;  // slot * rings + ring
  std::vector<int> ranking_;
  std::vector<GuideView> views_;
  std::uint64_t next_id_ = 1;
};

}  // namespace gso
