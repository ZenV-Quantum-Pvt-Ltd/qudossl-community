#!/bin/sh
# scripts/check-export-map.sh — every public API symbol must be exported.
#
# WHY THIS EXISTS
# ---------------
# config/libqudo-pqc.map is an ELF version script with a `local: *;` catch-all,
# so a symbol is exported ONLY if it is named (or prefix-matched) in `global:`.
# Declaring a function QUDO_PQC_API in include/qudo_pqc.h does NOT export it.
#
# The two are maintained by hand and drift silently. When they drift, nothing
# fails at compile time -- the symbol is simply hidden, and the first sign is an
# "undefined reference" when an external consumer links the SHARED library.
#
# Worse, the version script is applied under `if(NOT APPLE)`, so the failure is
# invisible on macOS and only appears on Linux. That is exactly how
# qudo_pqc_set_rand_provider (Story 2.1) shipped hidden: the function, its
# header declaration and its test all landed, but the map entry did not, and the
# break only surfaced in Linux CI.
#
# This asserts the two agree.
set -eu

ROOT=$(cd "$(dirname "$0")/.." && pwd)
HDR="$ROOT/include/qudo_pqc.h"
MAP="$ROOT/config/libqudo-pqc.map"

[ -f "$HDR" ] || { echo "FAIL: header not found: $HDR" >&2; exit 1; }
[ -f "$MAP" ] || { echo "FAIL: map not found: $MAP" >&2; exit 1; }

TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT

# Functions declared QUDO_PQC_API in the public header.
grep -hoE "QUDO_PQC_API[[:space:]]+[a-zA-Z_][a-zA-Z0-9_ *]*[[:space:]]+\**([a-z_0-9]+)[[:space:]]*\(" "$HDR" \
  | sed -E 's/.*[ *]([a-z_0-9]+)[[:space:]]*\($/\1/' | sort -u > "$TMP/declared"

# Symbols explicitly named in the map's global: section.
sed -n '/global:/,/local:/p' "$MAP" \
  | grep -oE "^[[:space:]]+[a-z_][a-z_0-9]*;" | tr -d ' ;' | sort -u > "$TMP/exported"

# Wildcard prefixes in global: (e.g. QUDO_KEM_*) cover their families.
sed -n '/global:/,/local:/p' "$MAP" \
  | grep -oE "^[[:space:]]+[A-Za-z_][A-Za-z_0-9]*\*;" | tr -d ' ;*' > "$TMP/prefixes" || true

missing=""
while IFS= read -r sym; do
    grep -qx "$sym" "$TMP/exported" && continue
    covered=0
    while IFS= read -r p; do
        [ -n "$p" ] || continue
        case "$sym" in "$p"*) covered=1; break;; esac
    done < "$TMP/prefixes"
    [ "$covered" -eq 1 ] || missing="$missing $sym"
done < "$TMP/declared"

n_decl=$(wc -l < "$TMP/declared" | tr -d ' ')

if [ -n "$missing" ]; then
    echo "FAIL: public API symbol(s) declared QUDO_PQC_API but NOT exported by" >&2
    echo "      config/libqudo-pqc.map. On Linux the version script hides these," >&2
    echo "      so consumers get 'undefined reference' at link time:" >&2
    for s in $missing; do echo "        $s" >&2; done
    echo "" >&2
    echo "  Fix: add each to the global: section of config/libqudo-pqc.map." >&2
    exit 1
fi

echo "OK: all $n_decl public QUDO_PQC_API symbols are exported by the map."
