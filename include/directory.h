#pragma once
// Which dialled numbers "connect", and to what.
//
// With an SD card present, every /<digits>.wav (or /<digits>.loop.wav) on the
// card is also a reachable number, no entry needed here. Entries listed here
// still work and take precedence; their files are /<id>.wav.
//
// Each entry maps a number to a destination id (later: which audio sample to
// play). Numbers may differ in length. Matching is decided after every digit:
// an exact entry connects immediately; once no entry can match any more, the
// call fails as unobtainable.
//
// DIAL_MAX_DIGITS caps how many digits are collected. With DIAL_ACCEPT_ANY = 1,
// every DIAL_MAX_DIGITS-long number connects, with id = its numeric value, so
// N digits address 10^N destinations without listing them here.

#include "Dialplan.h"

#define DIAL_MAX_DIGITS 8
#define DIAL_ACCEPT_ANY 0

// Normally empty: numbers come from the file names (/123.wav reaches 123).
// Add entries only to alias a number to a differently named file, e.g. {"1", 42}
// makes dialling 1 play /42.wav.
static const Dialplan::Entry kDirectory[] = {};
static const size_t kDirectorySize = 0;
