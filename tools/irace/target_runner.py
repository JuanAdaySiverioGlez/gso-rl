#!/usr/bin/env python3
"""irace target runner for any GSO-RL program (docs/07-tuning.md).

irace calls it as:   target_runner.py <configuration id> <instance id> <seed> <instance> <parameters...>
and reads one number from its output: the cost of the run (irace minimizes).

What to run comes from runner.json, looked for in the current directory (irace's execDir) and then
next to this script:

  {
    "command":    ["../build/my_problem", "{instance}"],
    "fixed":      ["--budget.iterations=200", "--output.print_config=false", "--output.print_summary=false"],
    "seed":       "--seed={seed}",
    "cost_regex": "best cost: (\\\\S+)",
    "timeout":    600
  }

Parameters arrive as  --name=value  (any GSO-RL parameter, or problem.*). Arguments of a component
arrive as  --name@arg=value  and are folded into its text spec:
  --agent.type=ucb1  --agent.type@c=0.3          ->  --agent.type=ucb1(c=0.3)
  --reward.type=eq3  --reward.type@alpha=0.6     ->  --reward.type=eq3(alpha=0.6)
so parameters.txt needs no parentheses (irace passes the parameters through a shell). The component
itself may be tuned (--agent.type=...) or fixed in runner.json (e.g. "--reward.type=eq3").
"""
import json
import os
import re
import subprocess
import sys


def fail(msg):
    print(f"target_runner: {msg}", file=sys.stderr)
    sys.exit(1)


def load_runner():
    for d in (os.getcwd(), os.path.dirname(os.path.abspath(__file__))):
        p = os.path.join(d, "runner.json")
        if os.path.exists(p):
            with open(p) as f:
                return json.load(f), d
    fail("runner.json not found (in the current directory or next to target_runner.py)")


def fold(params):
    """Turns --name=value and --name@arg=value into GSO-RL arguments."""
    base, extra, order = {}, {}, []  # a later --name=value replaces an earlier one
    for p in params:
        if not p.startswith("--") or "=" not in p:
            fail(f"parameter '{p}' is not --name=value")
        key, value = p[2:].split("=", 1)
        if "@" in key:
            name, arg = key.split("@", 1)
            extra.setdefault(name, []).append(f"{arg}={value}")
        else:
            if key not in base:
                order.append(key)
            base[key] = value
    for name in extra:
        if name not in base:
            fail(f"--{name}@... given without --{name}=<component>")
    return [f"--{k}={base[k]}" + (f"({','.join(extra[k])})" if k in extra else "") for k in order]


def main():
    if len(sys.argv) < 5:
        fail("expected: <configuration id> <instance id> <seed> <instance> [parameters...]")
    _, config_id, instance_id, seed, instance, *params = sys.argv
    runner, where = load_runner()
    subst = lambda s: s.replace("{instance}", instance).replace("{seed}", seed)
    command = [subst(c) for c in runner["command"]]
    if command and not os.path.isabs(command[0]) and os.sep in command[0]:
        command[0] = os.path.normpath(os.path.join(where, command[0]))
    args = command + [subst(runner.get("seed", "--seed={seed}"))]
    args += fold([subst(a) for a in runner.get("fixed", [])] + params)  # fixed first: tuned values win
    try:
        out = subprocess.run(args, capture_output=True, text=True, timeout=runner.get("timeout"))
    except subprocess.TimeoutExpired:
        fail(f"timeout: {' '.join(args)}")
    if out.returncode != 0:
        fail(f"exit {out.returncode}: {' '.join(args)}\n{out.stderr.strip()[-2000:]}")
    found = re.findall(runner.get("cost_regex", r"best cost: (\S+)"), out.stdout, re.MULTILINE)
    if not found:
        fail(f"no cost in the output of: {' '.join(args)}\n{out.stdout.strip()[-2000:]}")
    print(float(found[-1]))


if __name__ == "__main__":
    main()
