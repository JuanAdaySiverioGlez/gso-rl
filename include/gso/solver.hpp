#pragma once
// The main loop of GSO-RL (docs/02-framework.md, Section 2.2), the Context seen by macro-moves,
// and gso::configure (configuration from the command line).

#include <chrono>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "agents.hpp"
#include "ccm.hpp"
#include "config.hpp"
#include "gcm.hpp"
#include "guide_selection.hpp"
#include "params.hpp"
#include "presets.hpp"
#include "problem.hpp"
#include "rewards.hpp"
#include "rng.hpp"
#include "stats.hpp"

namespace gso {

template <class P>
class Checker;  // check.hpp

template <class P>
struct Result {
  typename P::Solution best;     // best solution found
  double best_cost;              // its cost
  Stats stats;                   // what happened during the run
  std::vector<TracePoint> trace; // every improvement of the best solution
};

template <class P>
class Solver {
  static_assert(detail::check_adapter_types<P>(), "see the messages above");

 public:
  using Solution = typename P::Solution;

  // Checks the configuration and the macro-moves. Throws ConfigError with a clear message.
  Solver(P& problem, const Config& cfg) : problem_(problem), cfg_(cfg) {
    cfg_.validate();
    moves_ = problem_.macro_moves();
    if (moves_.size() < 2)
      throw ConfigError("the adapter provides " + std::to_string(moves_.size()) +
                        " macro-move(s); GSO-RL needs at least two to choose between");
    for (const auto& m : moves_) names_.push_back(m.name);
    agent_spec_ = Spec::parse(cfg_.agent.type);
    make_agent(agent_spec_, names_);  // fails now, not in the middle of a run, if the spec is wrong
    reward_ = make_reward(Spec::parse(cfg_.reward.type));
    select_guide_ = make_guide_selection(Spec::parse(cfg_.search.guide_selection));
    decay_lambda_ = parse_on_replace(cfg_.agent.on_replace);
    agent_of_start_ = cfg_.search.agent_of == "start";
    schedule_all_ = cfg_.search.schedule == "all";
    {
      const Spec sat = Spec::parse(cfg_.region.on_saturation);
      replace_saturated_ = sat.name == "replace";
      replace_other_ = sat.name == "replace_other";
      replace_random_p_ = sat.name == "replace_random" ? sat.number("p", 0) : 0.0;
    }
    // gcm.tenure_unit = budget: the tenure is a fraction of the run's budget, measured in its unit.
    tenure_in_seconds_ = cfg_.gcm.tenure_unit == "seconds";
    tenure_ = cfg_.gcm.tenure;
    if (cfg_.gcm.tenure_unit == "budget") {  // the unit of the budget: seconds, else iterations, else macro-moves
      tenure_in_seconds_ = cfg_.budget.seconds > 0;
      tenure_in_moves_ = !tenure_in_seconds_ && cfg_.budget.iterations <= 0;
      tenure_ *= tenure_in_seconds_ ? cfg_.budget.seconds
                 : tenure_in_moves_ ? static_cast<double>(cfg_.budget.macro_moves)
                                    : static_cast<double>(cfg_.budget.iterations);
    }
  }

  const Config& config() const { return cfg_; }
  const MacroMoves<P>& macro_moves() const { return moves_; }

  // Prints every parameter (framework and adapter) with its current value.
  void print_configuration(std::ostream& os) {
    Config shown = cfg_;
    Params p;
    shown.bind(p);
    bind_problem_parameters(problem_, p);
    os << "# GSO-RL configuration\n# macro-moves:";
    for (const auto& m : moves_) os << " " << m.name << (m.is_restart ? " (restart)" : "");
    os << "\n";
    p.print(os);
  }

  Result<P> run() { return run(cfg_.seed); }

  Result<P> run(std::uint64_t seed) {
    if (cfg_.output.print_config) print_configuration(std::cout);
    start(seed);

    CCM ccm(cfg_.ccm.size);
    while (!out_of_budget()) {
      if (schedule_all_) {
        // search.schedule = all: every guide runs its own regional search, in slot order (one loop
        // per guide). Each regional search counts as one iteration of the budget.
        for (int slot = 0; slot < gcm_->size() && !out_of_budget(); ++slot) regional_search(slot, ccm);
        continue;
      }
      // Phase 1: region selection
      const int first = select_guide_(gcm_->views(), rng_);
      if (first < 0 || first >= gcm_->size())
        throw std::logic_error("search.guide_selection '" + cfg_.search.guide_selection + "' chose guide " +
                               std::to_string(first) + ", but there are " + std::to_string(gcm_->size()));
      regional_search(first, ccm);
    }
    return finish();
  }

