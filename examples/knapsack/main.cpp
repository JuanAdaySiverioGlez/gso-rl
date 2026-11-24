// GSO-RL on a random 0/1 knapsack instance.
//
//   ./knapsack                         run with the defaults below (200 items, instance seed 7)
//   ./knapsack 500:3                   another instance: items:seed
//   ./knapsack --check                 check the adapter and suggest parameter values
//   ./knapsack --help                  list every parameter
//   ./knapsack --agent.type=random     ablation GSO-Random
//   ./knapsack --config=my_run.cfg     read parameters from a file

#include <iostream>
#include <string>

#include "knapsack.hpp"

int main(int argc, char** argv) {
  int items = 200;
  unsigned long long instance_seed = 7;
  if (argc > 1 && argv[1][0] != '-') {  // optional instance "items:seed"
    const std::string a = argv[1];
    items = std::stoi(a.substr(0, a.find(':')));
    instance_seed = std::stoull(a.substr(a.find(':') + 1));
  }
  Knapsack problem(items, instance_seed);

  gso::Config cfg;  // the defaults (docs/05): only the budget has to be set
  cfg.budget.seconds = 2;

  return gso::run_main(problem, cfg, argc, argv, [&](const gso::Result<Knapsack>& result) {
    std::cout << "best value: " << result.best.value << "   (upper bound: " << problem.lp_bound() << ")\n";
    std::cout << "best cost: " << result.best_cost << "\n";  // the value minimized (-value), for tools/irace
  });
}
