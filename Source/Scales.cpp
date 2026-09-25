// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Scales.h"
#include <algorithm>

namespace wavelathe
{
namespace music
{
namespace
{
const char* const keyNames[numKeys] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

struct ScaleDefinition
{
    const char* name;
    const char* description;
    std::vector<int> intervals;
};

// Ordered so the ones a beginner should reach for come first.
const ScaleDefinition scaleDefinitions[] = {
    {"Chromatic (off)", "Every note - the lock is off.",
     {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}},
    {"Major", "Bright and resolved. The default happy sound.",
     {0, 2, 4, 5, 7, 9, 11}},
    {"Natural Minor", "The standard sad sound, and most of dance music.",
     {0, 2, 3, 5, 7, 8, 10}},
    {"Harmonic Minor", "Minor with a raised seventh - tense, eastern.",
     {0, 2, 3, 5, 7, 8, 11}},
    {"Melodic Minor", "Minor going up, smoother than harmonic.",
     {0, 2, 3, 5, 7, 9, 11}},
    {"Dorian", "Minor with a brighter sixth. Funk and house.",
     {0, 2, 3, 5, 7, 9, 10}},
    {"Phrygian", "Minor with a flat second - spanish, dark.",
     {0, 1, 3, 5, 7, 8, 10}},
    {"Lydian", "Major with a raised fourth - floating, filmic.",
     {0, 2, 4, 6, 7, 9, 11}},
    {"Mixolydian", "Major with a flat seventh - bluesy, rock.",
     {0, 2, 4, 5, 7, 9, 10}},
    {"Locrian", "Unstable and unresolved. Use sparingly.",
     {0, 1, 3, 5, 6, 8, 10}},
    {"Major Pentatonic", "Five notes, all safe together. Hard to go wrong.",
     {0, 2, 4, 7, 9}},
    {"Minor Pentatonic", "Five notes - the riff scale. Start here.",
     {0, 3, 5, 7, 10}},
    {"Blues", "Minor pentatonic plus the flat fifth.",
     {0, 3, 5, 6, 7, 10}},
    {"Whole Tone", "Every step the same size - dreamlike, no home note.",
     {0, 2, 4, 6, 8, 10}},
    {"Diminished", "Alternating steps - tense, used over dominant chords.",
     {0, 2, 3, 5, 6, 8, 9, 11}},
    {"Hungarian Minor", "Two augmented steps - exotic and dramatic.",
     {0, 2, 3, 6, 7, 8, 11}}
};

static_assert(sizeof(scaleDefinitions) / sizeof(scaleDefinitions[0]) == (size_t) Scale::numScales,
              "scale table is out of step with the Scale enum");

const char* const chordModeNames[] = {
    "Off", "Power (5th)", "Triad", "7th", "Sus4", "Octave", "Triad + Octave"
};

static_assert(sizeof(chordModeNames) / sizeof(chordModeNames[0]) == (size_t) ChordMode::numModes,
              "chord mode table is out of step with the ChordMode enum");

// Floor division, so notes below the key's root still land on the right octave
// (C-style division truncates toward zero and would fold them the wrong way).
int floorDiv(int value, int divisor)
{
    int quotient = value / divisor;
    if ((value % divisor != 0) && ((value < 0) != (divisor < 0)))
        --quotient;
    return quotient;
}

const ScaleDefinition& definition(Scale scale)
{
    int index = juce::jlimit(0, (int) Scale::numScales - 1, (int) scale);
    return scaleDefinitions[index];
}
} // namespace

const char* keyName(int key)
{
    return keyNames[((key % numKeys) + numKeys) % numKeys];
}

const char* scaleName(int scale)
{
    return scaleDefinitions[juce::jlimit(0, (int) Scale::numScales - 1, scale)].name;
}

const char* scaleDescription(int scale)
{
    return scaleDefinitions[juce::jlimit(0, (int) Scale::numScales - 1, scale)].description;
}

const char* chordModeName(int mode)
{
    return chordModeNames[juce::jlimit(0, (int) ChordMode::numModes - 1, mode)];
}

const std::vector<int>& scaleIntervals(Scale scale)
{
    return definition(scale).intervals;
}

int snapToScale(int midiNote, int key, Scale scale)
{
    if (scale == Scale::chromatic)
        return juce::jlimit(0, 127, midiNote);

    const auto& intervals = definition(scale).intervals;

    int fromKey = midiNote - key;
    int octave = floorDiv(fromKey, 12);
    int pitchClass = fromKey - octave * 12;

    // Search outward from where the note landed, downward first on a tie.
    for (int distance = 0; distance <= 6; ++distance)
    {
        for (int direction : {-1, 1})
        {
            int candidate = pitchClass + direction * distance;
            int candidateOctave = octave + floorDiv(candidate, 12);
            int candidateClass = candidate - floorDiv(candidate, 12) * 12;

            if (std::find(intervals.begin(), intervals.end(), candidateClass) != intervals.end())
                return juce::jlimit(0, 127, key + candidateOctave * 12 + candidateClass);

            if (distance == 0)
                break; // zero is the same candidate in both directions
        }
    }

    return juce::jlimit(0, 127, midiNote);
}

bool isInScale(int midiNote, int key, Scale scale)
{
    const auto& intervals = definition(scale).intervals;
    int fromKey = midiNote - key;
    int pitchClass = fromKey - floorDiv(fromKey, 12) * 12;
    return std::find(intervals.begin(), intervals.end(), pitchClass) != intervals.end();
}

int noteForDegree(int degree, int key, Scale scale)
{
    const auto& intervals = definition(scale).intervals;
    int perOctave = (int) intervals.size();

    int octave = floorDiv(degree, perOctave);
    int index = degree - octave * perOctave;

    return juce::jlimit(0, 127, key + octave * 12 + intervals[(size_t) index]);
}

int degreeForNote(int midiNote, int key, Scale scale)
{
    const auto& intervals = definition(scale).intervals;

    int fromKey = midiNote - key;
    int octave = floorDiv(fromKey, 12);
    int pitchClass = fromKey - octave * 12;

    auto found = std::find(intervals.begin(), intervals.end(), pitchClass);
    if (found == intervals.end())
        return -1;

    return octave * (int) intervals.size() + (int) std::distance(intervals.begin(), found);
}

std::vector<int> buildChord(int rootNote, int key, Scale scale, ChordMode mode)
{
    int root = snapToScale(rootNote, key, scale);
    std::vector<int> notes{root};

    if (mode == ChordMode::off)
        return notes;

    if (mode == ChordMode::power)
    {
        notes.push_back(juce::jlimit(0, 127, root + 7));
        return notes;
    }

    if (mode == ChordMode::octave)
    {
        notes.push_back(juce::jlimit(0, 127, root + 12));
        return notes;
    }

    int rootDegree = degreeForNote(root, key, scale);
    if (rootDegree < 0)
        return notes; // should not happen after snapping, but never guess a note

    // Stacking scale steps rather than semitones is what makes the chord come
    // out in the right key without anyone choosing its quality.
    auto addDegree = [&](int stepsAbove)
    {
        int note = noteForDegree(rootDegree + stepsAbove, key, scale);
        if (note != root)
            notes.push_back(note);
    };

    switch (mode)
    {
        case ChordMode::triad:
            addDegree(2);
            addDegree(4);
            break;

        case ChordMode::seventh:
            addDegree(2);
            addDegree(4);
            addDegree(6);
            break;

        case ChordMode::sus4:
            addDegree(3);
            addDegree(4);
            break;

        case ChordMode::triadPlusOctave:
            addDegree(2);
            addDegree(4);
            notes.push_back(juce::jlimit(0, 127, root + 12));
            break;

        default:
            break;
    }

    std::sort(notes.begin(), notes.end());
    notes.erase(std::unique(notes.begin(), notes.end()), notes.end());
    return notes;
}

juce::String chordLabel(int rootNote, int key, Scale scale, ChordMode mode)
{
    static const char* const upper[] = {"I", "II", "III", "IV", "V", "VI", "VII", "VIII"};
    static const char* const lower[] = {"i", "ii", "iii", "iv", "v", "vi", "vii", "viii"};

    int root = snapToScale(rootNote, key, scale);
    int degree = degreeForNote(root, key, scale);

    juce::String noteName = juce::String(keyName(root % numKeys)) + juce::String(root / 12 - 1);

    if (degree < 0 || scale == Scale::chromatic)
        return noteName;

    const auto& intervals = definition(scale).intervals;
    int step = degree % (int) intervals.size();
    if (step > 7)
        return noteName;

    if (mode == ChordMode::off)
        return noteName + "  (" + upper[step] + ")";

    auto notes = buildChord(root, key, scale, mode);

    if (mode == ChordMode::power)
        return noteName + "5  (" + upper[step] + "5)";

    if (mode == ChordMode::octave)
        return noteName + " oct";

    if (mode == ChordMode::sus4)
        return noteName + "sus4  (" + upper[step] + "sus4)";

    // Major or minor is read off the actual third and fifth the key produced,
    // so the numeral always matches what is sounding.
    if (notes.size() < 3)
        return noteName;

    int third = notes[1] - notes[0];
    int fifth = notes[2] - notes[0];

    juce::String numeral = (third >= 4) ? upper[step] : lower[step];
    juce::String quality;

    if (third == 3 && fifth == 6)
        quality = " dim";
    else if (third == 4 && fifth == 8)
        quality = " aug";

    juce::String suffix;
    if (mode == ChordMode::seventh)
        suffix = "7";

    // Diminished and augmented already say what the third is, so they do not
    // also take the "m" - "Bdim", not "Bmdim".
    juce::String chordName = noteName;
    if (quality.isNotEmpty())
        chordName += quality.trim();
    else if (third < 4)
        chordName += "m";

    chordName += suffix;

    return chordName + "  (" + numeral + quality + suffix + ")";
}
} // namespace music
} // namespace wavelathe
