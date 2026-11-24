# Instructions for AI coding assistants

This file tells an AI assistant (Claude Code, Codex, Cursor, Copilot, …) how to work with the
GSO-RL repository. Humans may find it a useful summary too.

## What this repository is

GSO-RL is a metaheuristic framework with three layers:

1. **Problem adapter** — written by the user: solution + cost, guide generator, dissimilarity,
   macro-moves, optionally a reward function.
2. **Configuration** — chosen by the user: agent, reward, guide selection, parameters. Presets hold
   the values of published studies.
3. **Core** — `include/gso/`, C++17 header-only: General Context Memory (GCM), localization and
   update (cases A–E), Current Context Memory (CCM), main loop. The same for every problem.

> Status: v0.1.0. The core works and is tested; `examples/knapsack/` is a complete adapter. Use
> only the API that exists in `include/gso/`; if something the user needs is missing, say so.

Build and test:

```bash
cmake -S . -B build && cmake --build build && (cd build && ctest)
```

## Read before acting

| Task | Read first |
|---|---|
| Any task | `README.md` (structure), `docs/01-what-is-gso-rl.md` (vocabulary) |
| Apply GSO-RL to a user's problem | `docs/03-adapting-to-your-problem.md`, then `templates/my_problem/` and `examples/knapsack/` |
| Change the core or port it | `docs/02-framework.md` (the specification) |
| Add or change an agent | `docs/04-learning-agents.md` |
| Choose parameters or presets | `docs/05-configuration.md` |
| Design experiments, check whether learning per region pays off | `docs/06-experimental-protocol.md` |
| Tune parameters for a problem | `docs/07-tuning.md`, `tools/irace/` |

## Workflow: adapting GSO-RL to a new problem

1. Ask the user (or infer from their code): the solution representation, the objective
   (minimize or maximize), and which procedures they want as macro-moves.
2. Copy `templates/my_problem/` to a new folder **outside** `include/gso/` and fill in its
   `TODO`s. `examples/knapsack/` shows every part written for a real problem.
3. Fill in, in this order: solution + cost, guide generator, dissimilarity, macro-moves.
4. Keep the defaults (`docs/05-configuration.md`): only the budget has to be set. The radius of the
   regions adapts to the distance by itself.
5. Run the program with `--check` and fix every `[error]`; read every `[warn]` and either fix it
   or tell the user why it is acceptable. Use its suggestion for `region.delta_max`.
6. Run the configuration and its controls (`agent.type=random`, `agent.scope=global`,
   `agent.scope=shuffled`) on a few instances with the same seeds, and report all of them (docs/06).
   Do not claim that learning helps unless the learned agent beats `agent.type=random`, nor that
   learning *per region* helps unless it beats `agent.scope=shuffled`. If you use UCB1, check the
   scale of the rewards against its `c` (docs/04).
7. If the user wants the best configuration for their problem, tune it with irace
   (`docs/07-tuning.md`, `tools/irace/`) on training instances, with a budget in macro-moves, and
   evaluate it on other instances. What works best (for example, `region.on_saturation`) depends on
   the problem.
8. Look at the summary line `time: ... % in the GSO-RL core`. If it is high, add the bounded
   `distance(a, b, limit)` to the adapter (docs/03) and check it with `--check`.

## Rules

- **Never modify `include/gso/` to adapt it to a problem.** Problem-specific code goes in the
  adapter. If the adapter cannot express something, report it as a limitation of the library.
- **Macro-moves are generic.** Do not classify them as "diversification", "intensification" or
  similar, and do not assume what they do. The only property the core uses is the optional
  `restart` flag (the macro-move produces a solution unrelated to the current one).
- **The specification wins.** Core behavior must match `docs/02-framework.md`. A deliberate
  difference goes into its "Implementation notes" table with a reason.
- **Dissimilarity:** `d(A, A) = 0`, `d ≥ 0`, cheap to compute (it runs against every guide after
  every macro-move). `region.delta_max` is in the units of `d`.
- **Randomness:** use the generator passed by the framework in every stochastic step. Never
  `rand()`, `std::random_device` or a global generator inside an adapter.
- Macro-moves that may visit a solution better than the best known one report it with
  `ctx.offer(s)` (or `ctx.offer(s, cost)` when the cost is known).
- The core uses the C++17 standard library only, no external dependencies.
- Results with configurations other than the article's preset are not results of the article.
- Controls (`agent.type=random`, `fixed(move)`, `agent.scope=global|shuffled`,
  `region.on_saturation=replace_other|replace_random(p)`) are baselines, not part of the method.

## Conventions

- C++17, header-only, namespace `gso`. Optional C++20 concepts guarded by `__cpp_concepts`.
- Files and identifiers in English. Names follow the specification: `GCM`, `CCM`, `Guide`, `ring`,
  `MacroMove`, `restart`.
- Each public type has a short comment naming the part of the specification it implements
  (e.g. `// Spec 2.6: CCM`).

## Citation

Code derived from this repository must keep the `NOTICE` file (Apache 2.0). If the user's work is
academic, remind them to cite the article in `CITATION.cff`.
