// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Scales.h"
#include "HarmonyProcessor.h"
#include "FactoryPresets.h"
#include <iostream>
#include <set>

using namespace wavelathe;
using namespace wavelathe::music;

namespace
{
int failures = 0;

void check(bool condition, const juce::String& what)
{
    if (!condition)
    {
        std::cout << "  FAIL: " << what << std::endl;
        ++failures;
    }
}

juce::String toText(const std::vector<int>& values)
{
    juce::String text;
    for (size_t i = 0; i < values.size(); ++i)
        text << (i > 0 ? " " : "") << values[i];
    return text;
}

constexpr int C3 = 48;
constexpr int keyC = 0;

// Runs notes through the harmony stage and reports what reached the voices.
struct Played
{
    std::vector<int> noteOns;
    std::vector<int> noteOffs;
};

Played runHarmony(const std::vector<int>& pressed, int key, Scale scale, ChordMode chordMode,
                  bool releaseWithDifferentSettings = false, int releaseKey = 0,
                  Scale releaseScale = Scale::chromatic)
{
    HarmonyProcessor harmony;
    harmony.reset();

    SynthParameters params;
    params.musicalKey = (float) key;
    params.musicalScale = (float) scale;
    params.chordMode = (float) chordMode;

    Played result;

    juce::MidiBuffer downs;
    for (int note : pressed)
        downs.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);

    harmony.process(downs, params);
    for (const auto metadata : downs)
        if (metadata.getMessage().isNoteOn())
            result.noteOns.push_back(metadata.getMessage().getNoteNumber());

    // Optionally move the goalposts before letting go, which is where a naive
    // implementation strands notes.
    if (releaseWithDifferentSettings)
    {
        params.musicalKey = (float) releaseKey;
        params.musicalScale = (float) releaseScale;
        params.chordMode = (float) ChordMode::off;
    }

    juce::MidiBuffer ups;
    for (int note : pressed)
        ups.addEvent(juce::MidiMessage::noteOff(1, note), 0);

    harmony.process(ups, params);
    for (const auto metadata : ups)
        if (metadata.getMessage().isNoteOff())
            result.noteOffs.push_back(metadata.getMessage().getNoteNumber());

    return result;
}
} // namespace

