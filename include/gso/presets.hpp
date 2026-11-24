#pragma once
// Presets: the configurations of the article (docs/05-configuration.md).
//
//   gso::Config cfg = gso::presets::eswa2027_experiments();   // in code
//   --preset=eswa2027_experiments                              // on the command line
//
// A preset sets only the parameters it defines. It never sets the radius of the regions (it depends
// on the dissimilarity of each problem), the seed or the output options; on the command line it
// keeps the values your code gave them.
//
// Siverio-González, Melián-Batista, Moreno-Vega (2027), Expert Systems with Applications 334, 134651.

#include <string>

#include "config.hpp"

namespace gso::presets {

// The configuration of the implementation used for the article's experiments. Budget: a time limit
// of 80 s per 90 customers in the article's large instances, left to the user.
inline void apply_eswa2027_experiments(Config& c) {
  c.gcm.size = 15;
  c.gcm.update = "expiry_first";
  c.gcm.case_a = "renew";
  c.gcm.tenure = 0.15;
  c.gcm.tenure_unit = "budget";
  c.region.rings = 1;
  c.search.region_iters = 10;
  c.search.guide_selection = "ucb(c=1.4)";
  c.search.agent_of = "start";
  c.agent.type = "ucb1(c=1.0)";
  c.agent.on_replace = "decay(lambda=0.5)";
  c.ccm.size = 2;
  c.reward.type = "levels(best=6, guide=0, current=1)";
}

// The configuration described in the article's text (Section 4.2) with the values of Table 2,
// large instances (|N| >= 90).
inline void apply_eswa2027_text_large(Config& c) {
  c.gcm.size = 15;
  c.gcm.update = "cases";
  c.gcm.case_a = "renew";
  c.gcm.elite_ratio = 0.70;
  c.gcm.tenure = 800;
  c.gcm.tenure_unit = "iterations";
  c.region.rings = 3;
  c.search.region_iters = 10;
  c.search.guide_selection = "geometric(p=0.25)";
  c.search.agent_of = "closest";
  c.agent.type = "ucb1(c=1.0)";
  c.agent.on_replace = "reset";
  c.ccm.size = 30;
  c.reward.type = "eq3(alpha=0.75)";
}

// Same, small instances (|N| <= 30): 3000 global iterations.
inline void apply_eswa2027_text_small(Config& c) {
  apply_eswa2027_text_large(c);
  c.budget.iterations = 3000;
  c.gcm.size = 10;
  c.gcm.tenure = 550;
  c.region.rings = 5;
  c.ccm.size = 60;
}

inline Config eswa2027_experiments() {
  Config c;
  apply_eswa2027_experiments(c);
  return c;
}

inline Config eswa2027_text_large() {
  Config c;
  apply_eswa2027_text_large(c);
  return c;
}

inline Config eswa2027_text_small() {
  Config c;
  apply_eswa2027_text_small(c);
  return c;
}

// Applies the preset `name` to c.
inline void apply(const std::string& name, Config& c) {
  if (name == "eswa2027_experiments") return apply_eswa2027_experiments(c);
  if (name == "eswa2027_text_large") return apply_eswa2027_text_large(c);
  if (name == "eswa2027_text_small") return apply_eswa2027_text_small(c);
  throw ConfigError("unknown preset '" + name +
                    "'. Available: eswa2027_experiments, eswa2027_text_large, eswa2027_text_small");
}

inline Config by_name(const std::string& name) {
  Config c;
  apply(name, c);
  return c;
}

}  // namespace gso::presets
