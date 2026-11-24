# include/gso — the GSO-RL core (C++17, header-only)

The same code for every problem. Users include one file: `#include <gso/gso.hpp>`.

| Header | Contents | Specification |
|---|---|---|
| `gso.hpp` | single include | |
| `problem.hpp` | the adapter contract, `MacroMove`, `gso::restart`, compile-time checks | docs/03 |
| `config.hpp` | `Config`: every parameter, its default, help and range, in one table | docs/05 |
| `params.hpp` | parameter registry: files, command line, `--help`, printing; `Spec` (`name(key=value)`) | docs/05 |
| `presets.hpp` | configurations of published studies | docs/05 |
| `gcm.hpp` | General Context Memory: guides, zones, tenure, rings, agents (`agent.scope`), cases A–E | spec 2.3, 2.4 |
| `ccm.hpp` | Current Context Memory: circular buffer of pending decisions | spec 2.6 |
| `rewards.hpp` | reward functions (`eq3`, `levels`) and their registry | spec 2.6 |
| `guide_selection.hpp` | which guide a regional search starts from (`ucb`, `geometric`, `uniform`, `best`, `value`), registry | spec 2.2 |
| `agents.hpp` | agent interface; `thompson`, `ucb1`, `epsilon_greedy`, `softmax`, `exp3`, `sliding_window_ucb`, `discounted_ucb`, `gradient`, `random`, `fixed`; registry | docs/04 |
| `solver.hpp` | main loop, saturated regions, `Context` (what a macro-move sees), `configure` | spec 2.2 |
| `stats.hpp` | run statistics and trace of improvements | |
| `check.hpp` | `check_adapter`: checks an adapter at run time and suggests parameter values | docs/03 |
| `run_main.hpp` | `run_main`: configure, then check (`--check`) or solve, for `main()` | docs/03 |
| `rng.hpp` | reproducible random numbers and distributions (normal, gamma, beta), identical on every compiler | |

Design rules: the problem is a template parameter (direct calls on the hot path); components called
once per macro-move (agent, reward) use simple run-time interfaces; no allocation inside the loop
except copies of solutions; every random number comes from `gso::Rng`.
