# tests

`test_core.cpp` checks the core without external dependencies: random generator, parameters and
specs, agents on synthetic bandits, rewards, guide selection, every GCM update case A–E, the
Current Context Memory, determinism of a full run with the same seed, custom components,
`gso::configure`, `gso::check_adapter` (on adapters with deliberate mistakes) and `gso::run_main`.

`ctest` also builds the blank template and checks that it names its missing parts, and runs the
knapsack example with `--check` and with a short budget.

```bash
cmake -S . -B build && cmake --build build && (cd build && ctest)
```
