# 6. Experimental protocol

How to evaluate a GSO-RL adaptation so that the results say something about GSO-RL, and not about
chance or about the procedures used as macro-moves.

## Reproducibility

- Every run takes an explicit seed; repetition `r` uses `seed_base + r`.
- The solver prints its full configuration at start and writes it next to the results.
- Keep one frozen configuration file per table or figure you publish.
- Report the budget in a machine-independent unit (iterations, evaluations) as well as in
  seconds, and the CPU used.

## Does learning per region pay off on your problem?

The hypothesis behind GSO-RL is that *learning which macro-move to apply in each region* helps.
Whether it holds depends on the problem: it can only help where different regions of the search
space need different macro-moves. Check it on yours. Each part of the method has a control that
removes it and keeps everything else:

| Arm | Configuration | Question it answers |
|---|---|---|
| **GSO-Random** (article) | `agent.type=random` | does the learned choice of macro-move matter? |
| **GSO-WithoutCCM** (article) | `ccm.size=0` | does the delayed reward matter? |
| Each macro-move alone | `agent.type=fixed(move=<name>)` | is the gain due to learning, or to one good macro-move? |
| One agent | `agent.scope=global` | do agents per region beat a single agent for the whole search? |
| **Shuffled agents** | `agent.scope=shuffled` | **does the region matter?** |
| One agent per guide | `region.rings=1` | do the rings matter? |

The first two are the ablation of the article (Section 5.5.5).

**Why the shuffled control matters.** Agents per region can beat a single agent for reasons that
have nothing to do with regions: several small learners explore more, and commit to one macro-move
less, than one learner with all the experience. `agent.scope=shuffled` keeps as many agents as
`region`, and each one receives as much experience, but every decision is taken by an agent drawn
at random: the agents are not tied to any region. If `region` beats `global` but not `shuffled`,
the gain came from having several learners, not from the regions.

**Abandoning exhausted regions** (`region.on_saturation=replace`) has two controls of its own:
`replace_other` abandons, at the same moments, another region chosen at random (does it matter
*which* region is abandoned?), and `replace_random(p)` abandons a region at random with probability
`p` after each regional search (does it matter *when*?). Take `p` = regions abandoned / regional
searches of the `replace` run; the summary reports both.

### Running the comparison

Run your configuration and each control on the same instances and the same seeds, changing only
the configuration line of the control, with a budget in macro-moves so that every run does the same
work:

```bash
for seed in $(seq 1 10); do
  for arm in "" "--agent.type=random" "--agent.scope=global" "--agent.scope=shuffled"; do
    ./my_problem instance.txt --seed=$seed --budget.iterations=0 --budget.macro_moves=3000 \
        --output.print_config=false $arm | grep "best cost"
  done
done
```

Then compare each control with your configuration run by run (same instance and seed): count
wins, ties and losses, and use the paired Wilcoxon signed-rank test with Holm's correction for the
number of comparisons. The summary of each run also reports the share of each macro-move chosen.

### Reading the result

| What you see | What it means | What to try |
|---|---|---|
| your configuration ≈ random | the choice of macro-move does not matter here, or the agent does not learn it | check the scale of the rewards (docs/04); macro-moves that differ more in what they do |
| better than random, ≈ the best macro-move alone | learning finds the good macro-move without knowing it in advance, but one macro-move is enough | it is still useful when you do not know which one; otherwise, macro-moves that complement each other |
| better than `global`, ≈ `shuffled` | several learners help; the regions add nothing: the same macro-moves are best everywhere | another definition of region (dissimilarity, `region.delta_ratio`, rings) that separates situations that need different macro-moves |
| better than `shuffled` | **the regions carry information**: this is the evidence that learning per region pays off on your problem | compare the macro-moves chosen in each ring (summary) to see what the regions learned |

Before a final comparison, tune each arm with the same irace budget ([07](07-tuning.md)): a tuned
configuration against untuned controls measures the tuning, not the method.

Report every comparison, including those where your configuration does not win: on that problem,
the memory of guides and the set of macro-moves may still be useful while the learning adds
nothing.

## Statistics

- At least 10 independent runs per instance and variant, under the same budget (as in the
  article).
- Compare algorithms across instances with the Friedman test, then pairwise post-hoc tests
  (Nemenyi, and Wilcoxon signed-rank with Holm correction), and report effect sizes
  (matched-pairs rank-biserial correlation), as in the article.
- Report the gap to the best-known solution, not only the average cost.
