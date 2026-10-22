#!/bin/sh
# Put sounds on the microSD card, named by the number that reaches them.
#   tools/card.sh <number> <input file>        -> <number>.wav      (plays once)
#   tools/card.sh <number>.loop <input file>   -> <number>.loop.wav (then loops)
#   tools/card.sh ls                            list what is on the card
#   tools/card.sh eject                         unmount the card safely
# Card volume name defaults to "NO NAME"; override with CARD=/Volumes/Other.
set -e
CARD="${CARD:-/Volumes/NO NAME}"
DIR="$(cd "$(dirname "$0")/.." && pwd)"
[ -d "$CARD" ] || { echo "card not mounted at $CARD"; exit 1; }
case "$1" in
  ls)    ls -la "$CARD" | grep -iE '\.wav$' || echo "(no .wav files)";;
  eject) diskutil eject "$CARD";;
  "")    sed -n '2,7p' "$0";;
  *)     [ -n "$2" ] || { echo "usage: $0 <number> <input file>"; exit 1; }
         "$DIR/tools/wav.sh" "$2" "$1" "$CARD"
         rm -f "$CARD/._$1.wav" 2>/dev/null || true;;
esac
