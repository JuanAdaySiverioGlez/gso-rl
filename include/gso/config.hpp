#pragma once
// Config: every parameter of the framework, in one place.
//
// The defaults are the configuration that performed best across four problems (knapsack, SCC-VRP,
// p-median, TSP; see docs/05-configuration.md): the system used in the article's experiments, with
// Thompson sampling and the Eq. 3 reward. The article's own configurations are presets.
// docs/05-configuration.md explains each parameter; `--help` prints this same table.

#include <cstdint>
#include <string>

#include "params.hpp"

namespace gso {

struct Config {
  // -- Budget: the run stops when the first limit is reached ---------------------------------
  struct Budget {
    long long iterations = 0;  // global iterations (regional searches); 0 = no limit
    double seconds = 0;        // wall-clock seconds; 0 = no limit
    long long macro_moves = 0;  // macro-moves applied; 0 = no limit (fair when search.region_iters varies)
  } budget;

  std::uint64_t seed = 1;  // run r of an experiment should use seed + r

  // -- General Context Memory (GCM) -----------------------------------------------------------
  struct Gcm {
    int size = 15;                          // number of guides |G|
    double elite_ratio = 0.70;              // share of guides admitted for quality (gcm.update = cases)
    double tenure = 1.0;                    // lifetime of a guide before it can be replaced (1 x budget: never)
    std::string tenure_unit = "budget";     // "iterations", "seconds", or "budget" (a fraction of the budget)
    std::string update = "expiry_first";    // memory update rule: "cases" | "expiry_first"
    std::string case_a = "drift";           // a guide improved in its own region: "renew" | "drift"
  } gcm;

  // -- Regions ----------------------------------------------------------------------------------
  struct Region {
    double delta_max = -1;     // radius of a region, in the units of d(A, B); if set, it is used
    double delta_ratio = 0.5;  // otherwise: radius as a fraction of a reference distance
    std::string delta_reference = "guides";  // that reference: "guides" | "adapter"
    int rings = 1;             // concentric rings per region; one agent per ring (1 = one agent per guide)
    int saturation = 5;        // saturated: this many regional searches in a row without improving its guide
    // none | replace (a saturated region is abandoned for a new guide) | controls of replace:
    // replace_other (another region, at random, is abandoned instead) | replace_random(p) (after
    // each regional search, a region at random is abandoned with probability p)
    std::string on_saturation = "none";
  } region;

  // -- Regional search --------------------------------------------------------------------------
  struct Search {
    int region_iters = 10;                           // macro-moves per visit to a region
    std::string guide_selection = "ucb(c=1.4)";  // which guide a regional search starts from
    std::string agent_of = "start";  // whose agent decides: the start guide, or the guide closest to S
    std::string schedule = "select";  // select: one guide per iteration | all: every guide in turn
    double value_discount = 0.8;  // weight of earlier regional searches in the value of a region
  } search;

  // -- Learning agents ----------------------------------------------------------------------------
  struct Agent {
    std::string type = "thompson";               // agent of every ring
    std::string on_replace = "decay(lambda=0.5)";  // when a guide is replaced: reset | decay(lambda)
    // region: one agent per ring of each guide (the method). Controls: global (one agent for the
    // whole search) and shuffled (as many agents as with region, each decision taken by one of them
    // drawn at random: the same experience per agent, but no link between an agent and a region)
    std::string scope = "region";
  } agent;

  // -- Current Context Memory (CCM) ---------------------------------------------------------------
  struct Ccm {
    int size = 2;  // decisions waiting for their reward; 0 = immediate reward
  } ccm;

  // -- Reward -------------------------------------------------------------------------------------
  struct Reward {
    std::string type = "eq3(alpha=0.75)";  // Eq. 3 of the article
  } reward;

  // -- Output ---------------------------------------------------------------------------------------
  struct Output {
    bool print_config = true;   // print the full configuration when a run starts
    bool print_summary = true;  // print a summary when a run ends
  } output;

