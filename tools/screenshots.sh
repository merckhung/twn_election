#!/usr/bin/env bash
# Regenerates docs/screenshots/ with the headless renderer (works with Mesa
# lavapipe, no GPU or display needed).
set -euo pipefail
cd "$(dirname "$0")/.."
bazel build //src/app:twn_election
BIN=bazel-bin/src/app/twn_election
OUT=docs/screenshots
mkdir -p "$OUT"
shot() { local name=$1; shift; "$BIN" --headless --screenshot="$OUT/$name.png" "$@"; }
shot 01_nation_preelection
shot 02_taipei_preelection --focus=63000 --hover=63000030
shot 03_nation_simulation --simulate --sim_progress=0.55 --hover=64000
shot 04_tainan_simulation --simulate --sim_progress=0.8 --focus=67000 --hover=67000310
shot 05_village_ja --simulate --sim_progress=0.8 --focus=67000310006 --lang=ja
shot 06_miaoli_en --simulate --sim_progress=0.6 --focus=10005 --hover=10005010 --lang=en
shot 07_referendum --simulate --sim_progress=0.7 --mode=3 --yaw=-25 --pitch=30
shot 08_taipei_ja --simulate --sim_progress=0.6 --focus=63000 --hover=63000030 --lang=ja
