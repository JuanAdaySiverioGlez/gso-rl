#pragma once
// GSO-RL adapter for the 0/1 knapsack problem: the smallest complete example.
//
// Choose items to maximize their total value without exceeding the capacity. GSO-RL minimizes,
// so the cost is the negated value. The four parts of an adapter are marked 1-4.

#include <gso/gso.hpp>

#include <algorithm>
#include <numeric>
#include <vector>

struct Knapsack {
  // -- Instance ---------------------------------------------------------------------------------
  std::vector<int> weight, value;
  int capacity = 0;

  // A random "strongly correlated" instance (value = weight + 10), hard for greedy methods.
  Knapsack(int items, std::uint64_t seed) {
    gso::Rng rng(seed);
    for (int i = 0; i < items; ++i) {
      weight.push_back(rng.uniform_int(1, 100));
      value.push_back(weight.back() + 10);
    }
    capacity = std::accumulate(weight.begin(), weight.end(), 0) / 2;
  }

  int items() const { return static_cast<int>(weight.size()); }

  // -- 1. Solution and cost ------------------------------------------------------------------------
  struct Solution {
    std::vector<char> packed;  // packed[i] = 1 if item i is in the knapsack
    int weight = 0, value = 0;
  };

  double cost(const Solution& s) const { return -s.value; }

  // -- 2. Guide generator: random filling + local search ----------------------------------------------
  Solution make_guide(gso::Rng& rng) {
    Solution s{std::vector<char>(weight.size(), 0), 0, 0};
    fill_randomly(s, rng);
    improve(s);
    return s;
  }

  // -- 3. Dissimilarity: share of items packed in one solution and not in the other, in [0, 1] -------
  double distance(const Solution& a, const Solution& b) const {
    int different = 0;
    for (int i = 0; i < items(); ++i) different += a.packed[i] != b.packed[i];
    return static_cast<double>(different) / items();
  }

  // -- 4. Macro-moves: the actions the agents choose between ----------------------------------------
  gso::MacroMoves<Knapsack> macro_moves() {
    return {
        {"rebuild", &Knapsack::rebuild, gso::restart},  // a new solution, unrelated to the current one
        {"perturb", &Knapsack::perturb},
        {"tabu", &Knapsack::tabu},
    };
  }

  // Parameters of the macro-moves. They appear in --help and in the printed configuration as
  // problem.<name>, and can be set like any other parameter (--problem.tabu_steps=100).
  int perturb_rounds = 10;
  int perturb_items = 3;
  int tabu_steps = 50;
  int tabu_tenure = 7;

  void parameters(gso::Params& p) {
    p.add("perturb_rounds", perturb_rounds, "perturb: rounds of remove-refill-improve", ">= 1");
    p.add("perturb_items", perturb_items, "perturb: items removed per round", ">= 1");
    p.add("tabu_steps", tabu_steps, "tabu: steps of the tabu search", ">= 1");
    p.add("tabu_tenure", tabu_tenure, "tabu: steps an item stays tabu after it moves", ">= 0");
  }

  // rebuild: start again from scratch.
  void rebuild(Solution& s, gso::Context<Knapsack>& ctx) { s = make_guide(ctx.rng()); }

  // perturb: remove a few items, refill, improve; keep the result only if it is strictly better.
  void perturb(Solution& s, gso::Context<Knapsack>& ctx) {
    for (int round = 0; round < perturb_rounds; ++round) {
      Solution t = s;
      for (int k = 0; k < perturb_items; ++k) {
        std::vector<int> in = packed_items(t);
        if (in.empty()) break;
        drop(t, ctx.rng().pick(in));
      }
      fill_randomly(t, ctx.rng());
      improve(t);
      if (t.value > s.value) s = t;
    }
  }

  // tabu: short tabu search over swaps (drop one item, add another) and additions. It accepts
  // worse solutions, so it may finish below where it started; the best one it visits is offered.
  void tabu(Solution& s, gso::Context<Knapsack>& ctx) {
    std::vector<int> tabu_until(weight.size(), -1);
    int best_value = s.value;
    for (int step = 0; step < tabu_steps; ++step) {
      int best_out = -1, best_in = -1, best_gain = 0;
      bool found = false;
      for (int j = 0; j < items(); ++j) {  // j enters
        if (s.packed[j]) continue;
        for (int i = -1; i < items(); ++i) {  // i leaves (-1: nothing leaves)
          if (i >= 0 && !s.packed[i]) continue;
          const int new_weight = s.weight + weight[j] - (i >= 0 ? weight[i] : 0);
          if (new_weight > capacity) continue;
          const int gain = value[j] - (i >= 0 ? value[i] : 0);
          const bool is_tabu = tabu_until[j] > step || (i >= 0 && tabu_until[i] > step);
          if (is_tabu && s.value + gain <= best_value) continue;  // aspiration: a new best is allowed
          if (!found || gain > best_gain) {
            found = true;
            best_gain = gain;
            best_in = j;
            best_out = i;
          }
        }
      }
      if (!found) break;
      if (best_out >= 0) {
        drop(s, best_out);
        tabu_until[best_out] = step + tabu_tenure;
      }
      add(s, best_in);
      tabu_until[best_in] = step + tabu_tenure;
      if (s.value > best_value) {
        best_value = s.value;
        ctx.offer(s);
      }
    }
  }

  // Upper bound of the linear relaxation (Dantzig), to judge the result in the example.
  double lp_bound() const {
    std::vector<int> order(weight.size());
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(),
              [&](int a, int b) { return value[a] * weight[b] > value[b] * weight[a]; });
    double room = capacity, bound = 0;
    for (int i : order) {
      const double take = std::min(1.0, room / weight[i]);
      bound += take * value[i];
      room -= take * weight[i];
      if (room <= 0) break;
    }
    return bound;
  }

 private:
  void add(Solution& s, int i) const {
    s.packed[i] = 1;
    s.weight += weight[i];
    s.value += value[i];
  }
  void drop(Solution& s, int i) const {
    s.packed[i] = 0;
    s.weight -= weight[i];
    s.value -= value[i];
  }

  std::vector<int> packed_items(const Solution& s) const {
    std::vector<int> in;
    for (int i = 0; i < items(); ++i)
      if (s.packed[i]) in.push_back(i);
    return in;
  }

  // Adds items in random order while they fit.
  void fill_randomly(Solution& s, gso::Rng& rng) const {
    std::vector<int> order(weight.size());
    std::iota(order.begin(), order.end(), 0);
    rng.shuffle(order);
    for (int i : order)
      if (!s.packed[i] && s.weight + weight[i] <= capacity) add(s, i);
  }

  // Best-improvement local search: additions and swaps, until no move improves the value.
  void improve(Solution& s) const {
    for (bool improved = true; improved;) {
      improved = false;
      int best_out = -1, best_in = -1, best_gain = 0;
      for (int j = 0; j < items(); ++j) {
        if (s.packed[j]) continue;
        for (int i = -1; i < items(); ++i) {
          if (i >= 0 && !s.packed[i]) continue;
          if (s.weight + weight[j] - (i >= 0 ? weight[i] : 0) > capacity) continue;
          const int gain = value[j] - (i >= 0 ? value[i] : 0);
          if (gain > best_gain) {
            best_gain = gain;
            best_in = j;
            best_out = i;
          }
        }
      }
      if (best_in >= 0) {
        if (best_out >= 0) drop(s, best_out);
        add(s, best_in);
        improved = true;
      }
    }
  }
};
