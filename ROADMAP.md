# Roadmap

Work is organized in phases. Each phase ends with something usable.

| Phase | Content | Status |
|---|---|---|
| 0 | Repository structure, licenses, citation, framework specification, documentation | **done** |
| 1 | C++ core: GCM with zones and tenure, localization and cases A–E, CCM with contextual validation, main loop, configuration (files, command line, `--help`), seeds; agents `ucb1`, `random`, `fixed`, `epsilon_greedy`; rewards `eq3`, `levels`; guide selection `geometric`, `uniform`, `best`; `reset`/`decay` on replacement; presets of the article; run statistics | **done** (v0.1) |
| 2 | Blank template (`templates/my_problem`), minimal example (knapsack), `check_adapter` / `--check` (the checklist, automated, with suggestions), `run_main`, tests of the core | **done** |
| 3 | Exact reproduction of the article's experiments: the SCC-VRP adapter and frozen configurations per table | planned |
| 4b | Both systems of the article as options; defaults chosen by a benchmark on four problems (docs/05) | **done** |
| 4 | More agents: `softmax`, `thompson`, `exp3`, `sliding_window_ucb`, `discounted_ucb`, `gradient` (with tests and a benchmark); radius as a fraction of a reference distance (`region.delta_ratio`) | **done** |
| 4c | Controls for regional learning: `agent.scope = global \| shuffled`, `region.on_saturation = replace_other \| replace_random(p)`; tuning kit (`tools/irace`) | **done** |
| 5 | Run statistics (cases A–E, macro-moves per ring, saturated regions, time in the core) | **done** · planned: a log of every decision and analysis scripts |
| 6 | Python bridge: bindings and a Gymnasium-style decision environment for external agents | planned |
| 7 | Assistant skill: packaged instructions to adapt GSO-RL to a new problem from a prompt | planned |
| — | Figures for the documentation (`docs/figures/`, TikZ sources + SVG) | **done** |

**v0.1.0** is the first public release: the framework usable on a new problem, with the controls to
check what learning per region adds on it. **v1.0.0** will be tagged when the article's experiments
can be reproduced exactly with its preset (phase 3). Until then, the API may still change between
minor versions; every change is listed in [CHANGELOG.md](CHANGELOG.md).

## Design decisions

| # | Decision | Reason |
|---|---|---|
| D1 | Core in C++17, header-only, no dependencies | Speed matters for macro-moves; a single `#include` is the easiest thing to adopt and to explain to an AI assistant. |
| D2 | The problem is a template parameter | Compile-time errors when part of the adapter is missing (clearer with C++20 concepts); no virtual calls on solutions. |
| D3 | Agent, reward and guide selection are chosen at run time in the configuration, as text specs `name(key=value)` | One binary runs every variant: ablations and irace tuning without recompiling. They are called once per macro-move, so the cost of run-time dispatch is negligible. |
| D4 | Macro-moves are a generic list; the only property the core uses is `restart` | The framework must not assume what a macro-move does: full procedures, engine parametrizations and single operators are all valid. |
| D5 | General framework + presets, rather than a hard-coded copy of the article | Other problems need other choices (rings, agents, rewards); the article's values remain available as a preset. |
| D6 | The specification (`docs/02-framework.md`) is the source of truth | Ports, refactors and AI-generated code are checked against one document. |
| D7 | Code under Apache 2.0 with a NOTICE; docs under CC BY 4.0 | Permissive for users, while the NOTICE and the documentation license keep the attribution in every redistribution. |
| D8 | One table of parameters (`Config::each`) generates `--help`, file and command-line reading, and the printed configuration | Parameters are always visible, and documentation and code cannot diverge. |
| D9 | Own random generator and distributions (`gso::Rng`) | The distributions of `<random>` differ between compilers; the same seed must give the same run everywhere. |
