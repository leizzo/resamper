#!/usr/bin/env bash
# Deterministic lint for what a branch changes. Exits non-zero on any finding.
#
#   scripts/lint.sh            # against origin/main
#   scripts/lint.sh <base-ref> # against another ref
#
# clang-tidy looks only at the lines changed since <base-ref> (staged new files
# included), so existing code is never re-litigated; it reads
# build/compile_commands.json, which any configured build has. Semgrep checks
# every file: Source/ has no matches left except marked exceptions. A rule
# still being migrated to (metadata changed-lines-only) checks changed lines only.
#
# There is no clang-format step: no clang-format setting reproduces the JUCE
# lambda braces and hand-aligned lists in Source/, so it would push the wrong style.
set -uo pipefail

base="${1:-origin/main}"
cd "$(git rev-parse --show-toplevel)"

llvm="$(brew --prefix llvm 2>/dev/null || true)"
tool() { command -v "$1" 2>/dev/null || { [ -x "$llvm/bin/$1" ] && echo "$llvm/bin/$1"; } || true; }

clang_tidy="$(tool clang-tidy)"
clang_tidy_diff="$llvm/share/clang/clang-tidy-diff.py"

if [ -z "$clang_tidy" ] || [ ! -f "$clang_tidy_diff" ] || ! command -v semgrep > /dev/null; then
    echo "lint: needs LLVM and Semgrep:  brew install llvm && uv tool install semgrep"
    exit 2
fi

merge_base="$(git merge-base "$base" HEAD)" || { echo "lint: unknown ref $base"; exit 2; }
failed=0

echo "== semgrep (.semgrep/)"
semgrep scan --config .semgrep --json --metrics=off --disable-version-check --quiet . \
    | python3 scripts/semgrep-changed-lines.py "$merge_base" || failed=1

echo "== clang-tidy (changed lines since $base)"
if [ ! -f build/compile_commands.json ]; then
    echo "lint: build/compile_commands.json missing; run  cmake -S . -B build -G Ninja"
    failed=1
else
    sysroot=()
    [ "$(uname)" = Darwin ] && sysroot=(-extra-arg="-isysroot$(xcrun --show-sdk-path)")
    git diff -U0 --no-color "$merge_base" -- 'Source/*.cpp' 'Source/*.h' 'Source/*.mm' \
        | python3 "$clang_tidy_diff" -p1 -path build -clang-tidy-binary "$clang_tidy" -quiet \
              -j "$(sysctl -n hw.ncpu 2>/dev/null || nproc)" "${sysroot[@]}" \
        || failed=1
fi

[ "$failed" = 0 ] && echo "lint: clean" || echo "lint: FAILED"
exit "$failed"
