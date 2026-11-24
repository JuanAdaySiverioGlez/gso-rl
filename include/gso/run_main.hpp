#pragma once
// gso::run_main: everything a main() needs, in one call.
//
//   int main(int argc, char** argv) {
//     MyProblem problem(argv[1]);
//     gso::Config cfg;
//     cfg.budget.seconds = 60;
//     cfg.region.delta_max = 0.3;
//     return gso::run_main(problem, cfg, argc, argv);
//   }
//
// Command line:
//   --help            every parameter, with default and meaning
//   --check           check the adapter (gso::check_adapter) instead of solving
//   --preset=<name>   --config=<file>   --name=value     see gso::configure
// Configuration errors and exceptions thrown by the adapter are printed, and main returns 1.

#include <cstddef>
#include <exception>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>

#include "check.hpp"
#include "solver.hpp"

namespace gso {

// on_result (optional): called with the Result of the run, e.g. to save the best solution.
// Without it, the best cost is printed.
template <class P, class OnResult = std::nullptr_t>
int run_main(P& problem, Config cfg, int argc, char** argv, OnResult on_result = nullptr) {
  bool check = false, help = false;
  std::vector<char*> args;
  for (int i = 0; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--check") check = true;
    else args.push_back(argv[i]);
    help = help || a == "--help" || a == "-h";
  }
  try {
    if (help)
      std::cout << "Options:\n"
                << "  --check                      check the adapter and suggest parameter values, then exit\n"
                << "  --preset=<name>              start from a preset: eswa2027_experiments, eswa2027_text_large, eswa2027_text_small\n"
                << "  --config=<file>              read 'name = value' lines\n"
                << "  --name=value                 set any parameter below\n\n";
    if (!configure(cfg, problem, static_cast<int>(args.size()), args.data())) return 0;
    if (check) return check_adapter(problem, cfg).ok() ? 0 : 1;

    Solver<P> solver(problem, cfg);
    const Result<P> result = solver.run();
    if constexpr (std::is_same_v<OnResult, std::nullptr_t>) {
      std::cout << "best cost: " << detail::format_number(result.best_cost) << "\n";
    } else {
      on_result(result);
    }
    return 0;
  } catch (const ConfigError& e) {
    std::cerr << "configuration error: " << e.what() << "\n";
  } catch (const std::exception& e) {
    std::cerr << "error: " << e.what() << "\n";
  }
  return 1;
}

}  // namespace gso
