#pragma once
// gso::check_adapter: checks an adapter at run time and suggests parameter values
// (the checklist of docs/03-adapting-to-your-problem.md, automated).
//
//   gso::check_adapter(problem, cfg);        // in code
//   ./my_program <instance> --check          // with gso::run_main
//
// It builds guides, checks the cost and the distance on them, runs every macro-move a few times,
// checks reproducibility, and makes a short pilot run to judge region.delta_max, gcm.tenure and
// the scale of the rewards. Every line starts with [ok], [info], [warn] or [error].

#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "solver.hpp"

namespace gso {

struct CheckReport {
  int errors = 0;
  int warnings = 0;
  bool ok() const { return errors == 0; }
};

struct CheckOptions {
  int move_tries = 5;         // times each macro-move is applied
  int pilot_iterations = 10;  // global iterations of the pilot run (0 = no pilot run)
};

template <class P>
class Checker {
 public:
  using Solution = typename P::Solution;

  Checker(P& problem, const Config& cfg, const CheckOptions& options, std::ostream& os)
      : problem_(problem), cfg_(cfg), options_(options), os_(os) {}

  CheckReport run() {
    os_ << "# GSO-RL adapter check\n";
    Config base = cfg_;
    base.output.print_config = false;
    base.output.print_summary = false;
    delta_set_ = base.region.delta_max >= 0 || base.region.delta_ratio > 0;
    if (!delta_set_) {
      error("the radius of a region (region.delta_max or region.delta_ratio) is not set; a suggestion follows "
            "from the distances between guides");
      base.region.delta_max = 0;
    }
    if (base.budget.iterations <= 0 && base.budget.seconds <= 0 && base.budget.macro_moves <= 0) {
      warn("no budget set (budget.iterations, budget.seconds or budget.macro_moves): a run would not start");
      base.budget.iterations = 1;
    }

    Solver<P> solver(problem_, base);  // throws ConfigError if a component spec is wrong
    solver.start(cfg_.seed);
    const auto& gcm = *solver.gcm_;
    const int g = gcm.size();
    ok("built " + std::to_string(g) + " guides in " + seconds_text(solver.stats_.seconds_building_guides));

    check_costs(solver);
    check_guide_reproducibility();
    check_distances(solver);
    check_macro_moves(solver);
    if (delta_set_ && options_.pilot_iterations > 0) pilot_run(base);

    os_ << "# " << report_.errors << " error(s), " << report_.warnings << " warning(s)\n";
    return report_;
  }

 private:
  void line(const char* tag, const std::string& message) { os_ << tag << " " << message << "\n"; }
  void ok(const std::string& m) { line("[ok]   ", m); }
  void info(const std::string& m) { line("[info] ", m); }
  void warn(const std::string& m) {
    line("[warn] ", m);
    ++report_.warnings;
  }
  void error(const std::string& m) {
    line("[error]", m);
    ++report_.errors;
  }

  static std::string num(double v) { return detail::format_number(std::abs(v) < 1e-12 ? 0.0 : v); }
  static std::string short_num(double v) {
    std::ostringstream os;
    os << std::setprecision(3) << v;
    return os.str();
  }
  static std::string seconds_text(double s) {
    std::ostringstream os;
    if (s < 1e-3) os << std::fixed << std::setprecision(1) << s * 1e6 << " us";
    else if (s < 1) os << std::fixed << std::setprecision(2) << s * 1e3 << " ms";
    else os << std::fixed << std::setprecision(2) << s << " s";
    return os.str();
  }
  static double quantile(std::vector<double> v, double q) {
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[static_cast<std::size_t>(q * static_cast<double>(v.size() - 1) + 0.5)];
  }

