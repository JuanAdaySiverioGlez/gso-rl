# 3. Adapting GSO-RL to your problem

Everything problem-specific lives in **one adapter**: a `struct` that you write. The library
provides the memory, localization, agents, rewards and main loop.

- **Start from** [templates/my_problem/](../templates/my_problem/): a blank adapter that compiles,
  with one `TODO` per part.
- **See every part written** for a real problem in
  [examples/knapsack/knapsack.hpp](../examples/knapsack/knapsack.hpp).
- **Wrapping a solver you already have** (instances, neighbourhoods, local search): keep it as it
  is, store its solution type in `Solution`, and call its procedures from the macro-moves.
- **Check it** with `--check` (below): it tells you what is wrong and suggests parameter values.

| You provide | Required | Purpose |
|---|---|---|
| 1. Solution + cost | yes | what is optimized |
| 2. Guide generator | yes | fills the General Context Memory (GCM) at the start |
| 3. Dissimilarity `d(A, B)` | yes | defines what a region is |
| 4. Macro-moves | yes | the actions the agents choose among |
| 5. Your own parameters | no | appear next to the framework's parameters |
| 6. A reward function | no | the default (Eq. 3 of the article: relative improvements) works for any cost |

## The adapter at a glance

```cpp
#include <gso/gso.hpp>

struct MyProblem {
  // 1. Solution and cost (minimized)
  using Solution = MySolution;
  double cost(const Solution& s) const;

  // 2. Guide generator
  Solution make_guide(gso::Rng& rng);

  // 3. Dissimilarity: d(a, a) = 0, d >= 0
  double distance(const Solution& a, const Solution& b) const;

  // 4. Macro-moves
  gso::MacroMoves<MyProblem> macro_moves() {
    return {
        {"rebuild", &MyProblem::rebuild, gso::restart},
        {"perturb", &MyProblem::perturb},
        {"tabu",    &MyProblem::tabu},
    };
  }
  void rebuild(Solution& s, gso::Context<MyProblem>& ctx);
  void perturb(Solution& s, gso::Context<MyProblem>& ctx);
  void tabu(Solution& s, gso::Context<MyProblem>& ctx);

  // 5. Your own parameters (optional): shown and set as problem.<name>
  int tabu_steps = 50;
  void parameters(gso::Params& p) { p.add("tabu_steps", tabu_steps, "steps of the tabu search", ">= 1"); }
};
```

If something is missing or has the wrong signature, the compiler says which part, for example:
`GSO-RL adapter: missing 'double distance(const Solution& a, const Solution& b) const'`.

## Running it

The shortest `main`:

```cpp
int main(int argc, char** argv) {
  MyProblem problem(argv[1]);      // read the instance

  gso::Config cfg;                 // the defaults (docs/05)
  cfg.budget.seconds = 60;         // the only parameter you must set

  return gso::run_main(problem, cfg, argc, argv);   // --help, --check, --preset, --config, --name=value
}
```

`run_main` reads the command line, then checks the adapter (`--check`) or solves and prints the
best cost; it prints configuration errors and exceptions and returns the exit status. Pass a fourth
argument, `[&](const gso::Result<MyProblem>& r) { ... }`, to use the result (for example, to save
`r.best`).

The same steps, written out:

```cpp
  if (!gso::configure(cfg, problem, argc, argv)) return 0;   // --help was printed
  gso::Solver<MyProblem> solver(problem, cfg);
  gso::Result<MyProblem> result = solver.run();              // or run(seed)
  // result.best, result.best_cost, result.trace (every improvement), result.stats
```

The run prints its full configuration when it starts and a summary when it ends (share of cases
A–E, macro-moves chosen, rewards discarded, time spent in your macro-moves and in the core). Both
can be switched off with `output.print_config` and `output.print_summary`.

## 1. Solution and cost

Any copyable C++ type, plus a function returning its cost. GSO-RL **minimizes**; for a
maximization problem return the negated objective (the knapsack example does this).

