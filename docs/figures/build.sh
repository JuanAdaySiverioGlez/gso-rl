#!/usr/bin/env bash
# Builds every documentation figure: src/<name>.tex -> <name>.svg
# Requires: latex (TeX Live) and dvisvgm. Usage: ./build.sh  (from any directory)
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
build="$(mktemp -d)"
trap 'rm -rf "$build"' EXIT
cd "$here/src"

compile() {  # compile <jobname> <tex code>
  latex -interaction=nonstopmode -halt-on-error -output-directory="$build" -jobname="$1" "$2" > "$build/$1.out" \
    || { tail -30 "$build/$1.log"; echo "FAILED: $1" >&2; exit 1; }
  dvisvgm --no-fonts --exact-bbox --zoom=1.4 -o "$here/$1.svg" "$build/$1.dvi" > /dev/null 2>&1
  echo "built $1.svg"
}

for f in question two-problems gcm-structure learning-loop gcm-update main-loop; do
  compile "$f" "\input{$f.tex}"
done
for s in 1 2 3 4 5 6 7; do
  compile "example-step-$s" "\def\paso{$s}\input{example.tex}"
done
