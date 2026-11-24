#pragma once
// GSO-RL: Guided Strategic Oscillation with Reinforcement Learning.
// Single include of the library. Documentation: docs/README.md
//
// Siverio-González, Melián-Batista, Moreno-Vega (2027). Expert Systems with Applications 334, 134651.
// https://doi.org/10.1016/j.eswa.2026.134651
// Licensed under the Apache License 2.0 (see LICENSE and NOTICE).

#define GSO_VERSION_MAJOR 0
#define GSO_VERSION_MINOR 1
#define GSO_VERSION_PATCH 0

#include "agents.hpp"            // learning agents (docs/04)
#include "ccm.hpp"               // Current Context Memory (spec 2.6)
#include "check.hpp"             // gso::check_adapter: checks an adapter at run time (docs/03)
#include "config.hpp"            // every parameter (docs/05)
#include "gcm.hpp"               // General Context Memory and cases A-E (spec 2.3, 2.4)
#include "guide_selection.hpp"   // which guide a regional search starts from (spec 2.2)
#include "params.hpp"            // parameter registry: files, command line, help
#include "presets.hpp"           // configurations of published studies
#include "problem.hpp"           // the adapter contract and macro-moves (docs/03)
#include "rewards.hpp"           // reward functions (spec 2.6)
#include "rng.hpp"               // reproducible random numbers
#include "run_main.hpp"          // gso::run_main: configure + check or solve, for main()
#include "solver.hpp"            // main loop, Context, configure (spec 2.2)
#include "stats.hpp"             // run statistics and trace
