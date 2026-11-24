# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and versions follow
[Semantic Versioning](https://semver.org/).

## [0.1.0] — first public release

### Core (`include/gso/`, C++17, header-only, no dependencies)

- General Context Memory (GCM): guides with elite and diversity zones, tenure, concentric rings and
  the update cases A–E; `gcm.update = expiry_first | cases`, `gcm.case_a = drift | renew`.
- Current Context Memory (CCM): delayed rewards with contextual validation (rewards of decisions
  whose guide was replaced are discarded).
- Main loop: guide selection, regional searches of `search.region_iters` macro-moves,
  `search.schedule = select | all`, `search.agent_of = start | closest`.
- Saturated regions: statistics, and `region.on_saturation = replace` (an exhausted region is
  abandoned for a new guide).
- `Context` for macro-moves (`ctx.offer` reports better solutions found inside a macro-move);
  compile-time checks of the adapter with clear messages.
- Localization compares the guide of the current regional search first; an optional bounded
  `distance(a, b, limit)` in the adapter lets it stop early against the other guides.
- Reproducible runs: own random generator and distributions, identical on every compiler.

### Components (chosen in the configuration; registries for your own)

- Agents: `thompson` (default), `ucb1`, `epsilon_greedy`, `softmax`, `exp3`, `sliding_window_ucb`,
  `discounted_ucb`, `gradient`, `random`, `fixed(move)`; `agent.on_replace = decay(lambda) | reset`.
- Rewards: `eq3(alpha, beta)` (Eq. 3 of the article, default), `levels(best, guide, current)`
  (the 6/1/0 reward of the article's experiments).
- Guide selection: `ucb(c)` (default), `geometric(p)`, `uniform`, `best`, `value(a, b)` (learned
  value of each region, `search.value_discount`).

### Controls for regional learning

- `agent.scope = global` (one agent for the whole search) and `agent.scope = shuffled` (as many
  agents as regions, each decision taken by one drawn at random: separates the effect of the region
  from the effect of having several learners).
- `region.on_saturation = replace_other | replace_random(p)`: controls of `replace` that abandon
  regions without looking at which one is exhausted.

### Configuration and tools

- One table of parameters: files, command line, `--help`, the configuration printed by every run,
  messages for wrong names and values. Budgets in iterations, seconds or macro-moves.
- Presets of the article: `eswa2027_experiments`, `eswa2027_text_small`, `eswa2027_text_large`.
  The defaults differ from them; they were chosen on a benchmark of four problems (docs/05).
- `--check` (`gso::check_adapter`): checks cost, guides, distance (including the bounded one),
  macro-moves and reproducibility, makes a pilot run and suggests parameter values.
- `gso::run_main`: a whole `main()` in one call.
- `tools/irace/`: tuning kit for any GSO-RL program.

### Examples, template and documentation

- `examples/knapsack/`: a complete adapter, small enough to read in one sitting.
- `templates/my_problem/`: a blank adapter that compiles, with one `TODO` per part.
- Documentation (`docs/01`–`07`), the specification with every deliberate difference from the
  article (notes N1–N19 of docs/02), figures, the article's PDF (CC BY-NC 4.0), instructions for AI
  assistants (`AGENTS.md`, `llms.txt`).
- Tests of the core, the template and the example; continuous integration on Linux, macOS and
  Windows.
