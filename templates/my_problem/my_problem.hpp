#pragma once
// GSO-RL adapter skeleton. Fill in every TODO, in order (parts 1 to 4). Then:
//   ./my_problem <instance> --check    checks the adapter and suggests parameter values
//   ./my_problem <instance>            solves
// Until a part is written, running stops with a message naming it.
// A complete adapter with every part written: examples/knapsack/knapsack.hpp

#include <gso/gso.hpp>

#include <stdexcept>
#include <string>
#include <vector>

struct MyProblem {
  // -- Instance ---------------------------------------------------------------------------------
  // TODO: the data of your problem (distances, demands, capacities...), read from the file.
  explicit MyProblem(const std::string& instance_path) {
    (void)instance_path;  // TODO: open the file and fill the data members
  }

  // -- 1. Solution and cost ------------------------------------------------------------------------
  // Any copyable type. Keeping the cost inside avoids recomputing it.
  struct Solution {
    // TODO: your representation (routes, a permutation, selected items...)
    double cost = 0;
  };

  // Minimized. For a maximization problem, return the negated objective.
  double cost(const Solution& s) const { return s.cost; }

  // -- 2. Guide generator ----------------------------------------------------------------------------
  // Builds one good starting solution: a randomized construction and a local search. It is called
  // gcm.size times and must give different solutions; use only `rng` for random decisions.
  Solution make_guide(gso::Rng& rng) {
    (void)rng;
    throw std::logic_error("TODO: write MyProblem::make_guide (templates/my_problem/my_problem.hpp, part 2)");
  }

  // -- 3. Dissimilarity -------------------------------------------------------------------------------
  // How structurally different two solutions are: d(a, a) = 0, d >= 0, symmetric.
  // Normalizing to [0, 1] is recommended; region.delta_max is in the same units.
  double distance(const Solution& a, const Solution& b) const {
    (void)a;
    (void)b;
    throw std::logic_error("TODO: write MyProblem::distance (templates/my_problem/my_problem.hpp, part 3)");
  }

  // -- 4. Macro-moves --------------------------------------------------------------------------------
  // The actions the agents choose between: at least two, with different effects. Mark with
  // gso::restart the ones that build a solution unrelated to the current one.
  gso::MacroMoves<MyProblem> macro_moves() {
    return {
        {"move_a", &MyProblem::move_a},
        {"move_b", &MyProblem::move_b},
        // {"rebuild", &MyProblem::rebuild, gso::restart},
    };
  }

  // A macro-move changes `s` in place. Through ctx: ctx.rng(), ctx.guide(), ctx.best(),
  // ctx.offer(s) (report a solution visited on the way), ctx.out_of_budget().
  void move_a(Solution& s, gso::Context<MyProblem>& ctx) {
    (void)s;
    (void)ctx;
    throw std::logic_error("TODO: write MyProblem::move_a (templates/my_problem/my_problem.hpp, part 4)");
  }

  void move_b(Solution& s, gso::Context<MyProblem>& ctx) {
    (void)s;
    (void)ctx;
    throw std::logic_error("TODO: write MyProblem::move_b (templates/my_problem/my_problem.hpp, part 4)");
  }

  // -- Optional: your own parameters --------------------------------------------------------------------
  // They appear in --help and in the printed configuration as problem.<name>, and can be set like
  // any other parameter (--problem.steps=100).
  //
  // int steps = 50;
  // void parameters(gso::Params& p) { p.add("steps", steps, "steps of move_b", ">= 1"); }
};
