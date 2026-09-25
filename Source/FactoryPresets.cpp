// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "FactoryPresets.h"
#include <functional>

namespace wavelathe
{
namespace FactoryPresets
{
namespace
{
const char* const categoryNames[] = {"Bass", "Lead", "Pad", "Pluck", "Keys", "Sequence", "FX"};

static_assert(sizeof(categoryNames) / sizeof(categoryNames[0]) == (size_t) Category::numCategories,
              "category table is out of step with the Category enum");

// The Wave dial travels sine -> triangle -> saw -> square -> bright, so these
// read as the waveform each patch is built on.
constexpr float sine = 0.0f;
constexpr float triangle = 0.25f;
constexpr float saw = 0.5f;
constexpr float square = 0.75f;
constexpr float bright = 1.0f;

// Filter types, in the order the Type dial steps through them.
constexpr float lp12 = 0.0f;
constexpr float lp24 = 1.0f;
constexpr float hp12 = 2.0f;
constexpr float bp12 = 3.0f;

// Arpeggiator rhythms and orders, by index into their tables.
constexpr float rhythmStraight = 0.0f;
constexpr float rhythmTresillo = 4.0f;
constexpr float rhythmTranceGate = 9.0f;
constexpr float rhythmOctaveJump = 16.0f;
constexpr float rhythmBerlin = 20.0f;
constexpr float rhythmAcid = 21.0f;
constexpr float rhythmChordPunch = 22.0f;

constexpr float orderUp = 0.0f;
constexpr float orderUpDown = 2.0f;
constexpr float orderChord = 19.0f;
constexpr float orderRandomOnce = 20.0f;

constexpr float scaleNaturalMinor = 2.0f;
constexpr float chordTriad = 2.0f;

std::vector<Entry> buildBank()
{
    std::vector<Entry> bank;

    auto add = [&bank](const char* name, Category category, const std::function<void(PresetValues&)>& shape)
    {
        Entry entry;
        entry.name = name;
        entry.category = category;
        entry.values = init();
        entry.values.name = name;
        shape(entry.values);
        bank.push_back(std::move(entry));
    };

    // ---- Bass --------------------------------------------------------------
    add("Deep Sub", Category::bass, [](PresetValues& v)
    {
        v.wavePosition = sine;   v.osc1Level = 0.65f;
        v.subLevel = 0.85f;      v.subOctave = 1.0f;
        v.filterType = lp24;     v.filterCutoffHz = 240.0f; v.filterResonance = 0.2f;
        v.attack = 0.004f; v.decay = 0.30f; v.sustain = 0.90f; v.release = 0.12f;
        v.driveAmount = 0.10f;   v.masterGain = 0.77f;
    });

    add("Reese Wobble", Category::bass, [](PresetValues& v)
    {
        v.wavePosition = saw;
        v.unisonVoices = 5.0f; v.unisonDetuneCents = 22.0f; v.unisonWidth = 0.55f;
        v.osc2Level = 0.6f; v.osc2WavePosition = saw; v.osc2Fine = -14.0f;
        v.filterType = lp24; v.filterCutoffHz = 700.0f; v.filterResonance = 0.45f;
        v.lfoRateHz = 2.6f; v.lfoDepth = 0.55f; v.lfo1Sync = 1.0f;
        v.attack = 0.005f; v.decay = 0.4f; v.sustain = 0.85f; v.release = 0.15f;
        v.driveAmount = 0.35f;
        v.masterGain = 0.80f;
    });

    add("Acid Squelch", Category::bass, [](PresetValues& v)
    {
        v.wavePosition = saw;
        v.filterType = lp24; v.filterCutoffHz = 190.0f; v.filterResonance = 0.88f;
        v.modEnvAttack = 0.001f; v.modEnvDecay = 0.22f; v.modEnvSustain = 0.0f;
        v.modEnvRelease = 0.1f;  v.modEnvToCutoff = 0.78f;
        v.attack = 0.002f; v.decay = 0.25f; v.sustain = 0.55f; v.release = 0.07f;
        v.driveAmount = 0.5f;
        v.masterGain = 0.62f;
    });

    add("Hard Growl", Category::bass, [](PresetValues& v)
    {
        v.wavePosition = square;
        v.osc2Level = 0.7f; v.osc2WavePosition = saw; v.osc2Semitones = -12.0f;
        v.filterType = lp24; v.filterCutoffHz = 520.0f; v.filterResonance = 0.55f;
        v.lfo2RateHz = 6.5f; v.lfo2ToWave = 0.35f;
        v.attack = 0.003f; v.decay = 0.35f; v.sustain = 0.8f; v.release = 0.1f;
        v.driveAmount = 0.62f; v.fxDistortion = 0.25f;
        v.masterGain = 0.52f;
    });

    add("Pluck Bass", Category::bass, [](PresetValues& v)
    {
        v.wavePosition = triangle;
        v.osc2Level = 0.45f; v.osc2WavePosition = saw;
        v.subLevel = 0.45f;
        v.filterType = lp24; v.filterCutoffHz = 900.0f; v.filterResonance = 0.35f;
        v.modEnvDecay = 0.18f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.55f;
        v.attack = 0.002f; v.decay = 0.22f; v.sustain = 0.25f; v.release = 0.09f;
        v.masterGain = 0.46f;
    });

    add("Rubber Bass", Category::bass, [](PresetValues& v)
    {
        v.wavePosition = sine; v.subLevel = 0.6f;
        v.filterType = lp24; v.filterCutoffHz = 300.0f; v.filterResonance = 0.72f;
        v.modEnvDecay = 0.3f; v.modEnvSustain = 0.15f; v.modEnvToCutoff = 0.5f;
        v.attack = 0.006f; v.decay = 0.35f; v.sustain = 0.7f; v.release = 0.14f;
        v.driveAmount = 0.2f;
        v.masterGain = 0.49f;
    });

    add("Distorted 808", Category::bass, [](PresetValues& v)
    {
        v.wavePosition = sine; v.osc1Level = 0.9f; v.subLevel = 0.7f; v.subOctave = 2.0f;
        v.filterType = lp12; v.filterCutoffHz = 1200.0f; v.filterResonance = 0.15f;
        v.attack = 0.002f; v.decay = 0.9f; v.sustain = 0.35f; v.release = 0.45f;
        // Left where it was, and the gain does the levelling. Raising it to
        // compensate was tried and abandoned: past about 0.7 the clipper holds
        // its own output level whatever arrives, so the gain below stops being
        // able to move it and the preset cannot be given headroom at all.
        //
        // Which is the honest thing to say about this patch: it used to sit at
        // -1 dB because it was running into the ceiling, and that was part of
        // the sound. It is a little cleaner now. You cannot give something
        // headroom and keep the character it got from not having any.
        v.fxDistortion = 0.55f; v.driveAmount = 0.3f;
        v.masterGain = 0.27f; // measured twice: the clipper after it makes this non-linear
    });

    add("Talking Bass", Category::bass, [](PresetValues& v)
    {
        v.wavePosition = bright;
        v.filterType = bp12; v.filterCutoffHz = 640.0f; v.filterResonance = 0.65f;
        v.lfoRateHz = 1.4f; v.lfoToWave = 0.55f; v.lfo1Sync = 1.0f;
        v.subLevel = 0.5f;
        v.attack = 0.004f; v.decay = 0.4f; v.sustain = 0.8f; v.release = 0.12f;
        v.driveAmount = 0.28f;
        v.masterGain = 0.92f;
    });


    // The bank below this line was written after the synth grew glide, mono,
    // legato, the chorus and phaser, the tone controls and the velocity
    // routings - none of which anything above it touches. A bank that never
    // reaches half the panel makes that half look optional, so these lean on it
    // deliberately rather than being more of the same.

    add("Portamento Sub", Category::bass, [](PresetValues& v)
    {
        // The sliding bass: one voice, and the next note bends to rather than
        // starts again. Mono and legato are what make glide mean anything -
        // without both it is a dial that does nothing.
        v.wavePosition = triangle; v.osc1Level = 0.7f;
        v.subLevel = 0.7f;         v.subOctave = 1.0f;
        v.monoMode = 1.0f; v.legatoMode = 1.0f; v.glideTimeMs = 90.0f;
        v.filterType = lp24; v.filterCutoffHz = 420.0f; v.filterResonance = 0.28f;
        v.attack = 0.006f; v.decay = 0.35f; v.sustain = 0.85f; v.release = 0.18f;
        v.velocityToCutoff = 0.35f;
        v.eqLowGain = 3.0f;
        v.driveAmount = 0.12f; v.masterGain = 0.42f;
    });

    add("FM Bite", Category::bass, [](PresetValues& v)
    {
        // The metallic edge a second oscillator a long way up gives you: not FM
        // as such, but the same clangy harmonic that makes a bass cut through a
        // mix without needing to be loud.
        v.wavePosition = sine;     v.osc1Level = 0.8f;
        v.osc2Level = 0.35f;       v.osc2WavePosition = sine; v.osc2Semitones = 19.0f;
        v.subLevel = 0.5f;
        v.filterType = lp24; v.filterCutoffHz = 900.0f; v.filterResonance = 0.3f;
        v.modEnvDecay = 0.09f; v.modEnvSustain = 0.0f; v.modEnvToWave = 0.45f;
        v.attack = 0.001f; v.decay = 0.22f; v.sustain = 0.55f; v.release = 0.1f;
        v.velocityToWave = 0.5f; v.velocityToCutoff = 0.4f;
        v.driveAmount = 0.22f; v.eqMidGain = 2.5f;
        v.masterGain = 0.52f;
    });

    add("Funk Slap", Category::bass, [](PresetValues& v)
    {
        // All in the first fiftieth of a second: a hard filter snap over a
        // short body, with the top lifted so the attack reads as a slap rather
        // than as a thump.
        v.wavePosition = saw;      v.osc1Level = 0.75f;
        v.osc2Level = 0.4f;        v.osc2WavePosition = square; v.osc2Fine = 6.0f;
        v.filterType = lp24; v.filterCutoffHz = 300.0f; v.filterResonance = 0.62f;
        v.modEnvDecay = 0.05f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.9f;
        v.attack = 0.001f; v.decay = 0.16f; v.sustain = 0.35f; v.release = 0.09f;
        v.velocityToCutoff = 0.6f; v.velocityToResonance = 0.3f;
        v.eqHighGain = 3.5f; v.eqLowGain = 2.0f;
        v.driveAmount = 0.2f;
        v.masterGain = 0.63f;
    });

    add("Rubber Donk", Category::bass, [](PresetValues& v)
    {
        // Short, resonant and pitched: the filter rings at its own note for
        // long enough to be heard as one, which is the whole of the sound.
        v.wavePosition = square;   v.osc1Level = 0.6f;
        v.subLevel = 0.6f;
        v.filterType = lp12; v.filterCutoffHz = 520.0f; v.filterResonance = 0.88f;
        v.modEnvDecay = 0.07f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.7f;
        v.attack = 0.001f; v.decay = 0.14f; v.sustain = 0.0f; v.release = 0.08f;
        v.velocityToResonance = 0.35f;
        v.driveAmount = 0.3f; v.masterGain = 0.58f;
    });
    // ---- Lead --------------------------------------------------------------
    add("Supersaw Lead", Category::lead, [](PresetValues& v)
    {
        v.wavePosition = saw;
        v.unisonVoices = 7.0f; v.unisonDetuneCents = 18.0f; v.unisonWidth = 0.8f;
        v.filterType = lp12; v.filterCutoffHz = 7200.0f; v.filterResonance = 0.25f;
        v.attack = 0.01f; v.decay = 0.3f; v.sustain = 0.85f; v.release = 0.22f;
        v.delayTimeMs = 375.0f; v.delayFeedback = 0.32f; v.delayMix = 0.22f; v.delaySync = 1.0f;
        v.reverbSize = 0.6f; v.reverbMix = 0.18f;
        v.masterGain = 0.62f;
    });

    add("Square Solo", Category::lead, [](PresetValues& v)
    {
        v.wavePosition = square;
        v.unisonVoices = 3.0f; v.unisonDetuneCents = 8.0f; v.unisonWidth = 0.35f;
        v.filterType = lp12; v.filterCutoffHz = 4200.0f; v.filterResonance = 0.3f;
        v.lfoRateHz = 5.2f; v.lfoToWave = 0.12f;
        v.attack = 0.015f; v.decay = 0.25f; v.sustain = 0.8f; v.release = 0.2f;
        v.delayTimeMs = 320.0f; v.delayMix = 0.2f; v.delaySync = 1.0f;
        v.masterGain = 0.75f;
    });

    add("Screamer", Category::lead, [](PresetValues& v)
    {
        v.wavePosition = bright;
        v.unisonVoices = 5.0f; v.unisonDetuneCents = 14.0f; v.unisonWidth = 0.6f;
        v.filterType = lp24; v.filterCutoffHz = 2600.0f; v.filterResonance = 0.6f;
        v.modEnvDecay = 0.5f; v.modEnvSustain = 0.4f; v.modEnvToCutoff = 0.45f;
        v.attack = 0.008f; v.decay = 0.3f; v.sustain = 0.8f; v.release = 0.18f;
        v.driveAmount = 0.45f; v.fxDistortion = 0.3f; v.reverbMix = 0.15f;
        v.masterGain = 0.18f;
    });

    add("Sine Whistle", Category::lead, [](PresetValues& v)
    {
        v.wavePosition = sine;
        v.filterType = lp12; v.filterCutoffHz = 9000.0f;
        v.lfoRateHz = 5.6f; v.lfoToWave = 0.08f; v.lfoAmpDepth = 0.12f;
        v.attack = 0.05f; v.decay = 0.2f; v.sustain = 0.9f; v.release = 0.3f;
        v.delayTimeMs = 400.0f; v.delayFeedback = 0.4f; v.delayMix = 0.28f; v.delaySync = 1.0f;
        v.reverbSize = 0.7f; v.reverbMix = 0.25f;
        v.masterGain = 0.49f;
    });

    add("Trance Lead", Category::lead, [](PresetValues& v)
    {
        v.wavePosition = 0.6f;
        v.unisonVoices = 7.0f; v.unisonDetuneCents = 24.0f; v.unisonWidth = 0.9f;
        v.osc2Level = 0.5f; v.osc2WavePosition = saw; v.osc2Semitones = 12.0f;
        v.filterType = lp24; v.filterCutoffHz = 5200.0f; v.filterResonance = 0.35f;
        v.attack = 0.006f; v.decay = 0.35f; v.sustain = 0.75f; v.release = 0.25f;
        v.delayTimeMs = 375.0f; v.delayFeedback = 0.38f; v.delayMix = 0.25f; v.delaySync = 1.0f;
        v.reverbSize = 0.65f; v.reverbMix = 0.2f;
        v.masterGain = 0.43f;
    });

    add("Chip Lead", Category::lead, [](PresetValues& v)
    {
        v.wavePosition = square;
        v.filterType = lp12; v.filterCutoffHz = 12000.0f; v.filterResonance = 0.1f;
        v.attack = 0.001f; v.decay = 0.12f; v.sustain = 0.7f; v.release = 0.05f;
        v.masterGain = 0.88f;
    });

    add("Vocal Lead", Category::lead, [](PresetValues& v)
    {
        v.wavePosition = bright;
        v.filterType = bp12; v.filterCutoffHz = 1100.0f; v.filterResonance = 0.7f;
        v.lfoRateHz = 4.8f; v.lfoToWave = 0.3f; v.lfoAmpDepth = 0.1f;
        v.unisonVoices = 3.0f; v.unisonDetuneCents = 10.0f; v.unisonWidth = 0.4f;
        v.attack = 0.04f; v.decay = 0.3f; v.sustain = 0.85f; v.release = 0.25f;
        v.reverbSize = 0.6f; v.reverbMix = 0.22f;
        v.masterGain = 0.48f;
    });

    add("Wide Stack", Category::lead, [](PresetValues& v)
    {
        v.wavePosition = saw;
        v.unisonVoices = 7.0f; v.unisonDetuneCents = 34.0f; v.unisonWidth = 1.0f;
        v.subLevel = 0.35f;
        v.filterType = lp12; v.filterCutoffHz = 6000.0f; v.filterResonance = 0.2f;
        v.stereoLfo = 1.0f; v.lfoRateHz = 0.5f; v.lfoDepth = 0.2f;
        v.attack = 0.02f; v.decay = 0.4f; v.sustain = 0.8f; v.release = 0.3f;
        v.reverbSize = 0.7f; v.reverbMix = 0.2f;
        v.masterGain = 0.25f; // trimmed: it was arriving past full scale
    });


    add("Hoover", Category::lead, [](PresetValues& v)
    {
        // The rave stab. Detuned saws with the filter swept down hard by the
        // mod envelope and the chorus opened right up - the wobble in it is the
        // chorus, not the LFO, which is why nothing before this preset sounded
        // quite like it.
        v.wavePosition = saw;
        v.unisonVoices = 7.0f; v.unisonDetuneCents = 28.0f; v.unisonWidth = 0.8f;
        v.osc2Level = 0.5f; v.osc2WavePosition = square; v.osc2Semitones = -12.0f;
        v.filterType = lp24; v.filterCutoffHz = 5200.0f; v.filterResonance = 0.35f;
        v.modEnvDecay = 0.55f; v.modEnvSustain = 0.15f; v.modEnvToCutoff = -0.75f;
        v.attack = 0.004f; v.decay = 0.5f; v.sustain = 0.7f; v.release = 0.25f;
        v.chorusRate = 0.35f; v.chorusDepth = 0.7f; v.chorusMix = 0.55f;
        v.driveAmount = 0.3f;
        v.delayTimeMs = 375.0f; v.delayFeedback = 0.25f; v.delayMix = 0.2f; v.delaySync = 1.0f;
        v.reverbSize = 0.5f; v.reverbMix = 0.15f;
        v.masterGain = 0.60f;
    });

    add("Mono Slide", Category::lead, [](PresetValues& v)
    {
        // Meant to be played legato: hold one note, add the next, and the line
        // bends between them. Overlap the notes and it slides; lift between
        // them and it does not, which is the whole expression of it.
        v.wavePosition = saw;      v.osc1Level = 0.8f;
        v.osc2Level = 0.35f;       v.osc2WavePosition = square; v.osc2Fine = -8.0f;
        v.monoMode = 1.0f; v.legatoMode = 1.0f; v.glideTimeMs = 130.0f;
        v.filterType = lp24; v.filterCutoffHz = 2400.0f; v.filterResonance = 0.4f;
        v.attack = 0.01f; v.decay = 0.3f; v.sustain = 0.75f; v.release = 0.2f;
        v.velocityToCutoff = 0.45f;
        v.wheelToCutoff = 0.5f;
        v.lfoRateHz = 5.2f; v.lfoAmpDepth = 0.0f; v.lfoToWave = 0.12f;
        v.delayTimeMs = 375.0f; v.delayFeedback = 0.3f; v.delayMix = 0.22f; v.delaySync = 1.0f;
        v.reverbSize = 0.55f; v.reverbMix = 0.2f;
        v.masterGain = 0.55f;
    });

    add("FM Bell Lead", Category::lead, [](PresetValues& v)
    {
        // A bell that sustains: the inharmonic partial comes from the second
        // oscillator sitting a twelfth and a bit above, and the mod envelope
        // pulls it back so the clang is in the attack rather than all the way
        // through.
        v.wavePosition = sine;     v.osc1Level = 0.75f;
        v.osc2Level = 0.45f;       v.osc2WavePosition = sine;
        v.osc2Semitones = 19.0f;   v.osc2Fine = 12.0f;
        v.filterType = lp12; v.filterCutoffHz = 6500.0f; v.filterResonance = 0.15f;
        v.modEnvDecay = 0.35f; v.modEnvSustain = 0.1f; v.modEnvToWave = 0.5f;
        v.attack = 0.002f; v.decay = 0.9f; v.sustain = 0.45f; v.release = 0.7f;
        v.velocityToWave = 0.55f;
        v.chorusRate = 0.5f; v.chorusDepth = 0.3f; v.chorusMix = 0.3f;
        v.delayTimeMs = 500.0f; v.delayFeedback = 0.35f; v.delayMix = 0.25f; v.delaySync = 1.0f;
        v.reverbSize = 0.75f; v.reverbMix = 0.32f;
    });

    add("Sync Sweep", Category::lead, [](PresetValues& v)
    {
        // The sound of one oscillator being dragged across another: here it is
        // the wavetable position being swept instead, by the LFO and by how
        // hard you play, which lands in the same place from a different road.
        v.wavePosition = 0.35f;    v.osc1Level = 0.8f;
        v.osc2Level = 0.55f;       v.osc2WavePosition = square; v.osc2Semitones = 7.0f;
        v.filterType = lp12; v.filterCutoffHz = 4200.0f; v.filterResonance = 0.45f;
        v.lfo2RateHz = 0.8f; v.lfo2ToWave = 0.6f;
        v.modEnvDecay = 0.4f; v.modEnvSustain = 0.3f; v.modEnvToWave = 0.4f;
        v.attack = 0.005f; v.decay = 0.35f; v.sustain = 0.7f; v.release = 0.2f;
        v.velocityToWave = 0.6f;
        v.driveAmount = 0.25f; v.eqMidGain = 2.0f;
        v.phaserRate = 0.3f; v.phaserFeedback = 0.5f; v.phaserMix = 0.25f;
        v.masterGain = 0.73f;
    });
    // ---- Pad ---------------------------------------------------------------
    add("Warm Analog Pad", Category::pad, [](PresetValues& v)
    {
        v.wavePosition = saw;
        v.unisonVoices = 5.0f; v.unisonDetuneCents = 16.0f; v.unisonWidth = 0.7f;
        v.filterType = lp24; v.filterCutoffHz = 2200.0f; v.filterResonance = 0.25f;
        v.lfoRateHz = 0.3f; v.lfoDepth = 0.25f; v.stereoLfo = 1.0f;
        v.attack = 0.8f; v.decay = 1.2f; v.sustain = 0.8f; v.release = 1.1f;
        v.reverbSize = 0.8f; v.reverbMix = 0.35f;
        v.masterGain = 0.43f;
    });

    add("Glass Pad", Category::pad, [](PresetValues& v)
    {
        v.wavePosition = bright;
        v.osc2Level = 0.4f; v.osc2WavePosition = sine; v.osc2Semitones = 12.0f;
        v.filterType = lp12; v.filterCutoffHz = 6500.0f; v.filterResonance = 0.2f;
        v.lfo2RateHz = 0.18f; v.lfo2ToWave = 0.4f;
        v.attack = 1.1f; v.decay = 1.5f; v.sustain = 0.75f; v.release = 1.4f;
        v.reverbSize = 0.9f; v.reverbMix = 0.45f;
        v.delayTimeMs = 500.0f; v.delayFeedback = 0.35f; v.delayMix = 0.18f;
        v.masterGain = 0.47f;
    });

    add("Choir Pad", Category::pad, [](PresetValues& v)
    {
        // A richer source than it had. This was a triangle through a bandpass at
        // 1.5 kHz, and a triangle's harmonics have died away long before that -
        // so almost nothing reached the output and it sat twelve decibels below
        // the rest of the bank, quiet enough to read as broken. A voice patch
        // wants a rich source under the formant anyway; that is how the filter
        // is supposed to earn its keep.
        v.wavePosition = 0.45f;
        v.noiseLevel = 0.12f; v.noiseColour = 0.8f;
        v.unisonVoices = 5.0f; v.unisonDetuneCents = 12.0f; v.unisonWidth = 0.8f;
        v.filterType = bp12; v.filterCutoffHz = 900.0f; v.filterResonance = 0.45f;
        v.attack = 0.9f; v.decay = 1.0f; v.sustain = 0.85f; v.release = 1.2f;
        v.reverbSize = 0.85f; v.reverbMix = 0.4f;
        v.masterGain = 1.00f;
    });

    add("Dark Drone", Category::pad, [](PresetValues& v)
    {
        v.wavePosition = sine;
        v.subLevel = 0.7f; v.subOctave = 2.0f;
        v.osc2Level = 0.5f; v.osc2WavePosition = triangle; v.osc2Fine = -8.0f;
        v.filterType = lp24; v.filterCutoffHz = 520.0f; v.filterResonance = 0.3f;
        v.lfoRateHz = 0.12f; v.lfoDepth = 0.3f;
        v.attack = 1.6f; v.decay = 2.0f; v.sustain = 0.9f; v.release = 1.8f;
        v.reverbSize = 0.95f; v.reverbMix = 0.4f;
        v.masterGain = 0.33f; // trimmed: it was arriving past full scale
    });

    add("Shimmer Pad", Category::pad, [](PresetValues& v)
    {
        v.wavePosition = 0.85f;
        v.osc2Level = 0.45f; v.osc2WavePosition = bright; v.osc2Semitones = 19.0f;
        v.filterType = lp12; v.filterCutoffHz = 7000.0f;
        v.lfoRateHz = 0.25f; v.lfoToWave = 0.5f; v.stereoLfo = 1.0f;
        v.attack = 1.0f; v.decay = 1.4f; v.sustain = 0.7f; v.release = 1.5f;
        v.delayTimeMs = 600.0f; v.delayFeedback = 0.45f; v.delayMix = 0.3f;
        v.reverbSize = 0.95f; v.reverbMix = 0.5f;
        v.masterGain = 0.42f;
    });

    add("String Ensemble", Category::pad, [](PresetValues& v)
    {
        v.wavePosition = saw;
        v.unisonVoices = 7.0f; v.unisonDetuneCents = 20.0f; v.unisonWidth = 0.85f;
        v.filterType = lp12; v.filterCutoffHz = 3400.0f; v.filterResonance = 0.2f;
        v.modEnvAttack = 0.5f; v.modEnvSustain = 0.8f; v.modEnvToCutoff = 0.3f;
        v.attack = 0.35f; v.decay = 0.8f; v.sustain = 0.85f; v.release = 0.7f;
        v.reverbSize = 0.75f; v.reverbMix = 0.3f;
        v.masterGain = 0.45f;
    });


    add("Juno Chorus", Category::pad, [](PresetValues& v)
    {
        // A single saw and nothing else clever. Everything wide about it is the
        // chorus, which is how the machines this is named after did it - one
        // oscillator, one filter, and a bucket-brigade chorus doing all the
        // work that unison does here elsewhere.
        v.wavePosition = saw;      v.osc1Level = 0.85f;
        v.unisonVoices = 1.0f;     v.unisonDetuneCents = 0.0f;
        v.subLevel = 0.3f;
        v.filterType = lp24; v.filterCutoffHz = 2600.0f; v.filterResonance = 0.22f;
        v.attack = 0.35f; v.decay = 0.6f; v.sustain = 0.85f; v.release = 0.9f;
        v.chorusRate = 0.6f; v.chorusDepth = 0.55f; v.chorusMix = 0.6f;
        v.eqHighGain = 1.5f;
        v.reverbSize = 0.7f; v.reverbMix = 0.25f;
        v.masterGain = 0.58f;
    });

    add("Brass Swell", Category::pad, [](PresetValues& v)
    {
        // Brass is a filter opening, not a waveform: the envelope takes about a
        // third of a second to get there, which is what separates a section
        // coming in from a synth pad with a slow attack.
        v.wavePosition = saw;
        v.unisonVoices = 3.0f; v.unisonDetuneCents = 9.0f; v.unisonWidth = 0.4f;
        v.osc2Level = 0.5f; v.osc2WavePosition = saw; v.osc2Semitones = -12.0f;
        v.filterType = lp24; v.filterCutoffHz = 700.0f; v.filterResonance = 0.3f;
        v.modEnvAttack = 0.28f; v.modEnvDecay = 0.9f; v.modEnvSustain = 0.6f;
        v.modEnvToCutoff = 0.75f;
        v.attack = 0.12f; v.decay = 0.5f; v.sustain = 0.8f; v.release = 0.4f;
        v.velocityToCutoff = 0.5f;
        v.eqMidGain = 3.0f; v.eqLowGain = 1.5f;
        v.driveAmount = 0.15f;
        v.reverbSize = 0.6f; v.reverbMix = 0.22f;
        v.masterGain = 0.21f; // trimmed: it was arriving past full scale
    });

    add("Motion Pad", Category::pad, [](PresetValues& v)
    {
        // Two slow movements at rates that do not divide into each other, so
        // the pad never quite repeats. A single LFO on a long pad is a pulse
        // you start counting; two at odd rates is weather.
        v.wavePosition = 0.4f;
        v.unisonVoices = 5.0f; v.unisonDetuneCents = 13.0f; v.unisonWidth = 0.85f;
        v.osc2Level = 0.45f; v.osc2WavePosition = triangle; v.osc2Fine = 7.0f;
        v.filterType = lp24; v.filterCutoffHz = 1800.0f; v.filterResonance = 0.25f;
        v.lfoRateHz = 0.23f; v.lfoDepth = 0.35f; v.lfoToWave = 0.3f;
        v.lfo2RateHz = 0.37f; v.lfo2ToWave = 0.4f; v.lfo2ToCutoff = 0.3f;
        v.stereoLfo = 1.0f;
        v.attack = 0.9f; v.decay = 1.2f; v.sustain = 0.8f; v.release = 1.6f;
        v.phaserRate = 0.12f; v.phaserFeedback = 0.45f; v.phaserMix = 0.35f;
        v.reverbSize = 0.9f; v.reverbMix = 0.4f;
        v.delayTimeMs = 750.0f; v.delayFeedback = 0.4f; v.delayMix = 0.2f; v.delaySync = 1.0f;
        v.masterGain = 1.00f;
    });
    // ---- Pluck -------------------------------------------------------------
    add("Nylon Pluck", Category::pluck, [](PresetValues& v)
    {
        v.wavePosition = triangle;
        v.filterType = lp12; v.filterCutoffHz = 2400.0f; v.filterResonance = 0.3f;
        v.modEnvDecay = 0.15f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.5f;
        v.attack = 0.002f; v.decay = 0.35f; v.sustain = 0.0f; v.release = 0.22f;
        v.reverbSize = 0.5f; v.reverbMix = 0.2f;
        v.masterGain = 0.46f;
    });

    add("Bell Pluck", Category::pluck, [](PresetValues& v)
    {
        v.wavePosition = sine;
        v.osc2Level = 0.5f; v.osc2WavePosition = sine; v.osc2Semitones = 19.0f;
        v.filterType = lp12; v.filterCutoffHz = 6000.0f;
        v.attack = 0.001f; v.decay = 0.6f; v.sustain = 0.0f; v.release = 0.5f;
        v.delayTimeMs = 375.0f; v.delayFeedback = 0.3f; v.delayMix = 0.22f; v.delaySync = 1.0f;
        v.reverbSize = 0.7f; v.reverbMix = 0.3f;
        v.masterGain = 0.50f;
    });

    add("Marimba", Category::pluck, [](PresetValues& v)
    {
        v.wavePosition = sine;
        v.osc2Level = 0.3f; v.osc2Semitones = 12.0f;
        v.filterType = lp12; v.filterCutoffHz = 3200.0f;
        v.attack = 0.001f; v.decay = 0.28f; v.sustain = 0.0f; v.release = 0.2f;
        v.reverbSize = 0.45f; v.reverbMix = 0.18f;
        v.masterGain = 0.39f;
    });

    add("Hard Pluck", Category::pluck, [](PresetValues& v)
    {
        v.wavePosition = saw;
        v.filterType = lp24; v.filterCutoffHz = 800.0f; v.filterResonance = 0.62f;
        v.modEnvDecay = 0.12f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.75f;
        v.attack = 0.001f; v.decay = 0.22f; v.sustain = 0.0f; v.release = 0.15f;
        v.driveAmount = 0.3f;
        v.delayTimeMs = 250.0f; v.delayMix = 0.18f; v.delaySync = 1.0f;
        v.masterGain = 0.93f;
    });

    add("Koto", Category::pluck, [](PresetValues& v)
    {
        v.wavePosition = bright;
        v.filterType = bp12; v.filterCutoffHz = 1800.0f; v.filterResonance = 0.55f;
        v.modEnvDecay = 0.2f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.4f;
        v.attack = 0.001f; v.decay = 0.4f; v.sustain = 0.0f; v.release = 0.3f;
        v.reverbSize = 0.55f; v.reverbMix = 0.25f;
        v.masterGain = 0.83f;
    });

    add("Glass Bell", Category::pluck, [](PresetValues& v)
    {
        v.wavePosition = sine;
        v.osc2Level = 0.55f; v.osc2WavePosition = sine; v.osc2Semitones = 12.0f; v.osc2Fine = 6.0f;
        v.filterType = lp12; v.filterCutoffHz = 8000.0f;
        v.attack = 0.001f; v.decay = 1.0f; v.sustain = 0.0f; v.release = 0.9f;
        v.reverbSize = 0.9f; v.reverbMix = 0.42f;
        v.masterGain = 0.34f; // trimmed: it was arriving past full scale
    });


    add("Kalimba", Category::pluck, [](PresetValues& v)
    {
        // A tine being let go of: almost a sine, with a breath of noise in the
        // first few milliseconds for the thumbnail on the metal. Take the noise
        // away and it is a bell; it is the noise that makes it a thumb piano.
        v.wavePosition = sine;     v.osc1Level = 0.8f;
        v.osc2Level = 0.25f;       v.osc2WavePosition = sine; v.osc2Semitones = 12.0f;
        v.noiseLevel = 0.12f;      v.noiseColour = 0.8f;
        v.filterType = lp12; v.filterCutoffHz = 4200.0f; v.filterResonance = 0.2f;
        v.modEnvDecay = 0.03f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.6f;
        v.attack = 0.001f; v.decay = 0.45f; v.sustain = 0.0f; v.release = 0.35f;
        v.velocityToCutoff = 0.5f; v.velocityToWave = 0.3f;
        v.eqLowGain = -2.0f; v.eqHighGain = 2.0f;
        v.reverbSize = 0.55f; v.reverbMix = 0.25f;
        v.masterGain = 0.60f;
    });

    add("Harp Gliss", Category::pluck, [](PresetValues& v)
    {
        // Long release on purpose: a harp's strings keep ringing after the
        // finger has moved on, so a run played fast piles up into a chord. A
        // short release here would make it a guitar.
        v.wavePosition = triangle; v.osc1Level = 0.75f;
        v.osc2Level = 0.3f;        v.osc2WavePosition = saw; v.osc2Fine = 5.0f;
        v.filterType = lp12; v.filterCutoffHz = 3400.0f; v.filterResonance = 0.18f;
        v.modEnvDecay = 0.12f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.55f;
        v.attack = 0.002f; v.decay = 0.8f; v.sustain = 0.0f; v.release = 1.4f;
        v.velocityToCutoff = 0.45f; v.velocityToAmp = 0.9f;
        v.chorusRate = 0.4f; v.chorusDepth = 0.25f; v.chorusMix = 0.25f;
        v.reverbSize = 0.8f; v.reverbMix = 0.35f;
        v.masterGain = 0.55f;
    });
    // ---- Keys --------------------------------------------------------------
    add("Electric Piano", Category::keys, [](PresetValues& v)
    {
        v.wavePosition = sine;
        v.osc2Level = 0.35f; v.osc2WavePosition = triangle; v.osc2Semitones = 12.0f;
        v.filterType = lp12; v.filterCutoffHz = 3600.0f; v.filterResonance = 0.15f;
        v.modEnvDecay = 0.5f; v.modEnvSustain = 0.1f; v.modEnvToCutoff = 0.35f;
        v.attack = 0.002f; v.decay = 0.9f; v.sustain = 0.3f; v.release = 0.35f;
        v.reverbSize = 0.5f; v.reverbMix = 0.18f;
        v.masterGain = 0.39f;
    });

    add("Drawbar Organ", Category::keys, [](PresetValues& v)
    {
        v.wavePosition = square; v.osc1Level = 0.7f;
        v.osc2Level = 0.5f; v.osc2WavePosition = sine; v.osc2Semitones = 19.0f;
        v.subLevel = 0.5f;
        v.filterType = lp12; v.filterCutoffHz = 5000.0f;
        v.attack = 0.005f; v.decay = 0.05f; v.sustain = 1.0f; v.release = 0.06f;
        v.lfoRateHz = 6.2f; v.lfoAmpDepth = 0.1f; v.stereoLfo = 1.0f;
        v.masterGain = 0.47f;
    });

    add("Clav", Category::keys, [](PresetValues& v)
    {
        v.wavePosition = bright;
        v.filterType = lp24; v.filterCutoffHz = 1400.0f; v.filterResonance = 0.6f;
        v.modEnvDecay = 0.1f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.6f;
        v.attack = 0.001f; v.decay = 0.2f; v.sustain = 0.15f; v.release = 0.1f;
        v.driveAmount = 0.35f;
        v.masterGain = 0.73f;
    });

    add("Soft Keys", Category::keys, [](PresetValues& v)
    {
        v.wavePosition = triangle;
        v.unisonVoices = 3.0f; v.unisonDetuneCents = 7.0f; v.unisonWidth = 0.4f;
        v.filterType = lp24; v.filterCutoffHz = 2800.0f; v.filterResonance = 0.2f;
        v.attack = 0.03f; v.decay = 0.7f; v.sustain = 0.5f; v.release = 0.4f;
        v.reverbSize = 0.6f; v.reverbMix = 0.24f;
        v.masterGain = 0.44f;
    });


    add("FM Tines", Category::keys, [](PresetValues& v)
    {
        // The electric piano everyone knows: a bell in the attack over a body
        // that is nearly a sine. How hard you play moves the wavetable, which
        // is what makes it bark when leant on and stay sweet when not - a fixed
        // amount of clang is the difference between an instrument and a sample.
        v.wavePosition = sine;     v.osc1Level = 0.8f;
        v.osc2Level = 0.4f;        v.osc2WavePosition = sine; v.osc2Semitones = 14.0f;
        v.filterType = lp12; v.filterCutoffHz = 5200.0f; v.filterResonance = 0.12f;
        v.modEnvDecay = 0.13f; v.modEnvSustain = 0.0f; v.modEnvToWave = 0.55f;
        v.attack = 0.002f; v.decay = 1.1f; v.sustain = 0.28f; v.release = 0.45f;
        v.velocityToWave = 0.7f; v.velocityToCutoff = 0.4f; v.velocityToAmp = 0.85f;
        v.chorusRate = 0.45f; v.chorusDepth = 0.4f; v.chorusMix = 0.4f;
        v.eqLowGain = 1.5f;
        v.reverbSize = 0.5f; v.reverbMix = 0.18f;
        v.masterGain = 0.47f;
    });

    add("Harpsichord", Category::keys, [](PresetValues& v)
    {
        // A plucked string with no dynamics at all - the mechanism plucks at
        // one strength however hard the key is hit, so velocity is deliberately
        // routed nowhere. That refusal is the instrument.
        v.wavePosition = 0.62f;    v.osc1Level = 0.7f;
        v.osc2Level = 0.45f;       v.osc2WavePosition = square; v.osc2Semitones = 12.0f;
        v.filterType = hp12; v.filterCutoffHz = 220.0f; v.filterResonance = 0.2f;
        v.attack = 0.001f; v.decay = 0.55f; v.sustain = 0.0f; v.release = 0.2f;
        v.velocityToAmp = 0.15f; v.velocityToCutoff = 0.0f;
        v.eqHighGain = 2.5f; v.eqLowGain = -3.0f;
        v.reverbSize = 0.45f; v.reverbMix = 0.2f;
        v.masterGain = 0.24f; // trimmed: it was arriving past full scale
    });

    add("Toy Piano", Category::keys, [](PresetValues& v)
    {
        // Short, bright and slightly wrong: the second oscillator sits a little
        // off a true octave, which is what makes a small struck bar sound like
        // a toy rather than like a celeste.
        v.wavePosition = triangle; v.osc1Level = 0.75f;
        v.osc2Level = 0.4f;        v.osc2WavePosition = sine;
        v.osc2Semitones = 12.0f;   v.osc2Fine = 22.0f;
        v.noiseLevel = 0.08f;      v.noiseColour = 0.9f;
        v.filterType = lp12; v.filterCutoffHz = 3800.0f; v.filterResonance = 0.25f;
        v.modEnvDecay = 0.04f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.7f;
        v.attack = 0.001f; v.decay = 0.4f; v.sustain = 0.0f; v.release = 0.3f;
        v.velocityToCutoff = 0.55f;
        v.eqHighGain = 2.0f;
        v.reverbSize = 0.6f; v.reverbMix = 0.28f;
        v.masterGain = 0.45f;
    });

    add("Reed Organ", Category::keys, [](PresetValues& v)
    {
        // Air through a reed: no decay at all, so it holds exactly as long as
        // the key does, and a phaser doing the slow drift that a real one gets
        // from its own bellows.
        v.wavePosition = square;   v.osc1Level = 0.7f;
        v.osc2Level = 0.5f;        v.osc2WavePosition = saw; v.osc2Fine = -6.0f;
        v.noiseLevel = 0.05f;      v.noiseColour = 0.5f;
        v.filterType = lp12; v.filterCutoffHz = 2400.0f; v.filterResonance = 0.2f;
        v.attack = 0.06f; v.decay = 0.2f; v.sustain = 1.0f; v.release = 0.12f;
        v.phaserRate = 0.25f; v.phaserFeedback = 0.5f; v.phaserMix = 0.4f;
        v.eqMidGain = 2.0f;
        v.reverbSize = 0.5f; v.reverbMix = 0.2f;
        v.masterGain = 0.47f;
    });
    // ---- Sequence ----------------------------------------------------------
    // These arrive with the arpeggiator running and the key locked, so one held
    // chord - or one finger, with the chord helper on - is already a part.
    add("Trance Gate Seq", Category::sequence, [](PresetValues& v)
    {
        v.wavePosition = saw;
        v.unisonVoices = 5.0f; v.unisonDetuneCents = 16.0f; v.unisonWidth = 0.7f;
        v.filterType = lp24; v.filterCutoffHz = 3200.0f; v.filterResonance = 0.4f;
        v.modEnvDecay = 0.12f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.5f;
        v.attack = 0.002f; v.decay = 0.18f; v.sustain = 0.0f; v.release = 0.08f;
        v.arpEnabled = 1.0f; v.arpMode = orderUp; v.arpPattern = rhythmTranceGate;
        v.arpRateHz = 8.0f; v.arpOctaves = 2.0f; v.arpGate = 0.5f; v.arpSync = 1.0f;
        v.musicalScale = scaleNaturalMinor;
        v.delayTimeMs = 375.0f; v.delayFeedback = 0.3f; v.delayMix = 0.2f; v.delaySync = 1.0f;
        v.reverbSize = 0.6f; v.reverbMix = 0.18f;
        v.masterGain = 0.73f;
    });

    add("Acid Line", Category::sequence, [](PresetValues& v)
    {
        v.wavePosition = saw;
        v.filterType = lp24; v.filterCutoffHz = 260.0f; v.filterResonance = 0.85f;
        v.modEnvDecay = 0.18f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.8f;
        v.attack = 0.001f; v.decay = 0.2f; v.sustain = 0.3f; v.release = 0.06f;
        v.driveAmount = 0.5f;
        v.arpEnabled = 1.0f; v.arpMode = orderUp; v.arpPattern = rhythmAcid;
        v.arpRateHz = 8.0f; v.arpOctaves = 1.0f; v.arpGate = 0.6f; v.arpSync = 1.0f;
        v.musicalScale = scaleNaturalMinor;
        v.delayTimeMs = 250.0f; v.delayMix = 0.15f; v.delaySync = 1.0f;
        v.masterGain = 0.77f;
    });

    add("Berlin Drift", Category::sequence, [](PresetValues& v)
    {
        v.wavePosition = square;
        v.filterType = lp24; v.filterCutoffHz = 1600.0f; v.filterResonance = 0.5f;
        v.lfoRateHz = 0.2f; v.lfoDepth = 0.4f; v.stereoLfo = 1.0f;
        v.modEnvDecay = 0.25f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.4f;
        v.attack = 0.002f; v.decay = 0.3f; v.sustain = 0.1f; v.release = 0.12f;
        v.arpEnabled = 1.0f; v.arpMode = orderRandomOnce; v.arpPattern = rhythmBerlin;
        v.arpRateHz = 8.0f; v.arpOctaves = 2.0f; v.arpGate = 0.55f; v.arpSync = 1.0f;
        v.musicalScale = scaleNaturalMinor;
        v.delayTimeMs = 500.0f; v.delayFeedback = 0.42f; v.delayMix = 0.28f; v.delaySync = 1.0f;
        v.reverbSize = 0.75f; v.reverbMix = 0.25f;
    });

    add("Octave Runner", Category::sequence, [](PresetValues& v)
    {
        v.wavePosition = triangle;
        v.filterType = lp12; v.filterCutoffHz = 3000.0f; v.filterResonance = 0.3f;
        v.modEnvDecay = 0.1f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.45f;
        v.attack = 0.001f; v.decay = 0.16f; v.sustain = 0.0f; v.release = 0.1f;
        v.arpEnabled = 1.0f; v.arpMode = orderUp; v.arpPattern = rhythmOctaveJump;
        v.arpRateHz = 8.0f; v.arpOctaves = 1.0f; v.arpGate = 0.45f; v.arpSync = 1.0f;
        v.musicalScale = scaleNaturalMinor;
        v.delayTimeMs = 375.0f; v.delayFeedback = 0.35f; v.delayMix = 0.24f; v.delaySync = 1.0f;
        v.reverbSize = 0.6f; v.reverbMix = 0.2f;
        v.masterGain = 0.61f;
    });

    add("Chord Pulse", Category::sequence, [](PresetValues& v)
    {
        v.wavePosition = saw;
        v.unisonVoices = 3.0f; v.unisonDetuneCents = 12.0f; v.unisonWidth = 0.6f;
        v.filterType = lp24; v.filterCutoffHz = 2400.0f; v.filterResonance = 0.3f;
        v.attack = 0.004f; v.decay = 0.25f; v.sustain = 0.2f; v.release = 0.15f;
        v.arpEnabled = 1.0f; v.arpMode = orderChord; v.arpPattern = rhythmChordPunch;
        v.arpRateHz = 8.0f; v.arpOctaves = 1.0f; v.arpGate = 0.5f; v.arpSync = 1.0f;
        v.musicalScale = scaleNaturalMinor; v.chordMode = chordTriad;
        v.reverbSize = 0.65f; v.reverbMix = 0.25f;
        v.masterGain = 0.51f;
    });

    add("Tresillo Pop", Category::sequence, [](PresetValues& v)
    {
        v.wavePosition = 0.35f;
        v.filterType = lp12; v.filterCutoffHz = 3800.0f; v.filterResonance = 0.25f;
        v.modEnvDecay = 0.14f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.4f;
        v.attack = 0.001f; v.decay = 0.2f; v.sustain = 0.0f; v.release = 0.12f;
        v.arpEnabled = 1.0f; v.arpMode = orderUpDown; v.arpPattern = rhythmTresillo;
        v.arpRateHz = 8.0f; v.arpOctaves = 2.0f; v.arpGate = 0.5f; v.arpSync = 1.0f;
        v.musicalScale = scaleNaturalMinor; v.chordMode = chordTriad;
        v.delayTimeMs = 375.0f; v.delayMix = 0.2f; v.delaySync = 1.0f;
        v.reverbSize = 0.6f; v.reverbMix = 0.22f;
        v.masterGain = 0.58f;
    });


    add("303 Slide", Category::sequence, [](PresetValues& v)
    {
        // The acid line as it was actually made: one voice, so every note takes
        // the line over from the last, and legato so an overlapping one bends
        // into place rather than starting again. Velocity opens the filter AND
        // the resonance, which is what an accent on that machine did - the
        // squelch gets sharper, not just louder.
        //
        // Turn the arpeggiator off and drive it from the sequencer page instead
        // and the SLIDE row does the same job per step.
        v.wavePosition = saw;      v.osc1Level = 0.85f;
        v.monoMode = 1.0f; v.legatoMode = 1.0f; v.glideTimeMs = 55.0f;
        v.filterType = lp24; v.filterCutoffHz = 230.0f; v.filterResonance = 0.9f;
        v.modEnvDecay = 0.22f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.85f;
        v.attack = 0.001f; v.decay = 0.25f; v.sustain = 0.25f; v.release = 0.05f;
        v.velocityToCutoff = 0.6f; v.velocityToResonance = 0.4f;
        v.accentAmount = 0.98f;
        v.driveAmount = 0.55f; v.fxDistortion = 0.2f;
        v.arpEnabled = 1.0f; v.arpMode = orderUpDown; v.arpPattern = rhythmAcid;
        v.arpRateHz = 8.0f; v.arpOctaves = 2.0f; v.arpGate = 0.85f; v.arpSync = 1.0f;
        v.musicalScale = scaleNaturalMinor;
        v.delayTimeMs = 250.0f; v.delayFeedback = 0.35f; v.delayMix = 0.18f; v.delaySync = 1.0f;
        v.eqLowGain = 2.0f;
        v.masterGain = 0.59f;
    });

    add("Dub Stab", Category::sequence, [](PresetValues& v)
    {
        // One chord, hit short, and then the delay does everything else. The
        // feedback is high enough that the repeats are the part you listen to,
        // which is why the note itself is almost nothing.
        v.wavePosition = square;   v.osc1Level = 0.7f;
        v.osc2Level = 0.45f;       v.osc2WavePosition = saw; v.osc2Fine = -10.0f;
        v.filterType = lp24; v.filterCutoffHz = 1500.0f; v.filterResonance = 0.4f;
        v.modEnvDecay = 0.08f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = 0.5f;
        v.attack = 0.002f; v.decay = 0.12f; v.sustain = 0.0f; v.release = 0.1f;
        v.chordMode = chordTriad; v.musicalScale = scaleNaturalMinor;
        v.arpEnabled = 1.0f; v.arpMode = orderChord; v.arpPattern = rhythmTresillo;
        v.arpRateHz = 4.0f; v.arpOctaves = 1.0f; v.arpGate = 0.3f; v.arpSync = 1.0f;
        v.delayTimeMs = 375.0f; v.delayFeedback = 0.68f; v.delayMix = 0.45f; v.delaySync = 1.0f;
        v.phaserRate = 0.2f; v.phaserFeedback = 0.6f; v.phaserMix = 0.3f;
        v.eqHighGain = -2.0f; v.eqLowGain = 3.0f;
        v.reverbSize = 0.7f; v.reverbMix = 0.25f;
        v.masterGain = 0.88f;
    });

    add("Glide Runner", Category::sequence, [](PresetValues& v)
    {
        // A running line that never stops moving between notes, because the
        // glide is long enough to still be arriving when the next one starts.
        // Shorten the glide and it becomes an ordinary arpeggio; that one dial
        // is the whole difference.
        v.wavePosition = triangle; v.osc1Level = 0.8f;
        v.monoMode = 1.0f; v.legatoMode = 1.0f; v.glideTimeMs = 110.0f;
        v.filterType = lp12; v.filterCutoffHz = 1900.0f; v.filterResonance = 0.5f;
        v.lfo2RateHz = 0.3f; v.lfo2ToCutoff = 0.4f;
        v.attack = 0.004f; v.decay = 0.3f; v.sustain = 0.7f; v.release = 0.12f;
        v.arpEnabled = 1.0f; v.arpMode = orderUpDown; v.arpPattern = rhythmBerlin;
        v.arpRateHz = 8.0f; v.arpOctaves = 2.0f; v.arpGate = 0.95f; v.arpSync = 1.0f;
        v.musicalScale = scaleNaturalMinor;
        v.chorusRate = 0.3f; v.chorusDepth = 0.4f; v.chorusMix = 0.3f;
        v.delayTimeMs = 375.0f; v.delayFeedback = 0.4f; v.delayMix = 0.25f; v.delaySync = 1.0f;
        v.reverbSize = 0.7f; v.reverbMix = 0.28f;
        v.masterGain = 0.82f;
    });
    // ---- FX ----------------------------------------------------------------
    add("Riser", Category::effect, [](PresetValues& v)
    {
        v.wavePosition = saw;
        v.noiseLevel = 0.35f; v.noiseColour = 0.2f;
        v.unisonVoices = 7.0f; v.unisonDetuneCents = 40.0f; v.unisonWidth = 1.0f;
        v.filterType = hp12; v.filterCutoffHz = 400.0f; v.filterResonance = 0.5f;
        v.modEnvAttack = 2.5f; v.modEnvSustain = 1.0f; v.modEnvToCutoff = 0.9f;
        v.attack = 1.8f; v.decay = 0.5f; v.sustain = 1.0f; v.release = 0.4f;
        v.reverbSize = 0.9f; v.reverbMix = 0.4f;
        v.masterGain = 0.56f;
    });

    add("Downlifter", Category::effect, [](PresetValues& v)
    {
        v.wavePosition = bright;
        v.filterType = lp24; v.filterCutoffHz = 9000.0f; v.filterResonance = 0.45f;
        v.modEnvAttack = 0.001f; v.modEnvDecay = 2.4f; v.modEnvSustain = 0.0f;
        v.modEnvToCutoff = -0.9f; v.modEnvToWave = -0.5f;
        v.attack = 0.002f; v.decay = 2.5f; v.sustain = 0.2f; v.release = 0.6f;
        v.reverbSize = 0.85f; v.reverbMix = 0.35f;
        v.masterGain = 0.54f;
    });

    add("Noise Sweep", Category::effect, [](PresetValues& v)
    {
        // Same problem as Choir Pad: a narrow band passes a sliver of what goes
        // into it. Wider, and the noise turned up to fill it.
        v.osc1Level = 0.0f; v.noiseLevel = 1.0f; v.noiseColour = 0.3f;
        v.filterType = bp12; v.filterCutoffHz = 900.0f; v.filterResonance = 0.8f;
        v.lfoRateHz = 0.25f; v.lfoDepth = 0.9f; v.stereoLfo = 1.0f;
        v.attack = 0.4f; v.decay = 1.0f; v.sustain = 0.9f; v.release = 0.8f;
        v.reverbSize = 0.9f; v.reverbMix = 0.4f;
        v.masterGain = 1.00f;
    });

    add("Alien Chatter", Category::effect, [](PresetValues& v)
    {
        v.wavePosition = bright;
        v.lfo2RateHz = 14.0f; v.lfo2ToWave = 0.9f; v.lfo2ToCutoff = 0.6f;
        v.filterType = bp12; v.filterCutoffHz = 1600.0f; v.filterResonance = 0.75f;
        v.attack = 0.005f; v.decay = 0.4f; v.sustain = 0.6f; v.release = 0.2f;
        v.delayTimeMs = 180.0f; v.delayFeedback = 0.5f; v.delayMix = 0.35f;
        v.reverbSize = 0.7f; v.reverbMix = 0.3f;
        v.masterGain = 0.93f;
    });


    add("Impact", Category::effect, [](PresetValues& v)
    {
        // A hit and the room it happened in. Almost all noise, filtered down
        // hard and fast by the mod envelope, over a sub that holds just long
        // enough to be felt rather than heard.
        v.wavePosition = sine;     v.osc1Level = 0.3f;
        v.subLevel = 0.9f;         v.subOctave = 1.0f;
        v.noiseLevel = 0.85f;      v.noiseColour = 0.35f;
        v.filterType = lp24; v.filterCutoffHz = 2600.0f; v.filterResonance = 0.3f;
        v.modEnvDecay = 0.18f; v.modEnvSustain = 0.0f; v.modEnvToCutoff = -0.9f;
        v.attack = 0.001f; v.decay = 0.9f; v.sustain = 0.0f; v.release = 0.8f;
        v.driveAmount = 0.45f;
        v.eqLowGain = 5.0f; v.eqMidGain = -3.0f;
        v.reverbSize = 0.95f; v.reverbMix = 0.45f;
        v.masterGain = 0.12f; // it was the loudest thing in the bank by ten decibels
    });

    add("Reverse Swell", Category::effect, [](PresetValues& v)
    {
        // Backwards without being reversed: the amplitude envelope takes two
        // seconds to arrive and then stops dead, which is what a tape played
        // the other way round sounds like and costs nothing to do forwards.
        v.wavePosition = 0.45f;
        v.unisonVoices = 5.0f; v.unisonDetuneCents = 18.0f; v.unisonWidth = 0.9f;
        v.noiseLevel = 0.25f;  v.noiseColour = 0.6f;
        v.filterType = lp12; v.filterCutoffHz = 600.0f; v.filterResonance = 0.35f;
        v.modEnvAttack = 1.9f; v.modEnvDecay = 0.05f; v.modEnvSustain = 0.0f;
        v.modEnvToCutoff = 0.9f; v.modEnvToWave = 0.5f;
        v.attack = 2.0f; v.decay = 0.05f; v.sustain = 0.0f; v.release = 0.05f;
        v.phaserRate = 0.5f; v.phaserFeedback = 0.6f; v.phaserMix = 0.4f;
        v.reverbSize = 0.85f; v.reverbMix = 0.4f;
        v.masterGain = 0.44f;
    });

    add("Tape Warble", Category::effect, [](PresetValues& v)
    {
        // The chorus pushed past where it flatters anything: slow, deep, and
        // wet enough that the pitch instability is the sound rather than a
        // thickening of it. Hold a chord and it sags like worn tape.
        v.wavePosition = triangle; v.osc1Level = 0.8f;
        v.osc2Level = 0.4f;        v.osc2WavePosition = sine; v.osc2Fine = -18.0f;
        v.filterType = lp12; v.filterCutoffHz = 3000.0f; v.filterResonance = 0.2f;
        v.attack = 0.05f; v.decay = 0.5f; v.sustain = 0.8f; v.release = 0.5f;
        v.chorusRate = 0.18f; v.chorusDepth = 1.0f; v.chorusMix = 0.85f;
        v.eqHighGain = -4.0f; v.eqLowGain = 2.0f;
        v.delayTimeMs = 500.0f; v.delayFeedback = 0.45f; v.delayMix = 0.3f; v.delaySync = 1.0f;
        v.reverbSize = 0.6f; v.reverbMix = 0.25f;
        v.masterGain = 0.56f;
    });
    return bank;
}
} // namespace

const char* categoryName(int category)
{
    return categoryNames[juce::jlimit(0, (int) Category::numCategories - 1, category)];
}

PresetValues init()
{
    PresetValues v;
    v.name = "Init";
    return v;
}

const std::vector<Entry>& all()
{
    // Built once on first use: the bank never changes at runtime.
    static const std::vector<Entry> bank = buildBank();
    return bank;
}

std::vector<const Entry*> search(const juce::String& term, int categoryFilter)
{
    std::vector<const Entry*> matches;
    auto needle = term.trim().toLowerCase();

    for (const auto& entry : all())
    {
        if (categoryFilter >= 0 && (int) entry.category != categoryFilter)
            continue;

        if (needle.isNotEmpty()
            && !entry.name.toLowerCase().contains(needle)
            && !juce::String(categoryName((int) entry.category)).toLowerCase().contains(needle))
            continue;

        matches.push_back(&entry);
    }

    return matches;
}
} // namespace FactoryPresets
} // namespace wavelathe
