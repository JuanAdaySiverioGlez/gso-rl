# templates

[`my_problem/`](my_problem/) is a blank GSO-RL adapter: copy it, fill in the `TODO`s and run.

```bash
cp -r templates/my_problem ~/my_project && cd ~/my_project
cmake -S . -B build -DGSO_RL_DIR=/path/to/gso-rl && cmake --build build
./build/my_problem --help                 # every parameter
./build/my_problem instance.txt --check   # checks the adapter; says which TODO is missing
./build/my_problem instance.txt           # solves
```

| File | What to do |
|---|---|
| `my_problem.hpp` | write the four parts of the adapter, in order: solution + cost, guide generator, distance, macro-moves ([docs/03](../docs/03-adapting-to-your-problem.md)) |
| `main.cpp` | set the budget and `region.delta_max` (`--check` suggests a value) |
| `CMakeLists.txt` | nothing, unless you add more source files |

The skeleton compiles as it is. Until a part is written, running stops with a message naming the
file and the part. [`examples/knapsack/`](../examples/knapsack/) shows every part written for a
real problem.
