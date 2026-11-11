#!/bin/bash
# usage: run_battle.sh <outdir> <seconds> [preset] ; extra env passed through. Samples memory to mem.log every 10 s.
OUT="$1"; SECS="$2"; PRESET="${3:-win-clang-release}"
ROOT="/j/El Señor de los Anillos Las Dos Torres Recompilado"
mkdir -p "$OUT"
export TTR_HACK_SKIPPABLE_FMV=1
export TTR_PAD_SCRIPT="${TTR_PAD_SCRIPT:-1100:cross;3000-3300:ly=0;3400:square}"
export TTR_FRAME_DUMP="$OUT"
export TTR_FRAME_DUMP_EVERY="${TTR_FRAME_DUMP_EVERY:-100}"
cd "$ROOT"
"$ROOT/out/build/$PRESET/TwoTowersRecomp/TwoTowersRecomp.exe" > "$OUT/run.log" 2>&1 &
PID=$!
: > "$OUT/mem.log"
for ((t=0; t<SECS; t+=10)); do
  sleep 10
  echo "t=$((t+10)) $(tasklist //FI "IMAGENAME eq TwoTowersRecomp.exe" //FO CSV //NH | cut -d, -f5-)" >> "$OUT/mem.log"
done
kill $PID 2>/dev/null; sleep 1; taskkill //F //IM TwoTowersRecomp.exe >/dev/null 2>&1
echo done