  void check_costs(Solver<P>& solver) {
    const auto& gcm = *solver.gcm_;
    int bad = 0, unstable = 0;
    for (int i = 0; i < gcm.size(); ++i) {
      const auto& guide = gcm.guide(i);
      if (!std::isfinite(guide.cost)) ++bad;
      else if (problem_.cost(guide.solution) != guide.cost) ++unstable;
    }
    if (bad) error(std::to_string(bad) + " guide(s) have a cost that is NaN or infinite");
    if (unstable) warn("cost() gave different values for the same solution: it should be deterministic");
    if (!bad && !unstable) ok("cost() is finite and deterministic on every guide");
  }

  void check_guide_reproducibility() {
    Rng a(cfg_.seed), b(cfg_.seed);
    const Solution s1 = problem_.make_guide(a);
    const Solution s2 = problem_.make_guide(b);
    if (problem_.cost(s1) == problem_.cost(s2) && problem_.distance(s1, s2) == 0)
      ok("make_guide() is reproducible with the same seed");
    else
      error("make_guide() gave different solutions with the same seed: use only the rng it receives");
  }

  void check_distances(Solver<P>& solver) {
    const auto& gcm = *solver.gcm_;
    const int g = gcm.size();
    std::vector<double> d;
    int self = 0, negative = 0, asymmetric = 0, identical = 0, bounded_bad = 0, bounded_inexact = 0;
    const double inf = std::numeric_limits<double>::infinity();
    for (int i = 0; i < g; ++i) {
      const Solution& a = gcm.guide(i).solution;
      if (problem_.distance(a, a) != 0) ++self;
      for (int j = i + 1; j < g; ++j) {
        const Solution& b = gcm.guide(j).solution;
        const double ab = problem_.distance(a, b), ba = problem_.distance(b, a);
        if (ab < 0) ++negative;
        if (std::abs(ab - ba) > 1e-9 * std::max(1.0, std::abs(ab))) ++asymmetric;
        if (ab == 0) ++identical;
        d.push_back(ab);
        if constexpr (detail::has_bounded_distance<P, Solution>::value) {
          // limit >= d: the exact distance, the same number as distance(a, b)
          for (const double limit : {ab, std::nextafter(ab, inf), 2 * ab + 1, inf}) {
            const double v = problem_.distance(a, b, limit);
            if (std::abs(v - ab) > 1e-9 * std::max(1.0, ab)) ++bounded_bad;
            else if (v != ab) ++bounded_inexact;
          }
          // limit < d: any value > limit (just below d is where rounding errors show up)
          if (ab > 0)
            for (const double limit : {std::nextafter(ab, -inf), ab * (1 - 1e-12), ab * 0.999, ab / 2, 0.0})
              if (!(problem_.distance(a, b, limit) > limit)) ++bounded_bad;
        }
      }
    }
    if (self) error("distance(s, s) is not 0 for " + std::to_string(self) + " guide(s)");
    if (negative) error("distance() is negative for " + std::to_string(negative) + " pair(s) of guides");
    if (asymmetric) warn("distance(a, b) != distance(b, a) for " + std::to_string(asymmetric) + " pair(s)");
    if (bounded_bad)
      error("the bounded distance(a, b, limit) must return the exact distance when it is <= limit, and a value "
            "> limit otherwise (" + std::to_string(bounded_bad) + " violation(s))");
    if (bounded_inexact)
      warn("the bounded distance(a, b, limit) differs from distance(a, b) in the last digits (" +
           std::to_string(bounded_inexact) + " case(s)): compute both with the same formula, or runs may depend on "
           "the order in which guides are compared");
    if (!self && !negative && !asymmetric && !bounded_bad) ok("distance() is 0 on itself, non-negative and symmetric");
    if (identical)
      warn(std::to_string(identical) + " pair(s) of guides are identical: make_guide() should produce different "
           "solutions");
    if (d.empty()) return;

    const double q25 = quantile(d, 0.25), median = quantile(d, 0.5);
    info("distance between guides: min " + num(quantile(d, 0)) + ", 25% " + num(q25) + ", median " + num(median) +
         ", max " + num(quantile(d, 1)));
    if (!delta_set_) {
      const double largest = quantile(d, 1);
      info("suggestion: start with region.delta_max = " + short_num(q25) + " (25% of the guide pairs are closer)" +
           (largest > 0 ? ", or region.delta_ratio = " + short_num(q25 / largest) : std::string()) +
           "; then tune it with the pilot run of --check");
      return;
    }
    const double delta = gcm.delta_max();
    if (cfg_.region.delta_ratio > 0)
      info("region.delta_ratio = " + num(cfg_.region.delta_ratio) + " of the reference distance gives region.delta_max = " +
           num(delta));
    const double share = static_cast<double>(std::count_if(d.begin(), d.end(), [&](double x) { return x <= delta; })) /
                         static_cast<double>(d.size());
    info("region.delta_max = " + num(delta) + ": " + short_num(100 * share) + "% of the guide pairs are within one region");
    if (share > 0.5)
      warn("most guides lie within region.delta_max of each other: regions overlap. The median distance between "
           "guides is " + num(median));
  }