int main()
{
    std::cout << "Harmony test - scale lock and chord helper" << std::endl << std::endl;

    // ---- Scale lock --------------------------------------------------------
    std::cout << "Scale lock:" << std::endl;
    {
        // C natural minor is C D Eb F G Ab Bb. Every chromatic note played must
        // land on one of those, and the ones already in key must not move.
        std::set<int> inKey;
        for (int interval : scaleIntervals(Scale::naturalMinor))
            inKey.insert(interval);

        bool allLanded = true;
        bool inKeyUnmoved = true;

        for (int note = C3; note < C3 + 12; ++note)
        {
            int snapped = snapToScale(note, keyC, Scale::naturalMinor);
            if (inKey.count((snapped - keyC) % 12) == 0)
                allLanded = false;

            if (inKey.count((note - keyC) % 12) > 0 && snapped != note)
                inKeyUnmoved = false;
        }

        check(allLanded, "every chromatic note lands in the scale");
        check(inKeyUnmoved, "notes already in the scale are left alone");

        // The whole point: after locking, a wrong note is not reachable.
        std::vector<int> chromaticRun;
        for (int note = C3; note < C3 + 12; ++note)
            chromaticRun.push_back(snapToScale(note, keyC, Scale::naturalMinor));
        std::cout << "  C3 chromatic run through C minor: " << toText(chromaticRun) << std::endl;

        check(snapToScale(C3, keyC, Scale::chromatic) == C3, "chromatic scale leaves notes untouched");
        check(snapToScale(C3 + 1, keyC, Scale::chromatic) == C3 + 1, "chromatic scale is a true bypass");

        // Below the key's root is where truncating division would fold the
        // octave the wrong way.
        bool belowRootOk = true;
        for (int note = 0; note < C3; ++note)
            if (!isInScale(snapToScale(note, 7, Scale::major), 7, Scale::major))
                belowRootOk = false;
        check(belowRootOk, "notes below the key's root snap correctly");
    }

    // ---- Degrees -----------------------------------------------------------
    std::cout << std::endl << "Scale degrees:" << std::endl;
    {
        bool roundTrips = true;
        for (int scale = 1; scale < (int) Scale::numScales; ++scale)
            for (int degree = 0; degree < 20; ++degree)
            {
                int note = noteForDegree(degree, keyC, (Scale) scale);
                if (degreeForNote(note, keyC, (Scale) scale) != degree)
                    roundTrips = false;
            }

        check(roundTrips, "degree -> note -> degree round-trips in every scale");

        // Degree 7 in a seven-note scale is the root, one octave up.
        check(noteForDegree(7, keyC, Scale::major) == noteForDegree(0, keyC, Scale::major) + 12,
              "a full scale of degrees is exactly an octave");
        check(degreeForNote(C3 + 1, keyC, Scale::major) == -1, "a note outside the key has no degree");
    }

    // ---- Chords ------------------------------------------------------------
    std::cout << std::endl << "Chords built in C major:" << std::endl;
    {
        // The quality has to come from the key, not from a fixed shape: I, IV
        // and V major; ii, iii and vi minor; vii diminished.
        struct Expected { int degree; const char* numeral; int third; int fifth; };
        const Expected expected[] = {
            {0, "I", 4, 7}, {1, "ii", 3, 7}, {2, "iii", 3, 7}, {3, "IV", 4, 7},
            {4, "V", 4, 7}, {5, "vi", 3, 7}, {6, "vii", 3, 6}
        };

        for (const auto& e : expected)
        {
            int root = noteForDegree(e.degree, keyC, Scale::major) + 36; // up into a playable octave
            auto chord = buildChord(root, keyC, Scale::major, ChordMode::triad);
            auto label = chordLabel(root, keyC, Scale::major, ChordMode::triad);

            std::cout << "  degree " << (e.degree + 1) << ": " << toText(chord)
                      << "   " << label << std::endl;

            check(chord.size() == 3, juce::String(e.numeral) + " is a three-note triad");
            if (chord.size() == 3)
            {
                check(chord[1] - chord[0] == e.third,
                      juce::String(e.numeral) + " has the third the key gives it");
                check(chord[2] - chord[0] == e.fifth,
                      juce::String(e.numeral) + " has the fifth the key gives it");
            }

            check(label.contains(e.numeral), juce::String("label names it ") + e.numeral);
        }
    }

    std::cout << std::endl << "Chord shapes on C:" << std::endl;
    {
        auto show = [](ChordMode mode)
        {
            auto chord = buildChord(C3, keyC, Scale::major, mode);
            std::cout << "  " << juce::String(chordModeName((int) mode)).paddedRight(' ', 16)
                      << toText(chord) << std::endl;
            return chord;
        };

        check(show(ChordMode::off).size() == 1, "chord off plays a single note");
        check(show(ChordMode::power) == std::vector<int>({C3, C3 + 7}), "power chord is root and fifth");
        check(show(ChordMode::triad).size() == 3, "triad has three notes");
        check(show(ChordMode::seventh).size() == 4, "seventh has four notes");
        check(show(ChordMode::sus4) == std::vector<int>({C3, C3 + 5, C3 + 7}), "sus4 replaces the third");
        check(show(ChordMode::octave) == std::vector<int>({C3, C3 + 12}), "octave doubles the root");
        check(show(ChordMode::triadPlusOctave).size() == 4, "triad plus octave has four notes");
    }

    // Every chord in every key and scale must stay inside that key.
    {
        bool allInKey = true;
        for (int scale = 1; scale < (int) Scale::numScales; ++scale)
            for (int key = 0; key < numKeys; ++key)
                for (int note = 36; note < 72; ++note)
                    for (int mode : {(int) ChordMode::triad, (int) ChordMode::seventh, (int) ChordMode::sus4})
                        for (int chordNote : buildChord(note, key, (Scale) scale, (ChordMode) mode))
                            if (!isInScale(chordNote, key, (Scale) scale))
                                allInKey = false;

        check(allInKey, "every chord in every key stays inside that key");
    }

    // ---- Note tracking -----------------------------------------------------
    // The part that breaks quietly if it is wrong: what was started must be
    // what is stopped, or notes hang forever.
    std::cout << std::endl << "Note tracking:" << std::endl;
    {
        auto played = runHarmony({C3 + 1}, keyC, Scale::naturalMinor, ChordMode::triad);
        std::cout << "  pressed one black key, voices got: " << toText(played.noteOns) << std::endl;

        check(played.noteOns.size() == 3, "one key produced a three-note chord");
        check(played.noteOns == played.noteOffs, "releasing the key released exactly those notes");

        // Changing the key while a note is held is the case that strands notes.
        auto moved = runHarmony({C3 + 1}, keyC, Scale::naturalMinor, ChordMode::triad,
                                true, 7, Scale::lydian);
        std::cout << "  key changed while held, on: " << toText(moved.noteOns)
                  << "  off: " << toText(moved.noteOffs) << std::endl;
        check(moved.noteOns == moved.noteOffs, "changing key mid-note still releases what it started");
    }

    {
        // Retriggering the same key must not orphan the first chord.
        HarmonyProcessor harmony;
        SynthParameters params;
        params.musicalScale = (float) Scale::naturalMinor;
        params.chordMode = (float) ChordMode::triad;

        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, C3, 0.9f), 0);
        midi.addEvent(juce::MidiMessage::noteOn(1, C3, 0.9f), 10);
        midi.addEvent(juce::MidiMessage::noteOff(1, C3), 20);
        harmony.process(midi, params);

        int ons = 0, offs = 0;
        for (const auto metadata : midi)
        {
            if (metadata.getMessage().isNoteOn()) ++ons;
            if (metadata.getMessage().isNoteOff()) ++offs;
        }

        std::cout << "  retrigger: " << ons << " note-ons, " << offs << " note-offs" << std::endl;
        check(ons == offs, "a retriggered key leaves nothing sounding");
    }

    {
        // With everything off the stage must be a true bypass.
        auto played = runHarmony({C3, C3 + 1, C3 + 6}, keyC, Scale::chromatic, ChordMode::off);
        check(played.noteOns == std::vector<int>({C3, C3 + 1, C3 + 6}), "everything off passes notes through");
    }

    // ---- Factory bank ------------------------------------------------------
    std::cout << std::endl << "Factory presets:" << std::endl;
    {
        const auto& bank = FactoryPresets::all();
        std::cout << "  " << bank.size() << " presets in the bank" << std::endl;

        check(bank.size() >= 40, "the bank has a usable number of presets");

        std::set<juce::String> names;
        bool allNamed = true, allSane = true, allCategorised = true;
        int perCategory[(int) FactoryPresets::Category::numCategories] = {};

        for (const auto& entry : bank)
        {
            if (entry.name.isEmpty() || entry.values.name != entry.name)
                allNamed = false;

            names.insert(entry.name);

            int category = (int) entry.category;
            if (category < 0 || category >= (int) FactoryPresets::Category::numCategories)
                allCategorised = false;
            else
                ++perCategory[category];

            // A preset that is silent, deafening or endless is a bug, not taste.
            const auto& v = entry.values;
            bool audible = (v.osc1Level > 0.0f || v.osc2Level > 0.0f || v.subLevel > 0.0f || v.noiseLevel > 0.0f);
            if (!audible
                || v.masterGain <= 0.0f || v.masterGain > 1.0f
                || v.release > 2.0f || v.attack > 3.0f
                || v.filterCutoffHz < 20.0f || v.filterCutoffHz > 20000.0f
                || v.filterResonance < 0.1f || v.filterResonance > 1.0f)
                allSane = false;
        }

        check(allNamed, "every preset is named and carries its own name");
        check(names.size() == bank.size(), "preset names are unique");
        check(allCategorised, "every preset has a valid category");
        check(allSane, "every preset is audible, in range, and does not ring on");

        for (int c = 0; c < (int) FactoryPresets::Category::numCategories; ++c)
        {
            std::cout << "  " << juce::String(FactoryPresets::categoryName(c)).paddedRight(' ', 10)
                      << perCategory[c] << std::endl;
            check(perCategory[c] > 0, juce::String(FactoryPresets::categoryName(c)) + " has presets");
        }

        check(FactoryPresets::search("bass", -1).size() > 0, "search finds presets by category name");
        check(FactoryPresets::search("", -1).size() == bank.size(), "an empty search matches everything");
        check(FactoryPresets::search("zzzznothing", -1).empty(), "a search with no matches returns nothing");
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL HARMONY TESTS PASSED" << std::endl;
    else
        std::cout << failures << " FAILURE(S)" << std::endl;

    return failures == 0 ? 0 : 1;
}