Keep the solution cheap to copy: the GCM stores `|G|` copies and each regional search starts from
a copy of a guide. If computing the cost is expensive, keep it inside the solution and let `cost`
return it.

## 2. Guide generator

`make_guide(rng)` builds one good starting solution: typically a randomized constructive
heuristic followed by a local search. It is called `gcm.size` times, so it must produce
**different** solutions each time. It **must use the generator it receives**; otherwise all guides
are identical and runs are not reproducible.

## 3. Dissimilarity `d(A, B)`

How structurally different two solutions are. This is the most important design decision of an
adaptation, because it defines what a region is.

Requirements: `d(A, A) = 0`, `d(A, B) ≥ 0`, and preferably symmetric. The scale is free:
`region.delta_max` is expressed in the same units. Normalizing to `[0, 1]` is recommended, because
then `δmax` reads as a fraction and transfers between instance sizes.

Guidelines:

- Measure the structure that matters **under your macro-moves**, not superficial resemblance. In
  the SCC-VRP, the article measures *where* each customer is assigned (depot and route) and
  deliberately ignores the order inside routes.
- Make it independent of arbitrary labels (route numbering, symmetric encodings).
- Keep it cheap: it is computed against every guide after every macro-move.
- Optional speed-up: also provide `double distance(const Solution& a, const Solution& b, double limit) const`.
  It may stop as soon as the distance exceeds `limit` and return any value larger than `limit`;
  when the distance is `<= limit` it must return exactly the same number as `distance(a, b)` (compute
  both with the same formula). The core compares the guide of the current region first and passes
  the closest distance found so far as `limit`, so most comparisons stop after a few elements. It
  pays off when the macro-moves are cheap: the summary line `time: ... % in the GSO-RL core` says
  how much of the run goes to the core, and suggests the bounded distance when the core takes more
  than 20 %. `--check` tests both rules, including limits
  just below the distance, where rounding errors show up.

Typical choices: Jaccard or Hamming distance on the set of selected elements (location,
selection, assignment problems); Jaccard on edge sets (routing, tours); positional distance
(permutations); Euclidean distance on vectors of real numbers (continuous problems), after scaling
each coordinate by its range so that no variable dominates. For a continuous problem, natural
macro-moves are local searches with different step sizes (small steps refine, large steps escape)
and a restart; the guide generator draws a point at random in the domain, or runs a short local
search from one.

**The radius of a region.** By default it is half the largest distance between the initial guides
(`region.delta_ratio = 0.5`), so it adapts to your distance and you do not have to set it. To fix
it in the units of your distance, set `region.delta_max`. Read the summary of a run: if case E
dominates, most solutions fall outside every region (raise the radius); if guides are almost never
replaced (A, C, D near 0 %), the regions may be too large.

## 4. Macro-moves

A macro-move is any procedure that takes the current solution and changes it. GSO-RL makes no
assumption about what each one does; the agents learn when each pays off. Examples of valid sets:

| Kind | Example |
|---|---|
| Complete search procedures | build a new solution + local search; iterated perturbation + local search; a short tabu search (the article: CI, TI, MNTS) |
| Parametrizations of one engine | the same ruin-and-recreate engine with three destruction fractions |
| Single neighborhood operators | 2-opt, Or-opt, swap |

Each macro-move is a member function `void name(Solution& s, gso::Context<P>& ctx)` (or a lambda
with the same arguments). It receives the current solution and changes it in place. Through `ctx`
it can use:

| `ctx.` | What it gives |
|---|---|
| `rng()` | the generator of the run (use it for every random decision) |
| `guide()`, `guide_cost()` | the guide of the region where this macro-move was chosen |
| `guides()`, `guide(i)`, `guide_cost(i)` | every guide of the GCM, read-only (e.g. for path relinking) |
| `best()`, `best_cost()` | the best solution found so far |
| `offer(s)` | report a solution visited along the way; kept if it is the best so far |
| `out_of_budget()` | the budget is exhausted: a long macro-move can stop early |

