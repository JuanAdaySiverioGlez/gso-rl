# Guided strategic oscillation with reinforcement learning for the SCC-VRP

**J. A. Siverio-González, B. Melián-Batista, J. M. Moreno-Vega.**
*Expert Systems with Applications* 334 (2027) 134651. Available online 6 October 2026.
DOI: [10.1016/j.eswa.2026.134651](https://doi.org/10.1016/j.eswa.2026.134651)

- **PDF:** [`paper.pdf`](paper.pdf) (version of record).
- **License of the article:** CC BY-NC 4.0 (open access). You may share and adapt it for
  non-commercial purposes with attribution. This license applies to the article only; the code of
  this repository is under Apache 2.0.
- **Instances (S3, S3E):** [doi:10.5281/zenodo.20267549](https://www.doi.org/10.5281/zenodo.20267549)

## Summary

The Shared Customer Collaboration Vehicle Routing Problem (SCC-VRP) is an NP-hard collaborative
logistics problem in which competing carriers jointly serve shared customers to minimize total
distribution cost. The article proposes GSO-RL, which replaces the predefined phase-transition
rules of strategic oscillation with independent multi-armed bandit agents. The agents learn
region-specific macro-move policies from the performance history accumulated around each guiding
solution; the guides evolve during the search.

**Results.** GSO-RL reaches the best-known solutions on the benchmark sets S1 and S2 and obtains
the best value among the three compared algorithms in 22 of the 24 large-scale instances (sets S3
and S3E, introduced in the article), improving on GRASP×ILS and a multi-neighborhood tabu search
(MNTS) by average margins of −1.10 % and −3.24 % on S3, and −1.89 % and −1.98 % on S3E.

**Ablation.** The components influence solution quality mainly on the large-scale sets, where the
bandit selection policy (UCB1) is the decisive contributor and the delayed-reward memory (CCM) adds
a smaller, statistically weaker gain. On small instances all variants reach the same quality and
the learning components only reduce computation time.

## Where each part of the article lives in this repository

| Article | Repository |
|---|---|
| Section 4.2, Algorithm 2, Figures 2–3, Eqs. (2)–(3) | [docs/02-framework.md](../../02-framework.md) |
| Sections 4.1–4.2, the SCC-VRP solver and the macro-moves CI, TI, MNTS | not included in this repository |
| Tables 1–2, parameters | [docs/05-configuration.md](../../05-configuration.md) |
| Section 5.5.5, ablation study | [docs/06-experimental-protocol.md](../../06-experimental-protocol.md) |