  // The table of parameters: name, field, help, range. Everything else (help, file and
  // command-line reading, printing) is generated from it.
  template <class Self, class F>
  static void each(Self& c, F&& f) {
    f("budget.iterations", c.budget.iterations, "stop after this many global iterations (0 = no limit)", ">= 0");
    f("budget.seconds", c.budget.seconds, "stop after this many seconds (0 = no limit)", ">= 0");
    f("budget.macro_moves", c.budget.macro_moves, "stop after this many macro-moves (0 = no limit); the deterministic budget to use when search.region_iters is tuned", ">= 0");
    f("seed", c.seed, "random seed of the run", "");
    f("gcm.size", c.gcm.size, "number of guides |G| in the General Context Memory", ">= 1");
    f("gcm.elite_ratio", c.gcm.elite_ratio, "share of guides kept for quality (elite zone); the rest keep diversity", "[0, 1]");
    f("gcm.tenure", c.gcm.tenure, "lifetime of a guide before it can be replaced (diversity guides only with update=cases)", "> 0");
    f("gcm.tenure_unit", c.gcm.tenure_unit, "unit of gcm.tenure; budget = fraction of the run's budget", "iterations | seconds | budget");
    f("gcm.update", c.gcm.update, "memory update rule: cases A-E with zones, or expired guides replaced first", "cases | expiry_first");
    f("gcm.case_a", c.gcm.case_a, "case A (better solution in a region): renew = new guide (pending rewards discarded, agents follow agent.on_replace); drift = same region moved (rewards and agents kept)", "renew | drift");
    f("region.delta_max", c.region.delta_max, "radius of a region, in the units of your distance d(A,B); -1 = use delta_ratio", ">= 0 or -1");
    f("region.delta_ratio", c.region.delta_ratio, "radius of a region as a fraction of region.delta_reference", "(0, 1]");
    f("region.delta_reference", c.region.delta_reference, "reference distance of delta_ratio: largest distance between the initial guides, or the adapter's max_distance()", "guides | adapter");
    f("region.rings", c.region.rings, "concentric rings per region, one agent each", ">= 1");
    f("region.saturation", c.region.saturation, "a region counts as saturated after this many regional searches in a row from its guide that did not improve it (reported in the summary)", ">= 1");
    f("region.on_saturation", c.region.on_saturation, "what happens to a saturated region: none, or replace (abandoned: a new guide from the generator takes its slot; the best solution is kept). Controls that measure what knowing which region is saturated adds: replace_other (at the same moment, another region chosen at random is abandoned instead) and replace_random(p) (after each regional search, a region chosen at random is abandoned with probability p, whatever its state)", "none | replace | replace_other | replace_random(p)");
    f("search.region_iters", c.search.region_iters, "macro-moves per visit to a region", ">= 1");
    f("search.guide_selection", c.search.guide_selection, "how the starting guide is chosen", "geometric(p) | ucb(c) | uniform | best | value(a, b)");
    f("search.value_discount", c.search.value_discount, "value of a region (guide_selection = value): each regional search from it multiplies the earlier outcomes by this (1 = plain counts)", "(0, 1]");
    f("search.agent_of", c.search.agent_of, "whose agent decides each macro-move: the guide closest to S, or the guide the search started from", "closest | start");
    f("search.schedule", c.search.schedule, "select: each iteration, one guide chosen by search.guide_selection; all: every guide runs its own regional search in turn (a loop per guide; guide_selection unused)", "select | all");
    f("agent.type", c.agent.type, "learning agent of each ring", "ucb1(c) | epsilon_greedy(eps) | softmax(tau) | thompson | exp3(gamma) | sliding_window_ucb(c, window) | discounted_ucb(c, gamma) | gradient(alpha) | random | fixed(move)");
    f("agent.on_replace", c.agent.on_replace, "what the agents of a replaced guide keep", "reset | decay(lambda)");
    f("agent.scope", c.agent.scope, "region: one agent per ring of each guide (regional learning). Controls: global (a single agent for the whole search) and shuffled (as many agents as with region, but each decision is taken by one of them drawn at random and its reward goes to the same one: the same amount of experience per agent, unrelated to the regions)", "region | global | shuffled");
    f("ccm.size", c.ccm.size, "decisions waiting in the Current Context Memory (0 = immediate reward)", ">= 0");
    f("reward.type", c.reward.type, "reward of a decision", "eq3(alpha, beta) | levels(best, guide, current)");
    f("output.print_config", c.output.print_config, "print the configuration when a run starts", "true | false");
    f("output.print_summary", c.output.print_summary, "print a summary when a run ends", "true | false");
  }

