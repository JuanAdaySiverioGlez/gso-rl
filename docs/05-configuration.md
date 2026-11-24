# 5. What you can change: configuration and presets

GSO-RL has three layers. This page lists everything that can be changed in each one.

| Layer | How it is changed |
|---|---|
| 1. Problem adapter | you write it ([03](03-adapting-to-your-problem.md)) |
| 2. Configuration | parameters: in code, in a file or on the command line (this page) |
| 3. Core | not changed by configuration: it *is* GSO-RL ([02](02-framework.md)) |

## Seeing every parameter

Every parameter has **one name**, used everywhere:

| Where | Example |
|---|---|
| code | `cfg.gcm.size = 15;` |
| file (`--config=run.cfg`) | `gcm.size = 15` |
| command line | `--gcm.size=15` or `--gcm.size 15` |
| printed configuration, `--help`, this page | `gcm.size` |

- `--help` lists every parameter with its default, range and meaning, including your adapter's own
  parameters (`problem.*`).
- Every run prints its full configuration when it starts; values that differ from the default are
  marked `(default: …)`. Keep that output with your results: it is everything needed to repeat the
  run.
- Wrong names and values stop the program with a message that says what to fix
  (`unknown parameter 'gcm.sise'. Did you mean 'gcm.size'?`).

Order of precedence: library defaults < your code < `--preset` < `--config` file < `--name=value`.
Call `gso::configure(cfg, problem, argc, argv)` after setting your values in code, so that the
command line can override them. A preset changes only the parameters it defines (see below): your
`region.delta_max`, seed and output options are kept.

## Layer 2 · Components (choose from a menu, or add your own)

Each component has a default (chosen by the benchmark at the end of this page) and the forms used in
the article's text and in its experiments.

| Parameter | Options | Default | Article's text | Article's experiments |
|---|---|---|---|---|
| `agent.type` | `ucb1(c)`, `epsilon_greedy(eps)`, `softmax(tau)`, `thompson`, `exp3(gamma)`, `sliding_window_ucb(c, window)`, `discounted_ucb(c, gamma)`, `gradient(alpha)`, `random`, `fixed(move)`, your own ([04](04-learning-agents.md)) | `thompson` | `ucb1(c=1.0)` | `ucb1(c=1.0)` |
| `reward.type` | `eq3(alpha, beta)`, `levels(best, guide, current)`, your own | `eq3(alpha=0.75)` | `eq3(alpha=0.75)` | `levels(best=6, guide=0, current=1)` |
| `search.guide_selection` | `ucb(c)`, `geometric(p)`, `uniform`, `best`, `value(a, b)`, your own | `ucb(c=1.4)` | `geometric(p=0.25)` | `ucb(c=1.4)` |
| `gcm.update` | `expiry_first`, `cases` | `expiry_first` | `cases` | `expiry_first` |
| `search.agent_of` | `start`, `closest` | `start` | `closest` | `start` |
| `search.schedule` | `select`, `all` | `select` | `select` | `select` |
| `agent.on_replace` | `decay(lambda)`, `reset` | `decay(lambda=0.5)` | `reset` | `decay(lambda=0.5)` |
| `gcm.case_a` | `drift`, `renew` | `drift` | `renew` | `renew` |
| `agent.scope` | `region`, `global`, `shuffled` | `region` | `region` | `region` |

- `eq3(alpha, beta)`: Eq. 3 of the article (`β = 1 − α` unless given): relative improvements, of
  order 10⁻³–10⁻². Pair it with a scale-free agent (`thompson`, `gradient`, `exp3`,
  `epsilon_greedy`) or a small UCB1 `c`.
- `levels(best, guide, current)`: points for the highest improvement reached — a new best solution,
  better than the guide of its region, better than the solution it started from; 0 otherwise.
  `levels(best=6, guide=0, current=1)` is the 6/1/0 reward of the article's experiments.
- `ucb(c)`: the guide with the largest `quality + c·sqrt(2 ln(V+1) / (v+1))`, quality 1 for the best
  guide and 0 for the worst, `v` its visits, `V` all visits. `geometric(p)`: guides ranked by cost,
  position `k` chosen with probability proportional to `(1 − p)^k`.
