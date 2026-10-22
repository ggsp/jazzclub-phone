#!/bin/sh
# Convert any audio file into the WAV format the phone plays.
#   tools/wav.sh <input file> <name> [output dir]
# <name> is the number to dial (e.g. 123), or 123.loop for the looping part.
# Output dir defaults to data/ (internal flash; flash with pio run -t uploadfs).
set -e
[ $# -ge 2 ] || { echo "usage: $0 <input> <name> [output dir]"; exit 1; }
outdir="${3:-$(dirname "$0")/../data}"
out="$outdir/$2.wav"
tmp="$(mktemp).wav"
ffmpeg -y -loglevel error -i "$1" -ac 1 -ar 16000 -c:a pcm_s16le -map_metadata -1 "$tmp"
# Master for a telephone earpiece: drop bass it cannot reproduce (and that eats
# headroom), even out the dynamics, bring the level up to just under full scale.
sox "$tmp" "$out" highpass 300 highpass 300 lowpass 3600 \
    compand 0.03,0.25 -60,-60,-40,-20,-25,-12,-10,-6,0,-3 -2 \
    gain -l 9 norm -0.5
rm -f "$tmp"
ls -la "$out"
