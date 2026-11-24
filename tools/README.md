# tools

| Folder | What it does | Documentation |
|---|---|---|
| [irace/](irace/) | Tunes the parameters of any GSO-RL program with irace | [docs/07](../docs/07-tuning.md) |

It works with any program that takes the instance as its first argument and prints
`best cost: <number>`, as `gso::run_main` and the template (`templates/my_problem/`) do.
