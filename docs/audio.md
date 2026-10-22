Sound files live on the microSD card (preferred) or in the ESP's flash (`data/`,
flashed with `pio run -t uploadfs`) when no card is present.

- Name each file `<number>.wav`, where `<number>` is what you dial to reach it: `123.wav`,
  `9.wav`, `000.wav`. Every such file on the media is automatically a reachable number.
- `tools/card.sh <number> <input>` converts and copies straight onto the card;
  `tools/card.sh ls` lists it; `tools/card.sh eject` unmounts it.
- Format: WAV, PCM 16-bit, mono, 8-48 kHz. 16 kHz is plenty for a telephone earpiece
  and uses 32 KB per second (about 3 minutes total in the 5.9 MB partition).
- Convert anything with: `tools/wav.sh input.mp3 <id>`

`123.wav` (chime) and `000.wav` (triple beep) in `data/` are test sounds.

## What a call sounds like
Once the dialled number matches a file, the firmware synthesises the call setup before the
sample starts: touch tones (DTMF) for each dialled digit, a pause, ringback (425 Hz, 1 s on,
3 s off, two rings), a pickup click, then `<number>.wav`. Timings, ring count and the on/off
switch (`CONNECT_SEQUENCE`) are in `include/config.h`. `call <number>` in the serial monitor
plays the whole thing without touching the dial.

Samples should therefore be just the "voice", with no rings baked in.

## Intro + loop
`<number>.wav` plays once. If `<number>.loop.wav` also exists it then plays on repeat until hang-up
(otherwise the busy tone follows). Either file may be absent.