 private:
  // Phase 2: regional search from the guide in slot `first` (search.region_iters macro-moves).
  void regional_search(int first, CCM& ccm) {
    gcm_->visit(first);
    Solution s = gcm_->guide(first).solution;
    double f = gcm_->guide(first).cost;
    // Saturation (statistics): searches in a row from this guide that did not improve it.
    const double f_start = f;
    const std::uint64_t id_before = gcm_->guide(first).id;
    const double best_start = best_cost_;
    if (id_before != streak_id_[first]) streak_id_[first] = id_before, streak_[first] = 0;
    const bool saturated = streak_[first] >= cfg_.region.saturation;
    if (saturated) ++stats_.searches_saturated;

    for (int j = 0; j < cfg_.search.region_iters; ++j) {
      Location where = gcm_->locate_and_update(s, f, now_for_tenure(), first);
      ++stats_.cases[static_cast<int>(where.kase)];
      if (agent_of_start_) {  // the agent of the guide this regional search started from decides
        where.slot = first;
        where.ring = gcm_->ring_in(first, s);
      }

      const int who = gcm_->agent_index(where.slot, where.ring, rng_);
      const int m = gcm_->agent_at(who).select(rng_);
      if (m < 0 || m >= static_cast<int>(moves_.size()))
        throw std::logic_error("agent '" + cfg_.agent.type + "' chose macro-move " + std::to_string(m) +
                               ", but there are " + std::to_string(moves_.size()));
      ++stats_.chosen[m];
      if (saturated) ++stats_.chosen_saturated[m];
      ++stats_.chosen_by_ring[static_cast<std::size_t>(where.ring) * moves_.size() + m];

      // A restart leaves the trajectory: pending decisions are rewarded with its last state.
      if (moves_[m].is_restart) ccm.flush(settle_at(f));

      const Guide<Solution>& g = gcm_->guide(where.slot);
      Decision decision{g.id, where.slot, where.ring, m, f, best_cost_, g.cost};
      decision.agent = who;

      Context<P> ctx(*this, where.slot);
      const auto t0 = Clock::now();
      moves_[m].apply(problem_, s, ctx);
      stats_.seconds_in_moves += seconds_since(t0);
      ++stats_.macro_moves;

      f = problem_.cost(s);
      improve(s, f);
      ccm.push(decision, settle_at(f));

      if (out_of_budget()) break;  // a macro-move is never interrupted
    }
    // The next regional search starts from another guide: settle what is pending now.
    ccm.flush(settle_at(f));
    ++iteration_;
    const auto& g = gcm_->guide(first);
    // Value of the region: did this search improve its guide or the best solution? (A guide that
    // was replaced is a new region and starts without a record.)
    if (g.id == id_before)
      gcm_->record_search(first, g.cost < f_start || best_cost_ < best_start, cfg_.search.value_discount);
    if (g.id != id_before || g.cost < f_start) {  // improved (drift), renewed or replaced: streak ends
      streak_[first] = 0;
      streak_id_[first] = g.id;
    } else {
      stats_.longest_streak = std::max(stats_.longest_streak, ++streak_[first]);
    }
    // region.on_saturation = replace: abandon the saturated region; a new guide takes its slot.
    if (replace_saturated_ && streak_[first] >= cfg_.region.saturation) abandon(first);
    // Controls of replace (not part of the method): they abandon regions as replace does, but not
    // the saturated one. replace_other: at the same moment, another region at random; the saturated
    // region keeps its guide and its count starts again, so the regions are abandoned as often as
    // with replace. replace_random(p): a region at random with probability p, whatever its state.
    if (replace_other_ && streak_[first] >= cfg_.region.saturation) {
      streak_[first] = 0;
      const int n = gcm_->size();
      int other = first;
      if (n > 1) {
        other = rng_.uniform_int(0, n - 2);
        if (other >= first) ++other;
      }
      abandon(other);
    }
    if (replace_random_p_ > 0 && rng_.bernoulli(replace_random_p_)) abandon(rng_.uniform_int(0, gcm_->size() - 1));
  }

