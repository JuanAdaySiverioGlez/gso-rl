// Tests of the GSO-RL core. No external framework: run the binary, it prints failures and returns
// a non-zero status if any check fails.

#include <array>
#include <gso/gso.hpp>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "knapsack/knapsack.hpp"

static int failures = 0;
static int checks = 0;

#define CHECK(cond)                                                                  \
  do {                                                                               \
    ++checks;                                                                        \
    if (!(cond)) {                                                                   \
      ++failures;                                                                    \
      std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK failed: " #cond "\n";    \
    }                                                                                \
  } while (0)

#define CHECK_THROWS(expr)                     \
  do {                                         \
    bool thrown = false;                       \
    try { expr; } catch (const gso::ConfigError&) { thrown = true; } \
    CHECK(thrown);                             \
  } while (0)

// A one-dimensional problem: solutions are numbers, cost = value, distance = |a - b|.
// The guides are given in advance, so every case of the GCM update can be reproduced.
struct Line {
  using Solution = double;
  std::vector<double> guides;
  std::size_t next = 0;
  double cost(const double& x) const { return x; }
  double make_guide(gso::Rng&) { return guides[next++ % guides.size()]; }
  double distance(const double& a, const double& b) const { return std::abs(a - b); }
  gso::MacroMoves<Line> macro_moves() {
    return {{"left", [](double& x, gso::Context<Line>&) { x -= 1; }},
            {"right", [](double& x, gso::Context<Line>&) { x += 1; }}};
  }
};

void test_rng() {
  gso::Rng a(42), b(42), c(43);
  bool same = true, different = false;
  for (int i = 0; i < 100; ++i) {
    const auto x = a.next(), y = b.next(), z = c.next();
    same = same && x == y;
    different = different || x != z;
  }
  CHECK(same);
  CHECK(different);
  double sum = 0;
  bool in_range = true;
  for (int i = 0; i < 20000; ++i) {
    sum += a.uniform();
    const auto k = a.below(7);
    in_range = in_range && k < 7;
  }
  CHECK(in_range);
  CHECK(std::abs(sum / 20000 - 0.5) < 0.02);
}

void test_spec_and_params() {
  const gso::Spec s = gso::Spec::parse(" ucb1( c = 1.4 ) ");
  CHECK(s.name == "ucb1");
  CHECK(s.number("c", 0) == 1.4);
  CHECK(gso::Spec::parse("reset").args.empty());
  CHECK_THROWS(gso::Spec::parse("ucb1(c=)"));
  CHECK_THROWS(gso::Spec::parse("ucb1(c=1"));
  CHECK_THROWS(gso::Spec::parse("ucb1(x=1)").allow_only({"c"}));

  gso::Config cfg;
  gso::Params p;
  cfg.bind(p);
  p.set("gcm.size", "20");
  p.set("gcm.update", "expiry_first");
  p.set("agent.type", "random");
  CHECK(cfg.gcm.size == 20);
  CHECK(cfg.gcm.update == "expiry_first");
  CHECK_THROWS(p.set("ccm.validate_context", "no"));  // removed: rewards of replaced guides are always discarded
  CHECK(cfg.agent.type == "random");
  CHECK_THROWS(p.set("gcm.size", "many"));
  CHECK_THROWS(p.set("gcm.sise", "3"));  // typo: unknown parameter
  try {
    p.set("gcm.sise", "3");
  } catch (const gso::ConfigError& e) {
    CHECK(std::string(e.what()).find("gcm.size") != std::string::npos);  // suggests the right name
  }

  {
    std::ofstream f("gso_test_params.cfg");
    f << "# a comment\nregion.delta_max = 0.25   # radius\n\nsearch.region_iters=7\n";
  }
  p.load_file("gso_test_params.cfg");
  CHECK(cfg.region.delta_max == 0.25);
  CHECK(cfg.search.region_iters == 7);
  std::remove("gso_test_params.cfg");

  std::ostringstream printed;
  p.print(printed);
  CHECK(printed.str().find("gcm.size = 20") != std::string::npos);
  CHECK(printed.str().find("(default: 15)") != std::string::npos);

  gso::Config defaults;
  defaults.budget.iterations = 10;
  defaults.validate();  // only the budget has to be set: the radius defaults to delta_ratio
  gso::Config unset = defaults;
  unset.region.delta_ratio = -1;
  CHECK_THROWS(unset.validate());  // neither delta_max nor delta_ratio
  unset.region.delta_max = 0.1;
  unset.validate();
  CHECK_THROWS(gso::presets::by_name("nope"));
  CHECK(gso::presets::eswa2027_text_small().gcm.size == 10);
  CHECK(gso::presets::eswa2027_experiments().gcm.update == "expiry_first");
  CHECK(gso::presets::eswa2027_experiments().gcm.case_a == "renew" && gso::presets::eswa2027_text_large().gcm.case_a == "renew");
  CHECK(gso::Config().gcm.case_a == "drift" && gso::Config().gcm.tenure == 1.0);
}

// Share of choices of the best arm on a 3-armed Bernoulli bandit (0.2, 0.5, 0.8), 3000 steps.
//   swap:  at step 1500 the best and worst arms exchange; measured on the last 500 steps
//   scale: rewards multiplied by 0.001
//   delay: each reward arrives 30 steps late (like the Current Context Memory)
double bandit_share(const std::string& spec, bool swap, double scale, int delay, std::uint64_t seed = 1) {
  gso::Rng rng(seed);
  auto agent = gso::make_agent(gso::Spec::parse(spec), {"a", "b", "c"});
  double p[3] = {0.2, 0.5, 0.8};
  std::vector<std::pair<int, double>> pending;
  int best = 0, counted = 0;
  for (int t = 0; t < 3000; ++t) {
    if (swap && t == 1500) std::swap(p[0], p[2]);
    const int m = agent->select(rng);
    const double r = (rng.bernoulli(p[m]) ? 1.0 : 0.0) * scale;
    if (!swap || t >= 2500) {
      best += m == ((swap && t >= 1500) ? 0 : 2);
      ++counted;
    }
    pending.emplace_back(m, r);
    if (static_cast<int>(pending.size()) > delay) {
      agent->update(pending.front().first, pending.front().second);
      pending.erase(pending.begin());
    }
  }
  return static_cast<double>(best) / counted;
}

void test_agents() {
  gso::Rng rng(1);
  // Every learning agent prefers the best arm (random: 1/3).
  const std::vector<std::pair<const char*, double>> stationary = {
      {"ucb1(c=1.0)", 0.8}, {"epsilon_greedy(eps=0.1)", 0.8}, {"softmax(tau=0.1)", 0.8}, {"thompson", 0.85},
      {"exp3(gamma=0.1)", 0.7}, {"sliding_window_ucb", 0.7}, {"discounted_ucb", 0.65}, {"gradient(alpha=0.1)", 0.65}};
  for (const auto& [spec, at_least] : stationary) {
    const double share = bandit_share(spec, false, 1.0, 0);
    if (!(share > at_least)) std::cerr << "  " << spec << ": " << share << "\n";
    CHECK(share > at_least);
  }
  CHECK(bandit_share("random", false, 1.0, 0) < 0.4);
  // Agents for changing rewards follow the swap.
  CHECK(bandit_share("sliding_window_ucb", true, 1.0, 0) > 0.7);
  CHECK(bandit_share("discounted_ucb", true, 1.0, 0) > 0.65);
  // Scale-free agents still learn when rewards are 1000 times smaller; UCB1 with c = 1 does not.
  for (const char* spec : {"thompson", "gradient", "exp3", "epsilon_greedy"}) CHECK(bandit_share(spec, false, 1e-3, 0) > 0.65);
  CHECK(bandit_share("ucb1(c=1.0)", false, 1e-3, 0) < 0.4);
  // Delayed rewards (30 steps) do not break the bandits.
  CHECK(bandit_share("ucb1(c=1.0)", false, 1.0, 30) > 0.8);
  CHECK(bandit_share("thompson", false, 1.0, 30) > 0.85);
  // Every agent survives reset and decay.
  for (const char* spec : {"ucb1", "epsilon_greedy", "softmax", "thompson", "exp3", "sliding_window_ucb",
                           "discounted_ucb", "gradient", "random"}) {
    auto agent = gso::make_agent(gso::Spec::parse(spec), {"a", "b", "c"});
    for (int t = 0; t < 50; ++t) agent->update(agent->select(rng), 1.0);
    agent->decay(0.5);
    for (int t = 0; t < 10; ++t) agent->update(agent->select(rng), 1.0);
    agent->reset();
    const int m = agent->select(rng);
    CHECK(m >= 0 && m < 3);
  }
  CHECK_THROWS(gso::make_agent(gso::Spec::parse("softmax(tau=0)"), {"a", "b"}));
  CHECK_THROWS(gso::make_agent(gso::Spec::parse("exp3(gamma=2)"), {"a", "b"}));
  auto fixed = gso::make_agent(gso::Spec::parse("fixed(move=b)"), {"a", "b", "c"});
  CHECK(fixed->select(rng) == 1);
  CHECK_THROWS(gso::make_agent(gso::Spec::parse("fixed(move=z)"), {"a", "b"}));
  CHECK_THROWS(gso::make_agent(gso::Spec::parse("ucb2"), {"a", "b"}));

  gso::UCB1 ucb(3, 1.0);
  for (int t = 0; t < 30; ++t) ucb.update(ucb.select(rng), 1.0);
  ucb.reset();  // after a reset every macro-move is untried again: each is chosen once, in random order
  std::vector<int> first = {ucb.select(rng), ucb.select(rng), ucb.select(rng)};
  std::sort(first.begin(), first.end());
  CHECK(first == std::vector<int>({0, 1, 2}));
  int starts_with_zero = 0;
  for (int t = 0; t < 300; ++t) {
    ucb.reset();
    starts_with_zero += ucb.select(rng) == 0;
  }
  CHECK(starts_with_zero > 60 && starts_with_zero < 140);  // about 1/3, not always the first
}

void test_rewards_and_selection() {
  const gso::RewardInput in{530, 498, 510, 505, 498};
  const double expected = 0.75 * (530.0 - 505) / 530 + 0.25 * (498.0 - 505) / 498;
  CHECK(std::abs(gso::make_reward(gso::Spec::parse("eq3(alpha=0.75)"))(in) - expected) < 1e-12);
  CHECK(gso::make_reward(gso::Spec::parse("levels"))(in) == 3);  // better than its guide (505 < 510)
  // 6/1/0 of the reference implementation: 6 new best, 1 improvement of the current solution, 0 otherwise
  const auto six_one_zero = gso::make_reward(gso::Spec::parse("levels(best=6, guide=0, current=1)"));
  CHECK(six_one_zero(in) == 1);
  CHECK(six_one_zero(gso::RewardInput{530, 498, 510, 490, 490}) == 6);
  CHECK(six_one_zero(gso::RewardInput{530, 498, 510, 540, 498}) == 0);

  gso::Rng rng(3);
  auto geometric = gso::make_guide_selection(gso::Spec::parse("geometric(p=0.25)"));
  std::vector<gso::GuideView> views;
  for (int i = 0; i < 15; ++i) views.push_back({100.0 - i, 0});  // slot 14 is the best guide
  int first = 0;
  bool in_range = true;
  for (int t = 0; t < 40000; ++t) {
    const int k = geometric(views, rng);
    first += k == 14;
    in_range = in_range && k >= 0 && k < 15;
  }
  CHECK(in_range);
  CHECK(std::abs(first / 40000.0 - 0.25 / (1 - std::pow(0.75, 15))) < 0.01);

  // ucb: the best guide while visits are equal; a guide never visited once the best is worn out.
  auto ucb = gso::make_guide_selection(gso::Spec::parse("ucb(c=1.4)"));
  std::vector<gso::GuideView> three = {{10, 0}, {12, 0}, {20, 0}};
  CHECK(ucb(three, rng) == 0);
  three = {{10, 50}, {12, 50}, {20, 0}};
  CHECK(ucb(three, rng) == 2);
  CHECK_THROWS(gso::make_guide_selection(gso::Spec::parse("ucb(c=-1)")));
}

void test_gcm_cases() {
  Line line;
  line.guides = {0, 10, 20, 30};
  gso::Config cfg;
  cfg.gcm.update = "cases";
  cfg.gcm.case_a = "renew";  // the article's reading of case A (the default is drift)
  cfg.gcm.tenure_unit = "iterations";
  cfg.gcm.size = 4;
  cfg.gcm.elite_ratio = 0.5;  // elite: 0 and 10; diversity: 20 and 30
  cfg.gcm.tenure = 100;
  cfg.region.delta_max = 3;
  cfg.region.rings = 3;
  gso::GCM<Line> gcm(line, cfg, [] { return std::make_unique<gso::RandomAgent>(2); }, -1);
  gso::Rng rng(1);
  gcm.initialize(rng, 0);

  auto loc = gcm.locate_and_update(11, 11, 0);  // close to 10, worse
  CHECK(loc.kase == gso::Case::B && loc.slot == 1 && loc.ring == 1);

  const auto old_id = gcm.guide(1).id;
  loc = gcm.locate_and_update(9, 9, 0);  // close to 10, better: replaces it
  CHECK(loc.kase == gso::Case::A && loc.slot == 1 && loc.ring == 0);
  CHECK(gcm.guide(1).solution == 9 && gcm.guide(1).id != old_id);

  loc = gcm.locate_and_update(15, 15, 0);  // far from all, not elite, nothing expired
  CHECK(loc.kase == gso::Case::E && loc.slot == 2 && loc.ring == 2);

  loc = gcm.locate_and_update(5, 5, 0);  // far from all, better than the worst elite (9)
  CHECK(loc.kase == gso::Case::C && loc.slot == 1);
  CHECK(gcm.guide(1).solution == 5 && gcm.guide(1).zone == gso::Zone::Elite);

  loc = gcm.locate_and_update(45, 45, 200);  // far, not elite, diversity guides expired
  CHECK(loc.kase == gso::Case::D && loc.slot == 2);
  CHECK(gcm.guide(2).solution == 45 && gcm.guide(2).zone == gso::Zone::Diversity);

  // Radius as a fraction of the largest distance between the initial guides (0..30 -> 30).
  gso::Config ratio = cfg;
  ratio.region.delta_max = -1;
  ratio.region.delta_ratio = 0.1;
  Line line2;
  line2.guides = {0, 10, 20, 30};
  gso::GCM<Line> gcm2(line2, ratio, [] { return std::make_unique<gso::RandomAgent>(2); }, -1);
  gcm2.initialize(rng, 0);
  CHECK(std::abs(gcm2.delta_max() - 3.0) < 1e-12);
  ratio.region.delta_max = 2;  // an explicit radius takes precedence over delta_ratio
  gso::GCM<Line> gcm2b(line2, ratio, [] { return std::make_unique<gso::RandomAgent>(2); }, -1);
  line2.next = 0;
  gcm2b.initialize(rng, 0);
  CHECK(gcm2b.delta_max() == 2);

  // Update rule expiry_first (the implementation of the article's experiments): no zones; an
  // expired guide is replaced first, whatever the new solution is.
  gso::Config ef = cfg;
  ef.gcm.update = "expiry_first";
  Line line3;
  line3.guides = {0, 10, 20, 30};
  gso::GCM<Line> gcm3(line3, ef, [] { return std::make_unique<gso::RandomAgent>(2); }, -1);
  gcm3.initialize(rng, 0);
  loc = gcm3.locate_and_update(11, 11, 0);  // in the region of 10, worse: ring
  CHECK(loc.kase == gso::Case::B && loc.slot == 1);
  loc = gcm3.locate_and_update(25, 25, 0);  // outside, better than the worst guide (30)
  CHECK(loc.kase == gso::Case::C && loc.slot == 3 && gcm3.guide(3).solution == 25);
  loc = gcm3.locate_and_update(55, 55, 0);  // outside, worse than every guide, nothing expired
  CHECK(loc.kase == gso::Case::E);
  loc = gcm3.locate_and_update(11, 11, 200);  // everything expired: the first expired slot takes it
  CHECK(loc.kase == gso::Case::D && loc.slot == 0 && gcm3.guide(0).solution == 11);
  gcm3.visit(2);
  CHECK(gcm3.views()[2].visits == 1 && gcm3.views()[0].visits == 0);
  CHECK(gcm3.ring_in(1, 12) == 2);  // |12 - 10| = 2 -> ring floor(3 * 2 / 3) = 2

  // gcm.case_a: renew gives the improved guide a new identity and resets its agents (lambda < 0);
  // drift keeps both.
  for (const char* mode : {"renew", "drift"}) {
    gso::Config ca = cfg;
    ca.gcm.case_a = mode;
    Line line4;
    line4.guides = {0, 10, 20, 30};
    gso::GCM<Line> gcm4(line4, ca, [] { return std::make_unique<gso::UCB1>(2, 1.0); }, -1);
    gcm4.initialize(rng, 0);
    gcm4.agent(1, 0).update(0, 1.0);
    const auto id = gcm4.guide(1).id;
    loc = gcm4.locate_and_update(9, 9, 0);
    CHECK(loc.kase == gso::Case::A && gcm4.guide(1).solution == 9);
    const auto& kept = static_cast<const gso::UCB1&>(gcm4.agent(1, 0));
    if (std::string(mode) == "renew") CHECK(gcm4.guide(1).id != id && kept.rewarded(0) == 0);
    else CHECK(gcm4.guide(1).id == id && kept.rewarded(0) == 1);
  }
}

void test_ccm() {
  std::vector<int> settled;
  auto record = [&](const gso::Decision& d) { settled.push_back(d.move); };
  gso::CCM window(3);
  for (int m = 1; m <= 3; ++m) window.push(gso::Decision{0, 0, 0, m, 0, 0, 0}, record);
  CHECK(settled.empty());
  window.push(gso::Decision{0, 0, 0, 4, 0, 0, 0}, record);  // full: the oldest leaves
  CHECK(settled == std::vector<int>({1}));
  window.flush(record);
  CHECK(settled == std::vector<int>({1, 2, 3, 4}));
  CHECK(window.pending() == 0);

  gso::CCM immediate(0);
  settled.clear();
  immediate.push(gso::Decision{0, 0, 0, 9, 0, 0, 0}, record);
  CHECK(settled == std::vector<int>({9}));
}

gso::Config knapsack_config() {
  gso::Config cfg;
  cfg.budget.iterations = 40;
  cfg.region.delta_max = 0.1;
  cfg.output.print_config = false;
  cfg.output.print_summary = false;
  return cfg;
}

void test_solver() {
  Knapsack problem(80, 7);
  const gso::Config cfg = knapsack_config();
  gso::Solver<Knapsack> solver(problem, cfg);
  const auto r1 = solver.run(5);
  const auto r2 = solver.run(5);
  CHECK(r1.best_cost == r2.best_cost);  // same seed, same run
  CHECK(r1.trace.size() == r2.trace.size());
  CHECK(r1.stats.macro_moves == r2.stats.macro_moves);
  CHECK(r1.stats.chosen == r2.stats.chosen);

  const auto& st = r1.stats;
  long long cases = 0, chosen = 0;
  for (long long c : st.cases) cases += c;
  for (long long c : st.chosen) chosen += c;
  CHECK(st.iterations == 40);
  CHECK(st.macro_moves == 40 * cfg.search.region_iters);
  CHECK(cases == st.macro_moves);
  CHECK(chosen == st.macro_moves);
  CHECK(st.rewards_paid + st.rewards_discarded == st.macro_moves);  // every decision is settled
  CHECK(-r1.best_cost <= problem.lp_bound());
  CHECK(!r1.trace.empty() && r1.trace.back().cost == r1.best_cost);
  CHECK(r1.best.value == -r1.best_cost);

  // Variants used in ablations run with the same binary.
  for (const char* change : {"agent.type=random", "ccm.size=0", "agent.on_replace=decay(lambda=0.5)",
                             "region.rings=1", "reward.type=levels", "search.guide_selection=uniform",
                             "search.guide_selection=ucb(c=1.4)", "gcm.update=expiry_first", "search.agent_of=start",
                             "gcm.tenure_unit=budget;gcm.tenure=0.15", "agent.type=thompson", "agent.scope=global",
                             "agent.scope=global;region.rings=3;agent.on_replace=reset", "gcm.case_a=drift",
                             "search.schedule=all", "search.schedule=all;agent.scope=global;ccm.size=0", "agent.scope=shuffled",
                             "agent.scope=shuffled;region.rings=3;search.agent_of=closest",
                             "region.on_saturation=replace;region.saturation=2", "region.on_saturation=replace;gcm.case_a=renew",
                             "region.on_saturation=replace_other", "region.on_saturation=replace_random(p=0.1)",
                             "gcm.update=expiry_first;search.guide_selection=ucb(c=1.4);search.agent_of=start;"
                             "region.rings=1;ccm.size=2;agent.on_replace=decay(lambda=0.5)",
                             "gcm.tenure_unit=seconds"}) {
    gso::Config v = knapsack_config();
    gso::Params p;
    v.bind(p);
    std::stringstream settings(change);  // "name=value;name=value"
    for (std::string item; std::getline(settings, item, ';');)
      p.set(item.substr(0, item.find('=')), item.substr(item.find('=') + 1));
    gso::Solver<Knapsack> variant(problem, v);
    const auto r = variant.run(1);
    CHECK(r.stats.rewards_paid + r.stats.rewards_discarded == r.stats.macro_moves);
  }

  // budget.macro_moves: the run stops after that many macro-moves, whatever search.region_iters is.
  {
    gso::Config mm = knapsack_config();
    mm.budget.iterations = 0;
    mm.budget.seconds = 0;
    mm.budget.macro_moves = 137;
    mm.search.region_iters = 25;
    gso::Solver<Knapsack> s_mm(problem, mm);
    const auto r = s_mm.run(2);
    CHECK(r.stats.macro_moves == 137 && r.stats.iterations == 6);  // 5 full regional searches + 12 moves
    mm.gcm.tenure_unit = "budget";
    mm.gcm.tenure = 0.5;
    gso::Solver<Knapsack> s_mt(problem, mm);
    CHECK(s_mt.run(2).stats.macro_moves == 137);
    gso::Config none = knapsack_config();
    none.budget.iterations = 0;
    none.budget.seconds = 0;
    CHECK_THROWS(none.validate());
  }

  // search.schedule = all: every guide runs a regional search per round; each one is an iteration.
  {
    gso::Config all = knapsack_config();
    all.search.schedule = "all";
    gso::Solver<Knapsack> s_all(problem, all);
    const auto a1 = s_all.run(3);
    const auto a2 = s_all.run(3);
    CHECK(a1.best_cost == a2.best_cost && a1.stats.chosen == a2.stats.chosen);
    CHECK(a1.stats.iterations == 40 && a1.stats.macro_moves == 40 * all.search.region_iters);
    // Saturation statistics: every macro-move chosen in a saturated region is counted once.
    long long sat_chosen = 0;
    for (long long c : a1.stats.chosen_saturated) sat_chosen += c;
    CHECK(sat_chosen <= a1.stats.searches_saturated * all.search.region_iters);
    CHECK(a1.stats.searches_saturated <= a1.stats.iterations && a1.stats.longest_streak >= 0);
    gso::Config one = knapsack_config();
    one.region.saturation = 1;  // every search that does not improve its guide makes it saturated
    gso::Solver<Knapsack> s_one(problem, one);
    CHECK(s_one.run(3).stats.searches_saturated >= a1.stats.searches_saturated / 2);
    // region.on_saturation = replace: saturated regions are abandoned for new guides.
    gso::Config rep = knapsack_config();
    rep.region.saturation = 1;
    rep.region.on_saturation = "replace";
    gso::Solver<Knapsack> s_rep(problem, rep);
    const auto r_rep = s_rep.run(3);
    CHECK(r_rep.stats.saturated_replaced > 0 && r_rep.stats.searches_saturated == 0);
    CHECK(r_rep.stats.iterations == 40 && r_rep.best_cost == s_rep.run(3).best_cost);
    // Its controls abandon regions too, but not the saturated one. replace_other abandons as many
    // regions as replace at the same moments; replace_random(p) abandons with probability p.
    gso::Config other = rep;
    other.region.on_saturation = "replace_other";
    gso::Solver<Knapsack> s_other(problem, other);
    const auto r_other = s_other.run(3);
    CHECK(r_other.stats.saturated_replaced > 0 && r_other.stats.searches_saturated == 0);
    CHECK(r_other.stats.iterations == 40 && r_other.best_cost == s_other.run(3).best_cost);
    gso::Config rnd = knapsack_config();
    rnd.region.on_saturation = "replace_random(p=1)";
    gso::Solver<Knapsack> s_rnd(problem, rnd);
    CHECK(s_rnd.run(3).stats.saturated_replaced == 40);
    for (const char* bad_sat : {"forget", "replace_random", "replace_random(p=0)", "replace_random(p=1.5)",
                                "replace_random(q=0.1)", "replace_other(p=0.1)", "replace(p=0.1)"}) {
      rep.region.on_saturation = bad_sat;
      CHECK_THROWS(rep.validate());
    }
    gso::Config wrong = knapsack_config();
    wrong.search.schedule = "parallel";
    CHECK_THROWS(wrong.validate());
  }

  gso::Config bad = knapsack_config();
  bad.agent.type = "ucb1(k=2)";
  CHECK_THROWS(gso::Solver<Knapsack>(problem, bad));
}

class MyAgent : public gso::BanditAgent {
 public:
  MyAgent(int moves, double x) : BanditAgent(moves), x_(x) {}
  int select(gso::Rng& rng) override { return record(static_cast<int>(rng.below(moves()))); }
  double x() const { return x_; }

 private:
  double x_;
};

void test_custom_components() {
  gso::register_agent("my_agent", [](const gso::Spec& spec, const std::vector<std::string>& moves) {
    spec.allow_only({"x"});
    return std::unique_ptr<gso::Agent>(new MyAgent(int(moves.size()), spec.number("x", 1.0)));
  });
  gso::register_reward("my_reward", [](const gso::Spec&) {
    return gso::RewardFn([](const gso::RewardInput& in) { return in.cost_before - in.cost_now; });
  });
  Knapsack problem(40, 2);
  gso::Config cfg = knapsack_config();
  cfg.agent.type = "my_agent(x=2)";
  cfg.reward.type = "my_reward";
  gso::Solver<Knapsack> solver(problem, cfg);
  const auto r = solver.run(3);
  CHECK(r.stats.macro_moves == 40 * cfg.search.region_iters);
  cfg.agent.type = "my_agent(y=2)";
  CHECK_THROWS(gso::Solver<Knapsack>(problem, cfg));
}

// An adapter with deliberate mistakes, to check that gso::check_adapter finds them.
struct Broken {
  using Solution = double;
  int calls = 0;
  double cost(const double& x) const { return x; }
  double make_guide(gso::Rng& rng) { return 10.0 * rng.uniform() + calls++; }  // not reproducible
  double distance(const double& a, const double& b) const { return a - b + 1; }  // d(a,a) = 1, asymmetric
  gso::MacroMoves<Broken> macro_moves() {
    return {{"nothing", [](double&, gso::Context<Broken>&) {}},  // never changes the solution
            {"step", [](double& x, gso::Context<Broken>& ctx) { x += ctx.rng().uniform(); }}};
  }
};

struct Lazy {
  using Solution = double;
  double cost(const double& x) const { return x; }
  double make_guide(gso::Rng& rng) { return 10.0 * rng.uniform(); }
  double distance(const double& a, const double& b) const { return std::abs(a - b); }
  gso::MacroMoves<Lazy> macro_moves() {
    return {{"nothing", [](double&, gso::Context<Lazy>&) {}},
            {"step", [](double& x, gso::Context<Lazy>& ctx) { x += ctx.rng().uniform(); }}};
  }
};

// Solutions are points in [0, 10]^3; distance = L1. With Bounded = true it also has the bounded
// distance (stops once the sum exceeds the limit); with Wrong = true that one returns the limit itself
// when it stops, which breaks the contract (it must return a value > limit).
template <bool Bounded, bool Wrong = false>
struct Cube {
  using Solution = std::array<double, 3>;
  double cost(const Solution& x) const { return std::abs(x[0] - 3) + std::abs(x[1] - 7) + std::abs(x[2] - 5); }
  Solution make_guide(gso::Rng& rng) { return {10 * rng.uniform(), 10 * rng.uniform(), 10 * rng.uniform()}; }
  double distance(const Solution& a, const Solution& b) const {
    double d = 0;
    for (int i = 0; i < 3; ++i) d += std::abs(a[i] - b[i]);
    return d;
  }
  template <bool B = Bounded, class = std::enable_if_t<B>>
  double distance(const Solution& a, const Solution& b, double limit) const {
    double d = 0;
    for (int i = 0; i < 3; ++i) {
      d += std::abs(a[i] - b[i]);
      if (d > limit) return Wrong ? limit : d;
    }
    return d;
  }
  gso::MacroMoves<Cube> macro_moves() {
    return {{"small", [](Solution& x, gso::Context<Cube>& ctx) { x[ctx.rng().below(3)] += ctx.rng().uniform() - 0.5; }},
            {"large", [](Solution& x, gso::Context<Cube>& ctx) { x[ctx.rng().below(3)] = 10 * ctx.rng().uniform(); }}};
  }
};

// search.guide_selection = value: Thompson sampling over the discounted success of each region.
void test_region_value() {
  gso::Rng rng(11);
  double sum = 0;
  for (int i = 0; i < 20000; ++i) sum += rng.beta(2, 6);
  CHECK(std::abs(sum / 20000 - 0.25) < 0.01);  // mean a / (a + b)
  sum = 0;
  for (int i = 0; i < 20000; ++i) sum += rng.gamma(0.5);
  CHECK(std::abs(sum / 20000 - 0.5) < 0.02);  // mean = shape

  // a region with recent successes is chosen far more often than exhausted ones
  const auto pick = gso::make_guide_selection(gso::Spec::parse("value(a=1,b=1)"));
  std::vector<gso::GuideView> g(4, gso::GuideView{0.0, 10, 0.0, 6.0});
  g[2].wins = 3, g[2].losses = 1;
  int chosen2 = 0;
  for (int i = 0; i < 2000; ++i) chosen2 += pick(g, rng) == 2;
  CHECK(chosen2 > 1700);
  CHECK_THROWS(gso::make_guide_selection(gso::Spec::parse("value(a=0)")));

  // the GCM records outcomes with the discount; drift keeps them, replacement resets them
  Line line;
  line.guides = {0.0, 10.0};
  gso::Config cfg;
  cfg.gcm.size = 2;
  cfg.region.delta_max = 2;
  cfg.agent.type = "random";
  gso::GCM<Line> gcm(line, cfg, [] { return gso::make_agent(gso::Spec::parse("random"), {"l", "r"}); }, -1);
  gcm.initialize(rng, 0);
  gcm.record_search(0, true, 0.5);
  gcm.record_search(0, false, 0.5);
  CHECK(gcm.views()[0].wins == 0.5 && gcm.views()[0].losses == 1.0);
  gcm.locate_and_update(-1.0, -1.0, 0);  // case A, drift: same region
  CHECK(gcm.views()[0].wins == 0.5);
  gcm.renew(0, 3.0, 3.0, 0);
  CHECK(gcm.views()[0].wins == 0 && gcm.views()[0].losses == 0);

  cfg = gso::Config{};
  CHECK_THROWS((cfg.search.value_discount = 0, cfg.validate()));
}

// agent.scope = shuffled: each decision is taken by an agent drawn at random, and its (delayed)
// reward goes to that same agent, never to another one.
struct Tally {
  static std::vector<std::pair<long long, long long>>& counts() {  // per agent: decisions, rewards
    static std::vector<std::pair<long long, long long>> c;
    return c;
  }
};
class TallyAgent : public gso::Agent {
 public:
  TallyAgent(int moves) : moves_(moves), id_(Tally::counts().size()) { Tally::counts().push_back({0, 0}); }
  int select(gso::Rng& rng) override {
    ++Tally::counts()[id_].first;
    return static_cast<int>(rng.below(static_cast<std::size_t>(moves_)));
  }
  void update(int, double) override { ++Tally::counts()[id_].second; }
  void reset() override {}
  void decay(double) override {}

 private:
  int moves_;
  std::size_t id_;
};

void test_shuffled_scope() {
  gso::register_agent("tally", [](const gso::Spec&, const std::vector<std::string>& moves) {
    return std::unique_ptr<gso::Agent>(new TallyAgent(static_cast<int>(moves.size())));
  });
  gso::Config cfg;
  cfg.budget.iterations = 0;
  cfg.budget.macro_moves = 2000;
  cfg.region.delta_ratio = 0.2;
  cfg.ccm.size = 3;
  cfg.agent.type = "tally";
  cfg.output.print_config = cfg.output.print_summary = false;
  for (const std::string scope : {"region", "shuffled"}) {
    cfg.agent.scope = scope;
    Cube<false> p;
    gso::Solver<Cube<false>> solver(p, cfg);
    Tally::counts().clear();  // the agents of the run (the constructor builds one to check the spec)
    const auto r = solver.run(4);
    long long decisions = 0, rewards = 0, used = 0;
    bool own = true;
    for (const auto& [d, w] : Tally::counts()) {
      decisions += d, rewards += w, used += d > 0;
      own = own && w <= d;  // an agent is rewarded only for decisions it took
    }
    CHECK(own && decisions == r.stats.macro_moves && rewards == r.stats.rewards_paid);
    CHECK(static_cast<int>(Tally::counts().size()) == cfg.gcm.size * cfg.region.rings);
    if (scope == "shuffled") CHECK(used == cfg.gcm.size);  // all of them take decisions
  }
  cfg.agent.scope = "random";
  CHECK_THROWS(cfg.validate());
}

// The bounded distance only saves time: the run is the same with and without it.
void test_bounded_distance() {
  gso::Config cfg;
  cfg.budget.iterations = 0;
  cfg.budget.macro_moves = 3000;
  cfg.region.delta_ratio = 0.2;
  cfg.output.print_config = cfg.output.print_summary = false;
  for (const std::string scope : {"region", "global", "shuffled"})
    for (const int rings : {1, 3}) {
      cfg.agent.scope = scope;
      cfg.region.rings = rings;
      cfg.search.agent_of = rings == 1 ? "start" : "closest";
      Cube<false> plain;
      Cube<true> bounded;
      const auto a = gso::Solver<Cube<false>>(plain, cfg).run(5);
      const auto b = gso::Solver<Cube<true>>(bounded, cfg).run(5);
      CHECK(a.best_cost == b.best_cost);
      bool same = a.stats.chosen == b.stats.chosen && a.stats.chosen_by_ring == b.stats.chosen_by_ring;
      for (int k = 0; k < 5; ++k) same = same && a.stats.cases[k] == b.stats.cases[k];
      CHECK(same);
    }

  std::ostringstream out;
  Cube<true> good;
  cfg.region.delta_max = 2;
  CHECK(gso::check_adapter(good, cfg, {}, out).errors == 0);
  Cube<true, true> wrong;
  out.str("");
  CHECK(gso::check_adapter(wrong, cfg, {}, out).errors >= 1);
  CHECK(out.str().find("the bounded distance(a, b, limit) must return") != std::string::npos);
}

void test_check_adapter() {
  std::ostringstream out;
  Knapsack problem(80, 7);
  gso::Config cfg = knapsack_config();
  cfg.region.delta_max = 0.4;
  const gso::CheckReport good = gso::check_adapter(problem, cfg, {}, out);
  CHECK(good.errors == 0);
  CHECK(out.str().find("pilot run") != std::string::npos);

  cfg.region.delta_max = -1;  // not set: an error, and a suggestion
  cfg.region.delta_ratio = -1;
  out.str("");
  const gso::CheckReport unset = gso::check_adapter(problem, cfg, {}, out);
  CHECK(unset.errors == 1);
  CHECK(out.str().find("suggestion: start with region.delta_max") != std::string::npos);

  Broken broken;
  gso::Config bcfg = knapsack_config();
  bcfg.region.delta_max = 1;
  out.str("");
  const gso::CheckReport bad = gso::check_adapter(broken, bcfg, {}, out);
  CHECK(out.str().find("distance(s, s) is not 0") != std::string::npos);
  CHECK(out.str().find("make_guide() gave different solutions") != std::string::npos);
  CHECK(out.str().find("distance(a, b) != distance(b, a)") != std::string::npos);
  CHECK(bad.errors >= 2 && bad.warnings >= 1);

  Lazy lazy;  // correct, except one macro-move that never changes anything
  out.str("");
  gso::check_adapter(lazy, bcfg, {}, out);
  CHECK(out.str().find("macro-move 'nothing': ") != std::string::npos);
  CHECK(out.str().find("it never changed the solution") != std::string::npos);
}

// Runs gso::run_main with the given arguments, capturing what it prints.
int run_main_quietly(std::vector<std::string> args, std::string* printed = nullptr) {
  Knapsack problem(40, 3);
  gso::Config cfg = knapsack_config();
  cfg.region.delta_max = 0.4;
  args.insert(args.begin(), "prog");
  std::vector<char*> argv;
  for (auto& a : args) argv.push_back(a.data());
  std::ostringstream out, err;
  auto* old_out = std::cout.rdbuf(out.rdbuf());
  auto* old_err = std::cerr.rdbuf(err.rdbuf());
  const int status = gso::run_main(problem, cfg, static_cast<int>(argv.size()), argv.data());
  std::cout.rdbuf(old_out);
  std::cerr.rdbuf(old_err);
  if (printed) *printed = out.str() + err.str();
  return status;
}

void test_run_main() {
  std::string printed;
  CHECK(run_main_quietly({"--help"}, &printed) == 0);
  CHECK(printed.find("--check") != std::string::npos && printed.find("gcm.size") != std::string::npos);
  CHECK(run_main_quietly({"--check"}, &printed) == 0);
  CHECK(printed.find("# 0 error(s)") != std::string::npos);
  CHECK(run_main_quietly({"--budget.iterations=5"}, &printed) == 0);
  CHECK(printed.find("best cost:") != std::string::npos);
  CHECK(run_main_quietly({"--gcm.sise=3"}, &printed) == 1);
  CHECK(printed.find("Did you mean 'gcm.size'?") != std::string::npos);
}

void test_configure() {
  Knapsack problem(30, 1);
  gso::Config cfg;
  cfg.region.delta_max = 0.4;  // set in code: a preset must keep it
  const char* args[] = {"prog", "--preset=eswa2027_text_small", "instance.txt", "--agent.type=random",
                        "--problem.tabu_steps=20", "--ccm.size", "5"};
  std::vector<std::string> positional;
  const bool go = gso::configure(cfg, problem, 7, const_cast<char**>(args), &positional);
  CHECK(go);
  CHECK(cfg.gcm.size == 10);  // from the preset
  CHECK(cfg.region.delta_max == 0.4);
  CHECK(cfg.agent.type == "random");
  CHECK(cfg.ccm.size == 5);
  CHECK(problem.tabu_steps == 20);
  CHECK(positional == std::vector<std::string>({"instance.txt"}));
}

int main() {
  test_rng();
  test_spec_and_params();
  test_agents();
  test_rewards_and_selection();
  test_gcm_cases();
  test_ccm();
  test_solver();
  test_custom_components();
  test_check_adapter();
  test_bounded_distance();
  test_shuffled_scope();
  test_region_value();
  test_run_main();
  test_configure();
  std::cout << checks - failures << "/" << checks << " checks passed\n";
  return failures == 0 ? 0 : 1;
}