- `expiry_first` / `cases`: the two memory update rules ([02](02-framework.md), Section 2.4).
- `start` / `closest`: whose agent decides each macro-move ([02](02-framework.md), Section 2.5).
- `renew` / `drift`: whether a guide improved inside its own region is a new guide (new identity;
  agents follow `agent.on_replace`; pending rewards discarded) or the same region moved
  ([02](02-framework.md), Section 2.4).
- `region` / `global` / `shuffled`: one agent per ring of each guide (the method); or, as controls,
  a single agent for the whole search, or as many agents as with `region` with each decision taken
  by one of them drawn at random. `global` measures what several learners add; `shuffled` measures
  what tying them to the regions adds ([06](06-experimental-protocol.md)).

## Layer 2 · Parameters

| Parameter | Default | Range | What it controls |
|---|---|---|---|
| `budget.iterations` | 0 | ≥ 0 | stop after this many global iterations (0 = no limit) |
| `budget.seconds` | 0 | ≥ 0 | stop after this many seconds (0 = no limit). **Set at least one budget** — the only parameter you must set |
| `budget.macro_moves` | 0 | ≥ 0 | stop after this many macro-moves (0 = no limit). Deterministic like `budget.iterations`, but it counts the work: use it when `search.region_iters` differs between the configurations you compare or tune |
| `seed` | 1 | | random seed; run `r` of an experiment should use `seed + r` |
| `gcm.size` | 15 | ≥ 1 | number of guides `\|G\|` |
| `gcm.elite_ratio` | 0.70 | [0, 1] | share of guides kept for quality (only with `gcm.update = cases`) |
| `gcm.tenure` | 1 | > 0 | lifetime of a guide before it can be replaced; with `tenure_unit = budget`, 1 = guides never expire |
| `gcm.tenure_unit` | budget | iterations, seconds, budget | unit of `gcm.tenure`; `budget` = a fraction of the run's budget |
| `region.delta_max` | −1 (unused) | ≥ 0 | radius of a region in the units of your distance; when set, it is used instead of `delta_ratio` |
| `region.delta_ratio` | 0.5 | (0, 1] | radius as a fraction of `region.delta_reference` |
| `region.delta_reference` | guides | guides, adapter | the largest distance between the initial guides, or the adapter's `max_distance()` |
| `region.rings` | 1 | ≥ 1 | rings per region, one agent each |
| `region.on_saturation` | none | none, replace, replace_other, replace_random(p) | `replace`: a region saturated for `region.saturation` searches in a row is abandoned and a new guide from the generator takes its slot (the best solution is kept). Controls of `replace`, to measure what knowing which region is saturated adds: `replace_other` abandons another region chosen at random at the same moments; `replace_random(p)` abandons a region chosen at random with probability p after each regional search |
| `region.saturation` | 5 | ≥ 1 | a region counts as saturated after this many regional searches in a row from its guide that did not improve it; the summary reports how much of the search starts from saturated regions and which macro-moves are chosen there |
| `search.region_iters` | 10 | ≥ 1 | macro-moves per visit to a region |
| `search.value_discount` | 0.8 | (0, 1] | with `search.guide_selection = value(a, b)`: the value of a region is the number of its regional searches that improved its guide or the best solution (wins) and that did not (losses), each search multiplying the earlier counts by this factor; the guide is chosen by Thompson sampling over Beta(a + wins, b + losses), so live regions are searched and exhausted ones are left |
| `search.schedule` | select | select, all | `select`: each iteration, one guide chosen by `search.guide_selection`; `all`: every guide runs its own regional search in turn (a loop per guide) |
| `ccm.size` | 2 | ≥ 0 | decisions waiting for their reward (0 = immediate reward) |
| `output.print_config` | true | | print the configuration when a run starts |
| `output.print_summary` | true | | print a summary when a run ends |
| `problem.*` | | | your adapter's own parameters |

## Layer 3 · The core

Not configurable: localization, the mechanics of the Current Context Memory, contextual validation
(rewards of decisions whose guide was replaced are always discarded) and the order of the main
loop. Some parameters switch parts of the method off for ablations: `agent.type = random`,
`agent.type = fixed(move)`, `agent.scope = global | shuffled`, `ccm.size = 0`, `region.rings = 1`,
`gcm.size = 1` ([06](06-experimental-protocol.md)).

## Presets of the article

