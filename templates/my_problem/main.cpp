// GSO-RL on your problem.
//
//   ./my_problem <instance>               solve
//   ./my_problem <instance> --check       check the adapter and suggest parameter values
//   ./my_problem --help                   every parameter, with default and meaning
//   ./my_problem <instance> --name=value  change any parameter (also --config=<file>, --preset=<name>)

#include <iostream>
#include <string>

#include "my_problem.hpp"

int main(int argc, char** argv) {
  const std::string instance = (argc > 1 && argv[1][0] != '-') ? argv[1] : "";
  MyProblem problem(instance);

  gso::Config cfg;          // the defaults (docs/05): only the budget has to be set
  cfg.budget.seconds = 60;  // TODO: your budget (or cfg.budget.iterations)

  return gso::run_main(problem, cfg, argc, argv, [](const gso::Result<MyProblem>& result) {
    std::cout << "best cost: " << result.best_cost << "\n";
    // TODO: save result.best if you need the solution itself
  });
}
