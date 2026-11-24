# 4. Learning agents

An agent chooses which macro-move to apply in one ring of one region. The framework keeps
`gcm.size × region.rings` independent agents and decides what happens to them when their guide is
replaced ([02-framework.md](02-framework.md), Section 2.4).

## What an agent sees

In the article each agent is a **stateless bandit**: conditioned on its ring, it repeatedly chooses
among the same macro-moves, with no state transitions to model. The context is *which ring it
belongs to*, not a feature vector. This is the setting for which bandit policies such as UCB1 are
designed.

Rewards arrive **late**: a decision is rewarded when it leaves the Current Context Memory (CCM),
`ccm.size` macro-moves after it was taken. The built-in agents therefore count a *pull* when they
choose a macro-move and update its mean when the reward arrives (note N8 of the specification).

## Choosing an agent

The agent is a parameter, so the same binary runs every variant (useful for ablations and irace):

```
agent.type = ucb1(c=1.0)              # in a file
--agent.type=random                   # on the command line
cfg.agent.type = "epsilon_greedy(eps=0.1)";   // in code
```

| `agent.type` | Policy | Scale-free | Rewards that change |
|---|---|---|---|
| `ucb1(c=1.0)` | `argmax mean(m) + c·sqrt(2 ln N / n_m)`; untried macro-moves first. **The article's agent** | no (`c` in reward units) | slowly |
| `epsilon_greedy(eps=0.1)` | best mean with probability `1 − ε`, random otherwise | yes | no |
| `softmax(tau=0.1)` | Boltzmann: `P(m) ∝ exp(mean(m) / τ)` | no (`τ` in reward units) | partly |
| `thompson` | Thompson sampling, Gaussian model: draws `mean(m) + sd(m)/sqrt(n_m)·N(0,1)`, takes the largest; `sd` estimated from the rewards | **yes** | no |
| `exp3(gamma=0.1)` | EXP3: exponential weights with `γ` uniform exploration; rewards rescaled to [0, 1] by their observed range | **yes** | no (built for adversarial rewards) |
| `sliding_window_ucb(c=0.5, window=300)` | UCB1 on the last `window` rewards and choices only | no | **yes** |
| `discounted_ucb(c=0.5, gamma=0.99)` | UCB1 with statistics multiplied by `γ` at every step | no | **yes** |
| `gradient(alpha=0.1)` | gradient bandit: preferences `H(m)`, `P(m) = softmax(H)`, updated with the reward's deviation from its running mean, divided by its running standard deviation | **yes** | no |
| `random` | uniform choice. **The article's ablation GSO-Random** | — | — |
| `fixed(move=<name or index>)` | always the same macro-move: baseline per macro-move | — | — |

*Scale-free*: the parameters do not depend on the size of the rewards (see below). *Rewards that
change*: the agent forgets old evidence, which matters when one agent lives long and the value of a
macro-move changes during the search. In GSO-RL agents are reset whenever their guide is replaced
(unless `agent.on_replace=decay`), which already limits how old their evidence gets.

Results obtained with agents other than `ucb1` are not results of the article.

### How they behave on a synthetic bandit

Share of choices of the best arm on a 3-armed Bernoulli bandit (success probabilities 0.2, 0.5, 0.8),
3000 steps, mean of 20 seeds. *swap*: the best and worst arms exchange at step 1500, measured on the
last 500 steps. *×10⁻³*: every reward multiplied by 0.001. *delay 30*: every reward arrives 30
steps late, as in the Current Context Memory. Random choice gives 0.33. Regenerate it with
`./build/bandit_benchmark` ([tests/bandit_benchmark.cpp](../tests/bandit_benchmark.cpp)).

