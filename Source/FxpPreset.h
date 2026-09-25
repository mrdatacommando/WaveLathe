// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>
#include "SynthParameters.h"
#include <array>
#include <vector>
#include "ProjectState.h"
#include "Drums/DrumSample.h"
#include "Drums/DrumVoice.h"

namespace wavelathe
{
// What an untouched drum mixer is, in the registry's units.
//
// Here rather than repeated in PresetValues and SynthParameters, because a
// project saved before the mixer existed reads these and a fresh synth starts
// at them, and those two having drifted apart would mean loading an old
// project quietly retuned the kit.
inline std::array<float, project::numDrumStoredControls> defaultDrumControls()
{
    std::array<float, project::numDrumStoredControls> values{};

    for (int voice = 0; voice < project::numDrumVoices; ++voice)
    {
        using DC = project::DrumControl;
        values[(size_t) project::drumControlIndex(voice, DC::level)] = 0.8f;
        values[(size_t) project::drumControlIndex(voice, DC::tune)] = 0.0f;
        values[(size_t) project::drumControlIndex(voice, DC::attack)] = 0.0f;
        values[(size_t) project::drumControlIndex(voice, DC::decay)] = 0.5f;
        values[(size_t) project::drumControlIndex(voice, DC::pan)] = 0.0f;
        values[(size_t) project::drumControlIndex(voice, DC::send1)] = 0.0f;
        values[(size_t) project::drumControlIndex(voice, DC::send1Amount)] = 0.0f;
        values[(size_t) project::drumControlIndex(voice, DC::send2)] = 0.0f;
        values[(size_t) project::drumControlIndex(voice, DC::send2Amount)] = 0.0f;
    }

    return values;
}

// The same, for the rack. One list of what untouched means, in ProjectState
// where both the synth's startup values and this can read it.
inline std::array<float, project::numDrumFxControls> defaultDrumFx()
{
    std::array<float, project::numDrumFxControls> values{};

    for (int i = 0; i < project::numDrumFxControls; ++i)
        values[(size_t) i] = project::defaultDrumFxValue((project::DrumFxControl) i);

    return values;
}

// An untouched mix bus: dry, flat, unity. What every file written before v25
// reads as, which is exactly right - those projects had no bus, and a bus that
// changes nothing is the same thing.
inline std::array<float, project::numBusFxControls> defaultBusFx()
{
    std::array<float, project::numBusFxControls> values{};

    for (int i = 0; i < project::numBusFxControls; ++i)
        values[(size_t) i] = project::defaultBusFxValue((project::BusFxControl) i);

    return values;
}

// Which drum each slot starts on: the 808's eleven, then one blank.
//
// Taken from the kit rather than written out here, for the reason the two
// above are: the audio library's defaultEngineFor is what the engine actually
// uses when nothing has said otherwise, and a second copy of that mapping here
// would be a second copy that can disagree.
inline std::array<int, project::numDrumVoices> defaultDrumEngines()
{
    std::array<int, project::numDrumVoices> values{};

    for (int voice = 0; voice < project::numDrumVoices; ++voice)
        values[(size_t) voice] = drums::defaultEngineFor(voice);

    return values;
}

// Plain-value snapshot of SynthParameters, used for save/load.
struct PresetValues
{
    juce::String name;
    float wavePosition = 0.0f;
    float attack = 0.02f;
    float decay = 0.15f;
    float sustain = 0.8f;
    float release = 0.2f;
    float filterCutoffHz = 4000.0f;
    float filterResonance = 0.7f;
    float lfoRateHz = 2.0f;
    float lfoDepth = 0.0f;
    float lfoAmpDepth = 0.0f;
    float unisonVoices = 1.0f;
    float unisonDetuneCents = 0.0f;
    float unisonWidth = 0.0f;
    float driveAmount = 0.0f;
    float masterGain = 0.7f;

    float stereoLfo = 0.0f;
    float fxDistortion = 0.0f;
    float delayTimeMs = 320.0f;
    float delayFeedback = 0.35f;
    float delayMix = 0.0f;
    float reverbSize = 0.5f;
    float reverbMix = 0.0f;

    float lfoToWave = 0.0f;
    float modEnvAttack = 0.01f;
    float modEnvDecay = 0.4f;
    float modEnvSustain = 0.3f;
    float modEnvRelease = 0.4f;
    float modEnvToWave = 0.0f;
    float modEnvToCutoff = 0.0f;
    float lfo2RateHz = 0.5f;
    float lfo2ToWave = 0.0f;
    float lfo2ToCutoff = 0.0f;

    float filterType = 0.0f;
    float osc1Level = 1.0f;
    float osc2Level = 0.0f;
    float osc2WavePosition = 0.0f;
    float osc2Semitones = 0.0f;
    float osc2Fine = 0.0f;
    float subLevel = 0.0f;
    float subWave = 0.0f;
    float subOctave = 1.0f;
    float noiseLevel = 0.0f;
    float noiseColour = 0.0f;

