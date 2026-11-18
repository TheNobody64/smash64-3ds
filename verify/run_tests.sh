#!/bin/sh
# Reproduces every check quoted in PERF-NOTES.md / FINDINGS.md on an x86 Linux/WSL box with gcc.
# Usage:  BATTLESHIP=/path/to/builder/3ds/vendor/BattleShip  sh run_tests.sh
set -e
: "${BATTLESHIP:?set BATTLESHIP to the vendor/BattleShip directory}"
HERE=$(cd "$(dirname "$0")" && pwd); W=$(mktemp -d); cd "$W"
FN="aSetBufferImpl aLoadBufferImpl aSaveBufferImpl aSetVolumeImpl aEnvMixerImpl aLoadADPCMImpl aSetLoopImpl aADPCMdecImpl aResampleImpl aClearBufferImpl aDMEMMoveImpl aInterleaveImpl aMixImpl aPoleFilterImpl"
rename() { D=""; for f in $FN; do D="$D -D$f=$1_$f"; done; echo "$D"; }
INC="-I$HERE -I$BATTLESHIP/debug_tools -I$BATTLESHIP/port -I$BATTLESHIP/port/audio"
CF="-std=gnu11 -O2 -fwrapv -fno-strict-aliasing -DN_MICRO=1 -DPORT=1"
# reference = vendor mixer.c (CRLF stripped copy), optimized = the shipped 3ds/src/mixer_3ds.c
gcc $CF $(rename ref) $INC -c "$HERE/mixer_ref.c" -o ref.o
gcc $CF $(rename opt) $INC -c "$HERE/../3ds/src/mixer_3ds.c" -o opt.o
gcc -O2 -c "$HERE/stubs.c" -o stubs.o
gcc -O2 -I"$BATTLESHIP/debug_tools" -c "$BATTLESHIP/debug_tools/acmd_trace/acmd_trace.c" -o a1.o
gcc -O2 -I"$BATTLESHIP/debug_tools" -c "$BATTLESHIP/debug_tools/acmd_trace/acmd_decoder.c" -o a2.o
gcc -O2 "$HERE/test_mixer.c" ref.o opt.o stubs.o a1.o a2.o -o t_mixer
echo "== mixer: reference vs optimized (bit-exact?)"; ./t_mixer
gcc -O2 -I"$HERE" "$HERE/test_ticks.c" -o t_ticks;               echo "== tick conversion: standalone";        ./t_ticks
gcc -O2 -std=gnu11 "$HERE/test_ticks_verbatim.c" -o t_tv;        echo "== tick conversion: verbatim from platform_3ds.c"; ./t_tv
gcc -O2 -msse2 -mfpmath=sse -ffp-contract=off -std=gnu11 "$HERE/test_render_math.c" -o t_rm -lm; echo "== renderer math: colour LUT + divide-free cull"; ./t_rm
