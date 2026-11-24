# Figures

Figures used by the documentation. Each SVG is generated from the TikZ source with the same name
in [`src/`](src/); never edit an SVG by hand.

```bash
./build.sh        # regenerates every figure; needs TeX Live (latex) and dvisvgm
```

| Figure | Content | Used in |
|---|---|---|
| `question.svg` | a point `S` of a cost landscape and three macro-moves leaving it | README, docs/01 |
| `two-problems.svg` | revisiting exhausted regions; one fixed rule applied everywhere | docs/01 |
| `gcm-structure.svg` | guide table, one agent per ring, regions with rings | README, docs/02 §2.3 |
| `main-loop.svg` | main loop of GSO-RL | README, docs/02 §2.2 |
| `gcm-update.svg` | the five cases A–E of the GCM update | docs/02 §2.4 |
| `learning-loop.svg` | agent → macro-move → search → CCM → delayed reward | docs/02 §2.6 |
| `example-step-1.svg` … `example-step-7.svg` | one iteration, step by step (`src/example.tex`) | docs/01 |

Conventions:

- White background, so figures stay readable in GitHub's dark mode.
- English labels.
- Macro-moves are labelled `M1, M2, M3`: generic actions, not tied to what they do in one problem.
- Shared colours and macros are in `src/common.tex`. With `dvisvgm`, text written after a
  `\color{…}` change inside the same node is misplaced: put differently coloured text in separate
  nodes.

Credits: adapted from the authors' slides for CAEPIA 2026; `gcm-structure` and `gcm-update` are
redrawn after Figures 2 and 3 of the article.