  // The region of `slot` is abandoned: a new guide from the generator takes its slot.
  void abandon(int slot) {
    Solution fresh = problem_.make_guide(rng_);
    const double fc = problem_.cost(fresh);
    improve(fresh, fc);
    gcm_->renew(slot, fresh, fc, now_for_tenure());
    streak_[slot] = 0;
    streak_id_[slot] = gcm_->guide(slot).id;
    ++stats_.saturated_replaced;
  }

  using Clock = std::chrono::steady_clock;
  friend class Context<P>;
  friend class Checker<P>;

  static double parse_on_replace(const std::string& text) {
    const Spec spec = Spec::parse(text);
    if (spec.name == "reset") {
      spec.allow_only({});
      return -1.0;
    }
    if (spec.name == "decay") {
      spec.allow_only({"lambda"});
      const double lambda = spec.number("lambda", 0.5);
      if (lambda < 0 || lambda > 1) throw ConfigError("agent.on_replace: decay(lambda) needs lambda in [0, 1]");
      return lambda;
    }
    throw ConfigError("agent.on_replace: unknown policy '" + spec.name + "'. Available: reset, decay(lambda)");
  }

  void start(std::uint64_t seed) {
    rng_.reseed(seed);
    start_ = Clock::now();
    iteration_ = 0;
    trace_.clear();
    stats_ = Stats{};
    stats_.moves = names_;
    for (const auto& m : moves_) stats_.restart.push_back(m.is_restart);
    stats_.rings = cfg_.region.rings;
    stats_.chosen.assign(moves_.size(), 0);
    stats_.chosen_by_ring.assign(moves_.size() * static_cast<std::size_t>(cfg_.region.rings), 0);
    stats_.saturation = cfg_.region.saturation;
    stats_.chosen_saturated.assign(moves_.size(), 0);

    gcm_.reset();
    Config memory = cfg_;
    memory.gcm.tenure = tenure_;  // in the resolved unit (see the constructor)
    gcm_.emplace(problem_, memory, [this] { return make_agent(agent_spec_, names_); }, decay_lambda_);
    gcm_->initialize(rng_, now_for_tenure());
    stats_.delta_max = gcm_->delta_max();
    streak_.assign(static_cast<std::size_t>(gcm_->size()), 0);
    streak_id_.assign(static_cast<std::size_t>(gcm_->size()), 0);
    stats_.seconds_building_guides = elapsed();

    const auto& g = gcm_->guide(gcm_->best_slot());
    best_.reset();
    best_cost_ = std::numeric_limits<double>::infinity();
    improve(g.solution, g.cost);
  }

  Result<P> finish() {
    stats_.iterations = iteration_;
    stats_.seconds = elapsed();
    if (cfg_.output.print_summary) {
      std::cout << "# GSO-RL finished: best cost " << detail::format_number(best_cost_) << "\n";
      stats_.print(std::cout);
    }
    return Result<P>{std::move(*best_), best_cost_, stats_, std::move(trace_)};
  }

  // Rewards one decision with the current state, unless its guide has been replaced.
  auto settle_at(double f_now) {
    return [this, f_now](const Decision& d) {
      if (gcm_->guide(d.slot).id != d.guide_id) {  // contextual validation: its guide was replaced
        ++stats_.rewards_discarded;
        return;
      }
      const double r = reward_(RewardInput{d.cost_before, d.best_before, d.guide_cost_before, f_now, best_cost_});
      gcm_->agent_at(d.agent).update(d.move, r);
      ++stats_.rewards_paid;
      stats_.reward_abs_sum += r < 0 ? -r : r;
    };
  }

  void improve(const Solution& s, double f) {
    if (f < best_cost_) {
      best_ = s;
      best_cost_ = f;
      trace_.push_back(TracePoint{elapsed(), iteration_, f});
    }
  }