| agent.type | stationary | swap | x1e-3 | delay 30 |
|---|---|---|---|---|
| `ucb1(c=1.0)` | 0.95 | 0.95 | 0.33 | 0.96 |
| `epsilon_greedy(eps=0.1)` | 0.93 | 0.14 | 0.93 | 0.92 |
| `softmax(tau=0.1)` | 0.98 | 0.74 | 0.33 | 0.94 |
| `thompson` | 0.99 | 0.35 | 0.99 | 0.99 |
| `exp3(gamma=0.1)` | 0.90 | 0.20 | 0.90 | 0.88 |
| `sliding_window_ucb(c=0.5, window=300)` | 0.92 | 0.93 | 0.34 | 0.88 |
| `discounted_ucb(c=0.5, gamma=0.99)` | 0.84 | 0.85 | 0.33 | 0.83 |
| `gradient(alpha=0.1)` | 0.84 | 0.16 | 0.84 | 0.83 |
| `random` | 0.33 | 0.33 | 0.33 | 0.33 |

A synthetic bandit is not a search: these numbers describe how each agent learns, not which one
makes GSO-RL find better solutions. That has to be measured on your problem, against
`agent.type=random`.

## What happens when a guide is replaced

| `agent.on_replace` | Effect |
|---|---|
| `reset` | the agents of that guide start from zero (the article) |
| `decay(lambda=0.5)` | they keep a fraction `λ` of their experience: useful when a region only drifts (case A); the reference implementation of the article used `decay(lambda=0.5)` |

## The scale of the reward matters

UCB1 adds an exploration bonus `c·sqrt(2 ln N / n_m)` to the mean reward. That bonus is of order 1,
while Eq. 3 produces rewards of order `10⁻³`–`10⁻²` (relative improvements). With `c = 1` the bonus
dominates and UCB1 chooses almost uniformly. On the knapsack example with `reward.type=eq3(alpha=0.75)`
(300 iterations, one seed):

| `agent.type` | rebuild | perturb | tabu |
|---|---|---|---|
| `ucb1(c=1.0)` | 31.5 % | 35.8 % | 32.7 % |
| `ucb1(c=0.01)` | 8.2 % | 49.0 % | 42.9 % |
| `random` | 32.6 % | 34.7 % | 32.7 % |

The defaults avoid the problem: `thompson` is scale-free. Otherwise, three ways out: a scale-free
agent (`thompson`, `gradient`, `exp3`, `epsilon_greedy`); `c` (or `τ`)
of the size of your rewards; or a reward of order 1 such as `reward.type=levels` — the 6/1/0 reward
used in the article's experiments is `levels(best=6, guide=0, current=1)`, and with it `ucb1(c=1.0)`
is well scaled. `--check` measures the mean |reward| in a pilot run and warns when UCB1's `c` is much
larger.

## Adding your own agent

1. Write a class derived from `gso::Agent` (or from `gso::BanditAgent`, which already counts pulls
   and rewards):

   ```cpp
   class MyAgent : public gso::BanditAgent {
    public:
     MyAgent(int moves, double x) : BanditAgent(moves), x_(x) {}
     int select(gso::Rng& rng) override { /* choose m */ return record(m); }
    private:
     double x_;
   };
   ```

   The interface has four operations: `select(rng)`, `update(move, reward)`, `reset()`,
   `decay(lambda)`.

2. Register it once, before creating the solver, and select it by name:

   ```cpp
   gso::register_agent("my_agent", [](const gso::Spec& spec, const std::vector<std::string>& moves) {
     spec.allow_only({"x"});
     return std::unique_ptr<gso::Agent>(new MyAgent(int(moves.size()), spec.number("x", 1.0)));
   });
   cfg.agent.type = "my_agent(x=2)";
   ```

3. If it should become part of the library: add it to `include/gso/agents.hpp`, a test in
   `tests/`, and a row to the table above.

## Beyond bandits (future)

- **Contextual agents:** `select` also receives features of the current situation (ring, distance
  to the guide, time since the last improvement).
- **External agents (Python):** a bridge that exposes the decision step as a Gymnasium-style
  environment, so policies trained with Python reinforcement-learning libraries can drive the C++
  search.