`--preset=<name>` or `gso::presets::<name>()`. A preset sets only the parameters it defines: never
the radius, the seed or the output options.

| Parameter | `eswa2027_experiments` | `eswa2027_text_small` (`\|N\| ≤ 30`) | `eswa2027_text_large` (`\|N\| ≥ 90`) |
|---|---|---|---|
| Source | the implementation used for the article's experiments | Section 4.2 and Table 2 | Section 4.2 and Table 2 |
| Budget | set by you (article: 80 s per 90 customers) | `budget.iterations = 3000` | set by you (80 / 160 / 240 / 320 s for `\|N\|` = 90 / 180 / 270 / 360) |
| `gcm.size` | 15 | 10 | 15 |
| `gcm.update` | `expiry_first` | `cases` (elite 0.70) | `cases` (elite 0.70) |
| `gcm.tenure` | 0.15 of the budget | 550 iterations | 800 iterations |
| `gcm.case_a` | `renew` | `renew` | `renew` |
| `region.rings` | 1 | 5 | 3 |
| `search.region_iters` | 10 | 10 | 10 |
| `search.guide_selection` | `ucb(c=1.4)` | `geometric(p=0.25)` | `geometric(p=0.25)` |
| `search.agent_of` | `start` | `closest` | `closest` |
| `agent.type` | `ucb1(c=1.0)` | `ucb1(c=1.0)` | `ucb1(c=1.0)` |
| `agent.on_replace` | `decay(lambda=0.5)` | `reset` | `reset` |
| `ccm.size` | 2 | 60 | 30 |
| `reward.type` | `levels(best=6, guide=0, current=1)` | `eq3(alpha=0.75)` | `eq3(alpha=0.75)` |
| Radius (not in the preset) | 0.3 of the Jaccard distance in the SCC-VRP | `γ` = 1.00 (note N1) | `γ` = 0.85 (note N1) |

Parameters of the article's SCC-VRP macro-moves (Table 2; they belong to the SCC-VRP adapter, which
is not part of this repository):

| Parameter | irace range | small | large |
|---|---|---|---|
| TI rounds (`iter_TI`) | [1, 25] | 5 | 10 |
| MNTS steps (`iter_TS`) | [5, 200] | 25 | 25 |
| Tabu tenure inside MNTS | [5, 30] | 10 | 10 |
| Candidate moves per tabu step (`k`) | fixed | 30 | 30 |
| Randomization range of Clarke–Wright | fixed | [0.05, 0.40] | [0.05, 0.40] |

## How the defaults were chosen

The defaults are the configuration that performed best on a benchmark of four problems of different
kinds (0/1 knapsack, SCC-VRP, p-median and TSP, each with three instances and its own adapter).
Configurations were compared in pairs on the same instances and seeds, and the choice was confirmed
on seeds that played no part in it. The starting point was the configuration of the article's
experiments. Two changes came out of it:

- **Thompson sampling with the Eq. 3 reward** instead of UCB1 with the 6/1/0 reward: Thompson
  sampling does not depend on the scale of the rewards ([04](04-learning-agents.md)).
- **Regions that keep their agents** (`gcm.case_a = drift`, `gcm.tenure = 1`). With `renew` and a
  tenure of 0.15 of the budget, every guide is replaced several times per run, so the agent of a
  region sees only a few decisions before it is reset; and with `renew` and `ccm.size > 0`, the
  decisions that improve their own guide are discarded by the contextual validation. `drift` keeps
  the identity and the agents of a region when its guide improves, and guides that do not expire
  let each agent accumulate experience.

The defaults are a reasonable start, not the best configuration for your problem: tune them
([07](07-tuning.md)) and check what each part of the method adds with the controls
([06](06-experimental-protocol.md)). The benchmark is not part of this repository.

## What to tune first on a new problem

Run `--check` first: its pilot run reports the cases A–E, the reward scale and the tenure against
your budget, with suggestions. Then, in order of expected influence:

1. **The macro-moves themselves**, and their cost balance ([03](03-adapting-to-your-problem.md)).
2. **The agent and the reward together** ([04](04-learning-agents.md)).
3. **The radius**, if the summary shows case E dominating (raise `region.delta_ratio`) or guides
   never replaced (lower it).
4. **`gcm.size`** and **`search.region_iters`**.
