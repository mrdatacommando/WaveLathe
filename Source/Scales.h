// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace wavelathe
{
// The music-theory layer: which notes belong to a key, and which notes stack
// into a chord that is correct in it. Everything here is a pure function of
// (note, key, scale), so the same answer is available to the audio thread, the
// editor and the tests without any shared state.
namespace music
{
enum class Scale
{
    chromatic = 0, // every note - the same as having the lock switched off
    major,
    naturalMinor,
    harmonicMinor,
    melodicMinor,
    dorian,
    phrygian,
    lydian,
    mixolydian,
    locrian,
    majorPentatonic,
    minorPentatonic,
    blues,
    wholeTone,
    diminished,
    hungarianMinor,
    numScales
};

// Chords are built by stacking scale steps rather than fixed semitone counts,
// so the quality follows the key: the chord on the second degree of a major
// key comes out minor by itself, with nothing to get wrong.
enum class ChordMode
{
    off = 0,
    power,          // root and fifth, no third - safe in any key
    triad,
    seventh,
    sus4,
    octave,
    triadPlusOctave,
    numModes
};

constexpr int numKeys = 12;

const char* keyName(int key);
const char* scaleName(int scale);
const char* chordModeName(int mode);
const char* scaleDescription(int scale);

// Semitone offsets from the root, ascending, one octave.
const std::vector<int>& scaleIntervals(Scale scale);

// The nearest note in the key. Ties go downward, so a run of chromatic input
// never jumps ahead of where it was heading.
int snapToScale(int midiNote, int key, Scale scale);
bool isInScale(int midiNote, int key, Scale scale);

// Scale degrees counted from the key's root at octave -1, so degree 0 is the
// lowest root and degrees keep counting upward through octaves. These are what
// a sequencer should store, since a wrong note is not representable.
int noteForDegree(int degree, int key, Scale scale);
int degreeForNote(int midiNote, int key, Scale scale); // -1 if outside the key

// The chord on this root, correct for the key. The root is snapped first, so
// it is safe to pass any note.
std::vector<int> buildChord(int rootNote, int key, Scale scale, ChordMode mode);

// How that chord is named in the key: "IV", "ii", "vii dim", "V7", "I5".
// Plain ASCII, so it renders the same everywhere.
juce::String chordLabel(int rootNote, int key, Scale scale, ChordMode mode);
} // namespace music
} // namespace wavelathe