    // Arpeggiator, tempo and the tempo-sync switches. These are as much a part
    // of how a patch sounds as the filter is, so they travel with it.
    float arpEnabled = 0.0f;
    float arpMode = 0.0f;
    float arpPattern = 0.0f;
    float arpRateHz = 8.0f;
    float arpOctaves = 1.0f;
    float arpGate = 0.6f;
    float arpSwing = 0.0f;
    float arpSync = 1.0f;
    float delaySync = 0.0f;
    float lfo1Sync = 0.0f;
    float lfo2Sync = 0.0f;
    float bpm = 120.0f;
    float midiClockSync = 0.0f;

    // The key a patch is meant to be played in, and the chord shape it builds.
    float musicalKey = 0.0f;
    float musicalScale = 0.0f;
    float chordMode = 0.0f;

    // The rates, and how they are tied together. Saved because a patch that
    // sounds right at one rate does not at another, and because an
    // arpeggiator following the sequencer needs to know what it is following.
    float arpDivision = 11.0f;
    float arpRateLink = 2.0f;
    float seqDivision = 11.0f;
    float seqLength = 64.0f;
    float seqRootNote = 48.0f;
    float seqRecordNotes = 1.0f;
    float seqQuantise = 1.0f;

    // How a line moves between notes, and the effects added in v9.
    float glideTimeMs = 0.0f;
    float monoMode = 0.0f;
    float legatoMode = 1.0f;
    float chorusRate = 0.6f;
    float chorusDepth = 0.35f;
    float chorusMix = 0.0f;
    float phaserRate = 0.4f;
    float phaserFeedback = 0.4f;
    float phaserMix = 0.0f;
    float eqLowGain = 0.0f;
    float eqMidGain = 0.0f;
    float eqHighGain = 0.0f;

    // Playing as a modulation source, added in v10.
    float velocityToCutoff = 0.0f;
    float velocityToWave = 0.0f;
    float velocityToAmp = 1.0f;

    // Accent, added in v13.
    float velocityToResonance = 0.0f;
    float accentAmount = 0.95f;
    float wheelToCutoff = 0.0f;
    float wheelToWave = 0.0f;
    float pressureToCutoff = 0.0f;

    // The mastering chain. Defaults here are what a file written before v16
    // gets, which is why masteringEnabled starts at zero: an older patch must
    // sound exactly as it did, and a stage that switched itself on would change
    // all sixty-five factory presets the first time they were opened.
    float masteringEnabled = 0.0f;
    float masterSatDrive = 0.0f;
    float masterSatMix = 1.0f;
    float masterCompThresholdDb = 0.0f;
    float masterCompRatio = 2.0f;
    float masterCompAttackMs = 10.0f;
    float masterCompReleaseMs = 100.0f;
    float masterCompMakeupDb = 0.0f;
    float masterCompMix = 1.0f;
    float masterLimitCeilingDb = -0.3f;
    float masterLimitReleaseMs = 50.0f;
    float masterFxGainDb = 0.0f;

    // Which hardware control moves which dial, as MidiMap writes it. Saved with
    // a project because the project is the session; ignored when a preset is
    // loaded, since a patch has no business rearranging the desk.
    juce::String midiMap;

    // Which MIDI channel played notes when this was saved: 0 for omni, 1-16 for
    // one channel, and -1 for "this file predates the field". The distinction
    // matters - without it an older project would read as omni and offer to
    // widen a keyboard that was deliberately narrowed.
    int keyboardChannel = -1;

    // The pattern and the automation drawn over it. Empty for anything saved
    // before the sequencer travelled with a project.
    SequencerState sequencer;

    // The drum mixer: twelve voices of Level, Tune, Decay and Pan, in the
    // registry's own units and order, indexed by project::drumControlIndex.
    //
    // Travels with the PROJECT rather than the preset, the way the pattern
    // does. Picking "Bright Lead" over "Warm Pad" is a decision about the
    // synth's voice, and having it retune your kick and recentre your hats
    // would be the same surprise the sequencer settings caused before they
    // were gated - a preset carries no drums, so what it would apply here is
    // twelve rows of defaults over a kit somebody built.
    //
    // A plain array rather than a variable-length block because it is always
    // exactly six values per voice: the mixer has a value for every voice
    // whether or not that voice has a hit on it, and an absent voice would
    // have to mean "default" anyway.
    std::array<float, project::numDrumStoredControls> drumControls = defaultDrumControls();

    // The four shared units. Saved with the project for the same reason the
    // mixer is: which reverb the snare is in is part of the beat, not part of
    // the patch, and loading "Bright Lead" should not flatten it.
    std::array<float, project::numDrumFxControls> drumFx = defaultDrumFx();