  bool out_of_budget() const {
    return (cfg_.budget.iterations > 0 && iteration_ >= cfg_.budget.iterations) ||
           (cfg_.budget.macro_moves > 0 && stats_.macro_moves >= cfg_.budget.macro_moves) ||
           (cfg_.budget.seconds > 0 && elapsed() >= cfg_.budget.seconds);
  }

  double now_for_tenure() const {
    if (tenure_in_seconds_) return elapsed();
    return tenure_in_moves_ ? static_cast<double>(stats_.macro_moves) : static_cast<double>(iteration_);
  }

  double elapsed() const { return seconds_since(start_); }
  static double seconds_since(Clock::time_point t) {
    return std::chrono::duration<double>(Clock::now() - t).count();
  }

  P& problem_;
  Config cfg_;
  MacroMoves<P> moves_;
  std::vector<std::string> names_;
  Spec agent_spec_;
  RewardFn reward_;
  GuideSelection select_guide_;
  double decay_lambda_ = -1;
  bool agent_of_start_ = false;
  bool schedule_all_ = false;
  bool replace_saturated_ = false;
  bool replace_other_ = false;       // control: region.on_saturation = replace_other
  double replace_random_p_ = 0.0;    // control: region.on_saturation = replace_random(p)
  std::vector<long long> streak_;          // per slot: regional searches in a row without improving its guide
  std::vector<std::uint64_t> streak_id_;   // id of the guide the streak refers to
  bool tenure_in_seconds_ = false;
  bool tenure_in_moves_ = false;  // gcm.tenure_unit = budget with a budget in macro-moves only
  double tenure_ = 0;

  // State of the current run
  Rng rng_;
  std::optional<GCM<P>> gcm_;
  std::optional<Solution> best_;
  double best_cost_ = 0;
  long long iteration_ = 0;
  Clock::time_point start_;
  Stats stats_;
  std::vector<TracePoint> trace_;
};

// What a macro-move can see and do while it runs.
template <class P>
class Context {
 public:
  using Solution = typename P::Solution;

  Context(Solver<P>& solver, int slot) : solver_(solver), slot_(slot) {}

  // Random generator of the run: use it for every random decision (reproducibility).
  Rng& rng() { return solver_.rng_; }

  // Guide of the region where this macro-move was chosen, and its cost.
  const Solution& guide() const { return solver_.gcm_->guide(slot_).solution; }
  double guide_cost() const { return solver_.gcm_->guide(slot_).cost; }

  // All guides of the General Context Memory (read-only), e.g. for path relinking.
  int guides() const { return solver_.gcm_->size(); }
  const Solution& guide(int i) const { return solver_.gcm_->guide(i).solution; }
  double guide_cost(int i) const { return solver_.gcm_->guide(i).cost; }

  // Best solution found so far.
  const Solution& best() const { return *solver_.best_; }
  double best_cost() const { return solver_.best_cost_; }

  // Report a solution visited along the way. If it is the best so far, it is kept even if the
  // macro-move finishes somewhere worse.
  void offer(const Solution& s) { solver_.improve(s, solver_.problem_.cost(s)); }
  void offer(const Solution& s, double cost) { solver_.improve(s, cost); }

  // True when the budget is exhausted: long macro-moves can stop early.
  bool out_of_budget() const { return solver_.out_of_budget(); }

  long long iteration() const { return solver_.iteration_; }
  double seconds() const { return solver_.elapsed(); }

 private:
  Solver<P>& solver_;
  int slot_;
};

// Reads the command line into cfg and into the adapter's own parameters:
//   --preset=<name>   apply a preset first (only the parameters it defines)
//   --config=<file>   lines "name = value"
//   --name=value      any parameter (also: --name value)
//   --help            list every parameter and return false (the program should exit)
// Arguments without "--" (for example an instance file) are returned in `positional`.
template <class P>
bool configure(Config& cfg, P& problem, int argc, char** argv, std::vector<std::string>* positional = nullptr) {
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg.rfind("--preset=", 0) == 0) presets::apply(arg.substr(9), cfg);
    else if (arg == "--preset" && i + 1 < argc) presets::apply(argv[i + 1], cfg);
  }
  Params p;
  cfg.bind(p);
  bind_problem_parameters(problem, p);
  return p.load_args(argc, argv, positional);
}

}  // namespace gso