  void check_macro_moves(Solver<P>& solver) {
    auto& gcm = *solver.gcm_;
    const auto& moves = solver.moves_;
    std::vector<double> mean_time;
    for (std::size_t m = 0; m < moves.size(); ++m) {
      int changed = 0, bad = 0;
      double total = 0;
      for (int t = 0; t < options_.move_tries; ++t) {
        const int slot = static_cast<int>((m * options_.move_tries + t) % gcm.size());
        Solution s = gcm.guide(slot).solution;
        Context<P> ctx(solver, slot);
        const auto t0 = std::chrono::steady_clock::now();
        moves[m].apply(problem_, s, ctx);
        total += std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        const double f = problem_.cost(s);
        if (!std::isfinite(f)) ++bad;
        else if (f != gcm.guide(slot).cost || problem_.distance(s, gcm.guide(slot).solution) != 0) ++changed;
      }
      mean_time.push_back(total / options_.move_tries);
      std::string msg = "macro-move '" + moves[m].name + "'" + (moves[m].is_restart ? " (restart)" : "") + ": " +
                        seconds_text(mean_time.back()) + " per call, changed the solution in " +
                        std::to_string(changed) + "/" + std::to_string(options_.move_tries) + " tries";
      if (bad) error(msg + "; the cost became NaN or infinite " + std::to_string(bad) + " time(s)");
      else if (changed == 0) warn(msg + ": it never changed the solution");
      else ok(msg);

      // Same seed, same start: the result must be the same.
      double cost[2];
      for (int k = 0; k < 2; ++k) {
        solver.rng_.reseed(cfg_.seed + 1000 + m);
        Solution s = gcm.guide(0).solution;
        Context<P> ctx(solver, 0);
        moves[m].apply(problem_, s, ctx);
        cost[k] = problem_.cost(s);
      }
      if (cost[0] != cost[1])
        warn("macro-move '" + moves[m].name + "' gave different results with the same seed: it may use randomness "
             "outside ctx.rng(), or keep state between calls");
    }
    const auto [fast, slow] = std::minmax_element(mean_time.begin(), mean_time.end());
    if (*fast > 0 && *slow / *fast > 10)
      warn("the slowest macro-move takes " + short_num(*slow / *fast) +
           "x the time of the fastest: agents learn reward per decision, not per second (docs/03)");
  }

