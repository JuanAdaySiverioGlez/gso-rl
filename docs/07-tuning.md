# 07 — Tuning GSO-RL for your problem with irace

The defaults ([05](05-configuration.md)) are a reasonable start, not the best configuration for
every problem. [irace](https://mlopez-ibanez.github.io/irace/) searches the parameter space with
racing: it runs candidate configurations on your instances and keeps the ones that are
statistically better. `tools/irace/` contains everything it needs for any GSO-RL program.

Requirements: R with the `irace` package (`install.packages("irace")`) and Python 3.

## Files

| File | What it is | Edit it? |
|---|---|---|
| `scenario.txt` | irace settings: budget (`maxExperiments`), instance lists, parallel runs | yes |
| `parameters.txt` | the GSO-RL parameters to tune, with ranges | comment out / narrow / add `problem.*` |
| `runner.json` | how to run your program: command, fixed arguments (budget), how to read the cost | yes |
| `train-instances.txt`, `test-instances.txt` | one instance per line, as your program takes it | yes |
| `target-runner` | what irace calls (a two-line shell wrapper) | no |
| `target_runner.py` | builds the command line, runs it, prints the cost | no |

## Steps

1. **Your program** takes the instance as its first argument and prints `best cost: <number>`.
   `gso::run_main` and the template (`templates/my_problem/main.cpp`) already do both.
2. **Copy** `tools/irace/` to a tuning folder.
3. **`runner.json`**: the path of your program, the budget and anything else that stays fixed:
   ```json
   {
     "command": ["/path/to/build/my_problem", "{instance}"],
     "fixed": ["--budget.seconds=0", "--budget.macro_moves=3000", "--reward.type=eq3",
               "--output.print_config=false", "--output.print_summary=false"],
     "seed": "--seed={seed}",
     "cost_regex": "best cost: (\\S+)",
     "timeout": 600
   }
   ```
   Use a budget in **macro-moves** (`--budget.macro_moves`): runs are reproducible, do not depend on
   the load of the machine while irace runs many of them in parallel, and cost the same whatever
   `search.region_iters` is. With a budget in iterations (regional searches), irace learns to raise
   `search.region_iters`, which buys more macro-moves for the same budget instead of a better
   configuration.
4. **Instances**: tune on `train-instances.txt`, evaluate on **different** instances
   (`test-instances.txt`). A configuration tuned and evaluated on the same instances is optimistic.
5. **`parameters.txt`**: comment out what you do not want to tune, narrow the ranges you know, and add
   your adapter's parameters (`problem.*`), for example
   `ti_rounds "--problem.ti_rounds=" i (1, 25)`.
6. **Run** `irace --scenario scenario.txt` (or `Rscript -e 'irace::irace_cmdline()' --scenario scenario.txt`).
   irace prints the best configurations as command lines; pass them to your program as they are
   (`target_runner.py` shows how the `@` arguments become component specs).
7. **Evaluate** the best configuration on the test instances with several seeds, against the
   defaults and against `--agent.type=random` (docs/06).

## Components and their arguments

irace passes parameters through a shell, so `parameters.txt` avoids parentheses. A component and
its arguments are separate parameters; `target_runner.py` joins them:

```
agent_type   "--agent.type="      c (thompson, ucb1, epsilon_greedy, ...)
ucb1_c       "--agent.type@c="    r,log (0.01, 2.0)    | agent_type == "ucb1"
```
`--agent.type=ucb1 --agent.type@c=0.3` reaches your program as `--agent.type=ucb1(c=0.3)`. A component
can also be fixed in `runner.json` (the reward: `"--reward.type=eq3"`) while irace tunes its arguments
(`--reward.type@alpha=`).

## Comparing variants fairly

To compare two variants of GSO-RL — learning against random choices, one agent per region against a
single agent (`agent.scope`), two reward functions — **tune each of them with the same irace budget**
and compare the tuned configurations on the test instances. Comparing a tuned variant with an
untuned one measures the tuning, not the variant. Fix the variant in `runner.json` (for example
`"--agent.scope=global"` or `"--agent.type=random"`, and remove the agent parameters from
`parameters.txt` for the random one).

## Budget of the tuning

irace needs at least a few hundred runs to start (it says how many if `maxExperiments` is too
small); 1 000–5 000 is typical for the parameters above. As a reference, the shipped example
(`runner.json` for `examples/knapsack`, 600 macro-moves per run, `maxExperiments = 600`, 8 runs in
parallel) takes a few minutes.

## Known issue

R sets `LD_LIBRARY_PATH` for its own libraries; a Python from conda or Homebrew can then fail to
start inside irace (`GLIBC_2.34 not found`). `target-runner` clears that variable before calling
Python.
