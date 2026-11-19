#!/bin/sh
# Runs the injected scoreboard script against a fake Panorama panel tree.
#
# The script is the only part of this feature that cannot be unit-tested from C++, and it
# is also the part that has no error reporting of its own: when it silently does nothing in
# game, nothing anywhere says why. This harness builds the real panel structure the CS:GO
# scoreboard uses (CSGOHud > ScoreboardContainer > Scoreboard > sb-row > data-stat="name")
# and asserts that a feed produces icons, armour, money and a panel in the first column.
set -e
here=$(cd "$(dirname "$0")" && pwd)
root=$(cd "$here/../.." && pwd)
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT

g++ -std=c++20 -O1 -I"$root" "$here/dump.cpp" \
    "$root/features/misc/general/scoreboard_script.cpp" -o "$out/dump"
"$out/dump" 100 > "$out/out.txt"
node "$here/sim.js" "$out"