  void pilot_run(const Config& base) {
    Config pilot = base;
    pilot.budget.iterations = options_.pilot_iterations;
    pilot.budget.seconds = 0;
    pilot.budget.macro_moves = 0;
    if (base.gcm.tenure_unit == "budget") {  // keep the tenure the real run will have
      const bool seconds = base.budget.seconds > 0;
      const double iterations = base.budget.iterations > 0
                                    ? static_cast<double>(base.budget.iterations)
                                    : static_cast<double>(base.budget.macro_moves) / base.search.region_iters;
      pilot.gcm.tenure_unit = seconds ? "seconds" : "iterations";
      pilot.gcm.tenure = base.gcm.tenure * (seconds ? base.budget.seconds : iterations);
    }
    Solver<P> solver(problem_, pilot);
    const Result<P> r = solver.run(cfg_.seed);
    const Stats& st = r.stats;
    const double per_iteration = st.iterations > 0 ? st.seconds / static_cast<double>(st.iterations) : 0;
    info("pilot run: " + std::to_string(st.iterations) + " iterations in " + seconds_text(st.seconds) + " (" +
         seconds_text(per_iteration) + " per iteration)");

    long long total = 0;
    for (long long c : st.cases) total += c;
    auto pct = [&](long long c) { return total > 0 ? 100.0 * static_cast<double>(c) / static_cast<double>(total) : 0.0; };
    std::string cases = "GCM update cases:";
    for (int c = 0; c < 5; ++c) cases += std::string(" ") + char('A' + c) + " " + short_num(pct(st.cases[c])) + "%";
    info(cases);
    if (pct(st.cases[4]) > 50)
      warn("case E dominates: most solutions fall outside every region. Increase region.delta_max");
    else if (st.cases[0] + st.cases[2] + st.cases[3] == 0)
      warn("no guide was replaced (cases A, C, D never happened): region.delta_max may be too large");

    // Will diversity guides ever expire within the real budget?
    double budget_iterations = cfg_.budget.iterations > 0 ? static_cast<double>(cfg_.budget.iterations) : 1e300;
    if (cfg_.budget.seconds > 0 && per_iteration > 0)
      budget_iterations = std::min(budget_iterations, cfg_.budget.seconds / per_iteration);
    if (cfg_.budget.macro_moves > 0)
      budget_iterations = std::min(budget_iterations, static_cast<double>(cfg_.budget.macro_moves) / cfg_.search.region_iters);
    const bool in_seconds = cfg_.gcm.tenure_unit == "seconds";
    const double budget_in_unit = in_seconds ? budget_iterations * per_iteration : budget_iterations;
    if (cfg_.gcm.tenure_unit != "budget" && budget_iterations < 1e299 && cfg_.gcm.tenure >= budget_in_unit)
      warn("gcm.tenure = " + num(cfg_.gcm.tenure) + " " + cfg_.gcm.tenure_unit + ", but the budget allows about " +
           short_num(budget_in_unit) + " " + cfg_.gcm.tenure_unit + ": diversity guides will never expire (no case D)");

    // Scale of the rewards against UCB1's exploration term.
    const double reward = st.mean_abs_reward();
    info("mean |reward| = " + short_num(reward));
    const Spec agent = Spec::parse(cfg_.agent.type);
    if (agent.name == "ucb1" && reward > 0) {
      const double c = agent.number("c", 1.0);
      if (c / reward > 20)
        warn("UCB1's exploration constant c = " + num(c) + " is " + short_num(c / reward) +
             "x the typical reward: its choices will be close to random. Try agent.type=ucb1(c=" + short_num(reward) +
             ") or reward.type=levels (docs/04)");
    }
    std::string chosen = "macro-moves chosen:";
    for (std::size_t m = 0; m < st.moves.size(); ++m)
      chosen += " " + st.moves[m] + " " +
                short_num(st.macro_moves > 0 ? 100.0 * static_cast<double>(st.chosen[m]) / static_cast<double>(st.macro_moves) : 0) + "%";
    info(chosen);
    info("time in the GSO-RL core: " + short_num(100 * st.core_share()) + "%");
  }

  P& problem_;
  Config cfg_;
  CheckOptions options_;
  std::ostream& os_;
  CheckReport report_;
  bool delta_set_ = true;
};

// Checks the adapter and prints a report; see the top of this file.
template <class P>
CheckReport check_adapter(P& problem, const Config& cfg, const CheckOptions& options = {},
                          std::ostream& os = std::cout) {
  return Checker<P>(problem, cfg, options, os).run();
}

}  // namespace gso