    // What is loaded into each sample slot, EMBEDDED rather than referenced
    // by path. A project that carries its samples survives being sent to
    // somebody else; one that points at C:\Samples does not.
    //
    // Shared pointers, and that is not a detail. Undo snapshots a whole
    // PresetValues every time a dial is touched, so a deep copy here would
    // make turning a knob cost however many megabytes of audio are loaded.
    std::array<drums::SampleRef, project::numDrumVoices> drumSamples;

    // Which synthesised drum each slot is set to, or drums::noEngine for a
    // blank one. Part of the project rather than of the patch, for the reason
    // the mixer is: which drum sits on which row is the kit somebody built.
    std::array<int, project::numDrumVoices> drumEngines = defaultDrumEngines();

    // The Master page's bus effects, in project::BusFxControl order. With the
    // project, not the patch: the bus is where the kit and the synth meet, and
    // a patch that rewrote it would be reaching into the drums.
    std::array<float, project::numBusFxControls> busFx = defaultBusFx();

    // Which rows had to be left behind to fit this project into the kit.
    //
    // Empty for everything written by this version. A project from when the
    // kit had sixteen rows can carry hits, and samples, on rows that no longer
    // exist - and dropping somebody's work silently is not a thing a loader
    // should do. The editor puts this on the display.
    //
    // INDICES, not names, and that is a layering decision rather than a
    // preference. This reader deliberately links only the header-only part of
    // the drum library - it turns bytes into values and knows nothing about
    // what a drum sounds like. Naming them here pulled drums::engineName in
    // and broke the link on seven console tools that read presets and have no
    // business owning a synthesiser. The editor names them; it already has the
    // kit.
    juce::Array<int> droppedDrumRows;

    // Optional sampled wavetable travelling with the preset, so a preset made
    // with waves sampled from audio still sounds right when reloaded later.
    // Empty means "use the built-in waves".
    std::vector<float> wavetableData;
};

// Reads/writes WaveLathe presets using the standard VST2 .fxp container
// format (Steinberg's documented "fxProgram" chunk, opaque-data variant).
// This is a real, generic FXP reader/writer for OUR OWN preset payload —
// it identifies files by WaveLathe's own fxID and will not decode presets
// exported by other synths, since their internal chunk payload formats are
// proprietary and undocumented.
namespace FxpPreset
{
PresetValues fromSynthParameters(const SynthParameters& params, const juce::String& name);

// How much of a file a load puts back. Two answers and only two, because
// there are two kinds of file and the menu names both:
//
// A SYNTH PRESET is the Synth page's sound. The oscillators, filter,
// envelopes and modulation, the Synth page's own effects and output gain, the
// expression routings, the arpeggiator, Play in Key, and a sampled wavetable
// if the patch was built on one. Nothing else - loading one never touches the
// pattern, the kit, the Master page or the tempo.
//
// A PROJECT is everything, on every page: the sound above, plus the tempo,
// the pattern and its automation, the whole drum kit with its samples, the
// Master page's bus effects and mastering chain, and the desk's MIDI map.
//
// Play in Key is on the synth side, and that is a judgement rather than an
// obvious fact: the sequencer's pattern is written in scale degrees, so a key
// change moves the pattern too. It stays with the sound because the factory
// presets are voiced with it - Chord Pulse IS its chord mode - and a preset
// that arrived without the scale it was written in would not be that preset.
//
// Until 0.48.0 this was two booleans, and a preset load passed one of them:
// the mastering chain and the tempo came back with every patch, so choosing a
// lead sound reset the Master page and moved the transport.
enum class ApplyScope
{
    synthPreset,
    project
};

// The five sequencer settings - length, rate, root note, record and quantise -
// come back with a project only when it actually carries a pattern, so
// nothing can resize someone's bar on the strength of a default it never
// meant to set.
void applyToSynthParameters(const PresetValues& values, SynthParameters& params,
                            ApplyScope scope = ApplyScope::project);

// What Save Synth Preset writes: the sound in `everything`, with every
// project-level field put back to what a fresh PresetValues holds. So a synth
// preset carries no pattern, no kit, no samples and no Master page - which is
// also the difference between a file of kilobytes and one of megabytes once
// drum samples are loaded.
//
// The same list of fields applyToSynthParameters leaves alone at synthPreset
// scope. DrumPatternTest holds the two to each other, field by field.
PresetValues synthPresetOnly(const PresetValues& everything);

bool save(const juce::File& file, const PresetValues& values, juce::String& errorMessage);
bool load(const juce::File& file, PresetValues& outValues, juce::String& errorMessage);

// The same payload, in memory rather than in a file. This is what the host -
// or the standalone app on its way out - stores to bring a session back
// exactly as it was left, so there is one definition of "the project" rather
// than a saved-file version and a remembered version that can disagree.
void writeToMemory(const PresetValues& values, juce::MemoryBlock& destination);
bool readFromMemory(const void* data, int sizeInBytes, PresetValues& outValues,
                    juce::String& errorMessage);
} // namespace FxpPreset
} // namespace wavelathe
