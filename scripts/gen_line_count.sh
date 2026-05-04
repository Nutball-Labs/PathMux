#!/usr/bin/env bash
# gen_line_count.sh — Daily line count snapshot for PathMux
# Writes Line_Counts/line_count_YYYYMMDD.md in the project root.
# Scheduled via crontab: 0 1 * * *

set -uo pipefail
PROJ="$(cd "$(dirname "$0")/.." && pwd)"
DATE="$(date +%Y%m%d)"
DISPLAY_DATE="$(date '+%Y-%m-%d')"
OUT="$PROJ/Line_Counts/line_count_${DATE}.md"

cd "$PROJ"

# Count lines in a list of files (pre-expanded by caller); emit "N path" sorted desc
scan() {
    for f in "$@"; do
        [ -f "$f" ] || continue
        printf '%d %s\n' "$(wc -l < "$f")" "$f"
    done | sort -rn
}

# Sum first column of "N path" lines (awk always exits 0)
sumlines() { awk '{s+=$1} END {print s+0}'; }

# Format integer with comma thousands separator
fmt() {
    local n=$1
    if   [ "$n" -ge 1000000 ]; then
        printf '%d,%03d,%03d' $(( n/1000000 )) $(( n/1000%1000 )) $(( n%1000 ))
    elif [ "$n" -ge 1000 ]; then
        printf '%d,%03d' $(( n/1000 )) $(( n%1000 ))
    else
        printf '%d' "$n"
    fi
}

# Print markdown table rows from "N path" input; strip project root prefix
rows() {
    while read -r n path; do
        printf '| `%s` | %s |\n' "${path#$PROJ/}" "$(fmt "$n")"
    done
}

# Print non-blank lines from multiple variables (avoids grep exit-1 on empty)
nonblank() { printf '%s\n' "$@" | awk 'NF'; }

# ---------------------------------------------------------------------------
# Collect all sections (globs expanded at call site)
# ---------------------------------------------------------------------------
gui_cpp=$(scan  gui/*.cpp   2>/dev/null || true)
gui_h=$(scan    gui/*.h     2>/dev/null || true)
cli_cpp=$(scan  cli/*.cpp   2>/dev/null || true)
cli_hpp=$(scan  cli/*.hpp   2>/dev/null || true)
lib_cpp=$(scan  lib/*.cpp   2>/dev/null || true)
lib_hpp=$(scan  lib/*.hpp   2>/dev/null || true)
tools_cpp=$(scan tools/*.cpp 2>/dev/null || true)
scripts_py=$(scan scripts/*.py 2>/dev/null || true)
cmake=$(scan    CMakeLists.txt 2>/dev/null || true)
manpages=$(scan man1/*.1    2>/dev/null || true)

# Markdown docs — only include files that exist
md_files=()
for f in ROADMAP.md CHANGELOG.md Session_Log.md CLAUDE.md README.md \
          PROPOSED_UTILS.md ROADMAP_MacOS.md pathmux_project_brief.md \
          ROADMAP_WINDOWS.md; do
    [ -f "$f" ] && md_files+=("$f")
done
docs_md=$(scan "${md_files[@]}" 2>/dev/null || true)

# ---------------------------------------------------------------------------
# Compute totals
# ---------------------------------------------------------------------------
t_src_cpp=$(nonblank "$gui_cpp"  "$cli_cpp"  "$lib_cpp"  "$tools_cpp" | sumlines)
t_src_h=$(  nonblank "$gui_h"   "$cli_hpp"  "$lib_hpp"               | sumlines)
t_py=$(     nonblank "$scripts_py"                                     | sumlines)
t_cmake=$(  nonblank "$cmake"                                          | sumlines)
t_md=$(     nonblank "$docs_md"                                        | sumlines)
t_man=$(    nonblank "$manpages"                                       | sumlines)
t_grand=$(( t_src_cpp + t_src_h + t_py + t_cmake + t_md + t_man ))

# ---------------------------------------------------------------------------
# Write markdown
# ---------------------------------------------------------------------------
{
cat <<HEADER
# PathMux Line Count — ${DISPLAY_DATE}

## Grand Total: $(fmt $t_grand) lines

| Category | Lines |
|---|---|
| C++ source (\`.cpp\`) | $(fmt $t_src_cpp) |
| C++ headers (\`.hpp\` / \`.h\`) | $(fmt $t_src_h) |
| Python scripts (\`.py\`) | $(fmt $t_py) |
| Markdown (\`.md\`) | $(fmt $t_md) |
| \`CMakeLists.txt\` | $(fmt $t_cmake) |
| Man pages (\`.1\`) | $(fmt $t_man) |
| **Total** | **$(fmt $t_grand)** |

---

## By File

### GUI (\`gui/\`)

| File | Lines |
|---|---|
HEADER
nonblank "$gui_cpp" "$gui_h" | sort -rn | rows

cat <<SEC2

### CLI (\`cli/\`)

| File | Lines |
|---|---|
SEC2
nonblank "$cli_cpp" "$cli_hpp" | sort -rn | rows

cat <<SEC3

### Library (\`lib/\`)

| File | Lines |
|---|---|
SEC3
nonblank "$lib_cpp" "$lib_hpp" | sort -rn | rows

cat <<SEC4

### Tools (\`tools/\`)

| File | Lines |
|---|---|
SEC4
nonblank "$tools_cpp" | rows

cat <<SEC5

### Scripts (\`scripts/\`)

| File | Lines |
|---|---|
SEC5
nonblank "$scripts_py" | rows

cat <<SEC5B

### Build System

| File | Lines |
|---|---|
SEC5B
nonblank "$cmake" | rows

cat <<SEC6

### Documentation (\`.md\`)

| File | Lines |
|---|---|
SEC6
nonblank "$docs_md" | rows

cat <<SEC7

### Man Pages (\`man1/\`)

| File | Lines |
|---|---|
SEC7
nonblank "$manpages" | rows

} > "$OUT"

echo "$(date '+%Y-%m-%d %H:%M:%S')  Written: $OUT" >&2
# SN: 00090
