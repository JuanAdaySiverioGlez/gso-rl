// How each built-in agent learns on a synthetic 3-armed Bernoulli bandit (0.2, 0.5, 0.8).
// Prints the share of choices of the best arm (mean of 20 seeds, 3000 steps) in four scenarios:
//   stationary
//   swap        best and worst arms exchange at step 1500; measured on the last 500 steps
//   x1e-3       every reward multiplied by 0.001 (is the agent independent of the reward scale?)
//   delay 30    every reward arrives 30 steps late, as in the Current Context Memory
// The table in docs/04-learning-agents.md comes from this program.

#include <gso/gso.hpp>

#include <cstdio>
#include <deque>
#include <string>

static double share(const std::string& spec, int scenario, std::uint64_t seed) {
  gso::Rng rng(seed);
  auto agent = gso::make_agent(gso::Spec::parse(spec), {"a", "b", "c"});
  double p[3] = {0.2, 0.5, 0.8};
  const int steps = 3000;
  int best = 0, counted = 0;
  std::deque<std::pair<int, double>> pending;
  for (int t = 0; t < steps; ++t) {
    if (scenario == 1 && t == steps / 2) std::swap(p[0], p[2]);
    const int m = agent->select(rng);
    const double r = (rng.bernoulli(p[m]) ? 1.0 : 0.0) * (scenario == 2 ? 1e-3 : 1.0);
    if (scenario != 1 || t >= steps - 500) {
      best += m == ((scenario == 1 && t >= steps / 2) ? 0 : 2);
      ++counted;
    }
    pending.emplace_back(m, r);
    if (static_cast<int>(pending.size()) > (scenario == 3 ? 30 : 0)) {
      agent->update(pending.front().first, pending.front().second);
      pending.pop_front();
    }
  }
  return static_cast<double>(best) / counted;
}

int main() {
  const char* agents[] = {"ucb1(c=1.0)", "epsilon_greedy(eps=0.1)", "softmax(tau=0.1)", "thompson",
                          "exp3(gamma=0.1)", "sliding_window_ucb(c=0.5, window=300)",
                          "discounted_ucb(c=0.5, gamma=0.99)", "gradient(alpha=0.1)", "random"};
  std::printf("| agent.type | stationary | swap | x1e-3 | delay 30 |\n|---|---|---|---|---|\n");
  for (const char* a : agents) {
    std::printf("| `%s` |", a);
    for (int scenario = 0; scenario < 4; ++scenario) {
      double mean = 0;
      for (std::uint64_t seed = 1; seed <= 20; ++seed) mean += share(a, scenario, seed);
      std::printf(" %.2f |", mean / 20);
    }
    std::printf("\n");
  }
}