Rules:

- Provide **at least two** macro-moves with genuinely different effects; if they all do the same,
  there is nothing to learn.
- Mark a macro-move with **`gso::restart`** if it produces a solution unrelated to the current one
  (for example, a construction from scratch). Pending decisions are rewarded before it runs,
  because what follows no longer descends from them.
- If a macro-move passes through a solution better than the best known one, **offer it**, even if
  it finishes somewhere worse.
- Give macro-moves **comparable cost**. Agents learn reward *per decision*; if one macro-move costs
  thirty times more than another, the agent optimizes something different from "best solution
  within the budget".

## 5. Your own parameters

Add `void parameters(gso::Params& p)` and register the members your macro-moves use. They get the
prefix `problem.`, appear in `--help` and in the printed configuration, and can be set from a file
or the command line (`--problem.tabu_steps=100`), so irace can tune them with the rest.

## 6. A reward function

The default `levels(best=6, guide=0, current=1)` works for any cost. To use another one, register it once and select it by name
(see [04-learning-agents.md](04-learning-agents.md) for the same mechanism with agents):

```cpp
gso::register_reward("my_reward", [](const gso::Spec&) {
  return gso::RewardFn([](const gso::RewardInput& in) { return in.cost_before - in.cost_now; });
});
cfg.reward.type = "my_reward";
```

## Checking your adapter: `--check`

Run your program with `--check` (or call `gso::check_adapter(problem, cfg)`). It builds the guides,
tests the cost and the distance on them, runs every macro-move a few times, checks
reproducibility, and makes a short pilot run. Every line is `[ok]`, `[info]`, `[warn]` or
`[error]`. On the knapsack example:

```
# GSO-RL adapter check
[ok]    built 15 guides in 669.5 us
[ok]    cost() is finite and deterministic on every guide
[ok]    make_guide() is reproducible with the same seed
[ok]    distance() is 0 on itself, non-negative and symmetric
[info]  distance between guides: min 0.42, 25% 0.48, median 0.505, max 0.595
[info]  region.delta_max = 0.4: 0% of the guide pairs are within one region
[ok]    macro-move 'rebuild' (restart): 29.9 us per call, changed the solution in 5/5 tries
[ok]    macro-move 'perturb': 431.9 us per call, changed the solution in 5/5 tries
[ok]    macro-move 'tabu': 1.09 ms per call, changed the solution in 5/5 tries
[warn]  the slowest macro-move takes 36.4x the time of the fastest: agents learn reward per decision, not per second (docs/03)
[info]  pilot run: 10 iterations in 44.54 ms (4.45 ms per iteration)
[info]  GCM update cases: A 29% B 43% C 8% D 0% E 20%
[info]  mean |reward| = 0.846
[info]  macro-moves chosen: rebuild 27% perturb 42% tabu 31%
[info]  time in the GSO-RL core: 0.231%
# 0 error(s), 1 warning(s)
```

| It checks | It warns or fails when |
|---|---|
| cost | NaN or infinite, or different values for the same solution |
| guide generator | different guides with the same seed (randomness outside `rng`); identical guides |
| distance | `d(s, s) ≠ 0`, negative, asymmetric; a wrong bounded distance |
| `region.delta_max` | not set (it suggests a value), or most guides within one region |
| macro-moves | a cost that becomes NaN; a macro-move that never changes the solution; different results with the same seed; one 10× slower than another |
| pilot run | case E dominates; no guide is ever replaced; `gcm.tenure` longer than your budget allows; UCB1's `c` much larger than the rewards |

Fix every `[error]`; read every `[warn]`. Two things remain for you to check:

- [ ] `cost` is correct: compare it with a full recomputation on random solutions.
- [ ] The ablations of [06-experimental-protocol.md](06-experimental-protocol.md) are run, at least
      `agent.type=random`.