  // Registers every parameter in `p`, with the library defaults as reference.
  void bind(Params& p) {
    static const Config defaults{};
    std::map<std::string, std::string> fallback;
    each(defaults, [&](const char* name, const auto& field, const char*, const char*) {
      fallback[name] = detail::to_text(field);
    });
    each(*this, [&](const char* name, auto& field, const char* help, const char* range) {
      p.add(name, field, help, range, fallback[name]);
    });
  }

  // Checks values that do not depend on the problem. Throws ConfigError with a clear message.
  void validate() const {
    auto require = [](bool ok, const std::string& message) {
      if (!ok) throw ConfigError(message);
    };
    require(budget.iterations >= 0 && budget.seconds >= 0 && budget.macro_moves >= 0,
            "budget.iterations, budget.seconds and budget.macro_moves must be >= 0");
    require(budget.iterations > 0 || budget.seconds > 0 || budget.macro_moves > 0,
            "no budget: set budget.iterations, budget.seconds or budget.macro_moves (the first limit reached stops the run)");
    require(gcm.size >= 1, "gcm.size must be >= 1 (value: " + std::to_string(gcm.size) + ")");
    require(gcm.elite_ratio >= 0 && gcm.elite_ratio <= 1, "gcm.elite_ratio must be in [0, 1]");
    require(gcm.tenure > 0, "gcm.tenure must be > 0");
    require(gcm.tenure_unit == "iterations" || gcm.tenure_unit == "seconds" || gcm.tenure_unit == "budget",
            "gcm.tenure_unit must be 'iterations', 'seconds' or 'budget' (value: '" + gcm.tenure_unit + "')");
    require(gcm.tenure_unit != "budget" || gcm.tenure <= 1,
            "with gcm.tenure_unit = budget, gcm.tenure is a fraction of the budget: it must be in (0, 1]");
    require(gcm.update == "cases" || gcm.update == "expiry_first",
            "gcm.update must be 'cases' or 'expiry_first' (value: '" + gcm.update + "')");
    require(gcm.case_a == "renew" || gcm.case_a == "drift",
            "gcm.case_a must be 'renew' or 'drift' (value: '" + gcm.case_a + "')");
    require(agent.scope == "region" || agent.scope == "global" || agent.scope == "shuffled",
            "agent.scope must be 'region', 'global' or 'shuffled' (value: '" + agent.scope + "')");
    require(search.schedule == "select" || search.schedule == "all",
            "search.schedule must be 'select' or 'all' (value: '" + search.schedule + "')");
    require(search.agent_of == "closest" || search.agent_of == "start",
            "search.agent_of must be 'closest' or 'start' (value: '" + search.agent_of + "')");
    require(region.delta_max >= 0 || region.delta_ratio > 0,
            "the radius of a region is not set. Set region.delta_max (in the units of your distance d(A,B)) or\n"
            "  region.delta_ratio (a fraction of the largest distance between the initial guides).\n"
            "  Tip: run with --check (gso::check_adapter): it suggests a value from the distances between guides.");
    require(region.delta_ratio <= 0 || region.delta_ratio <= 1, "region.delta_ratio must be in (0, 1]");
    require(region.delta_reference == "guides" || region.delta_reference == "adapter",
            "region.delta_reference must be 'guides' or 'adapter' (value: '" + region.delta_reference + "')");
    require(region.rings >= 1, "region.rings must be >= 1 (value: " + std::to_string(region.rings) + ")");
    require(search.region_iters >= 1, "search.region_iters must be >= 1");
    require(search.value_discount > 0 && search.value_discount <= 1, "search.value_discount must be in (0, 1]");
    require(region.saturation >= 1, "region.saturation must be >= 1");
    {
      const Spec sat = Spec::parse(region.on_saturation);
      require(sat.name == "none" || sat.name == "replace" || sat.name == "replace_other" ||
                  sat.name == "replace_random",
              "region.on_saturation must be none, replace, replace_other or replace_random(p) (value: '" +
                  region.on_saturation + "')");
      if (sat.name == "replace_random") {
        sat.allow_only({"p"});
        const double p = sat.number("p", -1);
        require(p > 0 && p <= 1, "region.on_saturation: replace_random(p) needs p in (0, 1]");
      } else {
        sat.allow_only({});
      }
    }
    require(ccm.size >= 0, "ccm.size must be >= 0 (0 = immediate reward)");
  }
};

}  // namespace gso
