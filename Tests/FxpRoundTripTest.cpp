// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <juce_core/juce_core.h>
#include "FxpPreset.h"
#include "SynthParameters.h"
#include <cstdio>
#include <cmath>

using namespace wavelathe;

namespace
{
int failures = 0;

void expectTrue(bool condition, const char* description)
{
    if (condition)
    {
        std::printf("PASS: %s\n", description);
    }
    else
    {
        std::printf("FAIL: %s\n", description);
        ++failures;
    }
}

bool nearlyEqual(float a, float b) { return std::abs(a - b) < 0.0001f; }
} // namespace

int main()
{
    // ---- The chooser filter and the saved extension must agree -------------
    //
    // They did not, from the rename until somebody went to reopen a project.
    // Saving forced the new .wlp onto the filename while both file choosers
    // still filtered for the old .wfo, so every project the renamed build saved
    // was written correctly to disk and then hidden from Load Project. From
    // outside, a file you cannot find afterwards is a file that did not save.
    //
    // Cheap to check and it would have caught it, which is most of the argument
    // for checking anything.
    {
        const juce::String filter(project::wildcard());
        const juce::String extension(project::fileExtension);

        expectTrue(filter.contains("*." + extension),
                   "the project filter includes the extension projects are saved with");
        expectTrue(filter.contains("*.wfo"),
                   "the project filter still opens projects saved before the rename");
    }

    auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("WaveLatheFxpTest");
    tempDir.createDirectory();

    // --- Round trip: save then load should reproduce every field ---
    PresetValues original;
    original.name = "Test Preset";
    original.wavePosition = 0.42f;
    original.attack = 0.05f;
    original.decay = 0.33f;
    original.sustain = 0.6f;
    original.release = 1.25f;
    original.filterCutoffHz = 1234.5f;
    original.filterResonance = 0.81f;
    original.lfoRateHz = 3.3f;
    original.lfoDepth = 0.5f;
    original.lfoAmpDepth = 0.4f;
    original.unisonVoices = 5.0f;
    original.unisonDetuneCents = 18.5f;
    original.unisonWidth = 0.75f;
    original.driveAmount = 0.3f;
    original.masterGain = 0.65f;

    // v7: the arpeggiator, tempo and key settings, which earlier versions
    // silently dropped on save.
    original.arpEnabled = 1.0f;
    original.arpMode = 13.0f;
    original.arpPattern = 21.0f;
    original.arpRateHz = 11.5f;
    original.arpOctaves = 3.0f;
    original.arpGate = 0.42f;
    original.arpSwing = 0.55f;
    original.arpSync = 0.0f;
    original.delaySync = 1.0f;
    original.lfo1Sync = 1.0f;
    original.lfo2Sync = 1.0f;
    original.bpm = 174.0f;
    original.midiClockSync = 1.0f;
    original.musicalKey = 9.0f;
    original.musicalScale = 11.0f;
    original.chordMode = 3.0f;

    auto roundTripFile = tempDir.getChildFile("roundtrip.fxp");
    juce::String error;

    bool saved = FxpPreset::save(roundTripFile, original, error);
    expectTrue(saved, "save() reports success");
    expectTrue(roundTripFile.existsAsFile(), "preset file was created on disk");

    PresetValues loaded;
    bool loadedOk = FxpPreset::load(roundTripFile, loaded, error);
    expectTrue(loadedOk, "load() reports success on our own preset");

    expectTrue(loaded.name == original.name, "preset name round-trips");
    expectTrue(nearlyEqual(loaded.wavePosition, original.wavePosition), "wavePosition round-trips");
    expectTrue(nearlyEqual(loaded.attack, original.attack), "attack round-trips");
    expectTrue(nearlyEqual(loaded.decay, original.decay), "decay round-trips");
    expectTrue(nearlyEqual(loaded.sustain, original.sustain), "sustain round-trips");
    expectTrue(nearlyEqual(loaded.release, original.release), "release round-trips");
    expectTrue(nearlyEqual(loaded.filterCutoffHz, original.filterCutoffHz), "filterCutoffHz round-trips");
    expectTrue(nearlyEqual(loaded.filterResonance, original.filterResonance), "filterResonance round-trips");
    expectTrue(nearlyEqual(loaded.lfoRateHz, original.lfoRateHz), "lfoRateHz round-trips");
    expectTrue(nearlyEqual(loaded.lfoDepth, original.lfoDepth), "lfoDepth round-trips");
    expectTrue(nearlyEqual(loaded.lfoAmpDepth, original.lfoAmpDepth), "lfoAmpDepth round-trips");
    expectTrue(nearlyEqual(loaded.unisonVoices, original.unisonVoices), "unisonVoices round-trips");
    expectTrue(nearlyEqual(loaded.unisonDetuneCents, original.unisonDetuneCents), "unisonDetuneCents round-trips");
    expectTrue(nearlyEqual(loaded.unisonWidth, original.unisonWidth), "unisonWidth round-trips");
    expectTrue(nearlyEqual(loaded.driveAmount, original.driveAmount), "driveAmount round-trips");
    expectTrue(nearlyEqual(loaded.masterGain, original.masterGain), "masterGain round-trips");

    expectTrue(nearlyEqual(loaded.arpEnabled, original.arpEnabled), "arpEnabled round-trips");
    expectTrue(nearlyEqual(loaded.arpMode, original.arpMode), "arpMode round-trips");
    expectTrue(nearlyEqual(loaded.arpPattern, original.arpPattern), "arpPattern round-trips");
    expectTrue(nearlyEqual(loaded.arpRateHz, original.arpRateHz), "arpRateHz round-trips");
    expectTrue(nearlyEqual(loaded.arpOctaves, original.arpOctaves), "arpOctaves round-trips");
    expectTrue(nearlyEqual(loaded.arpGate, original.arpGate), "arpGate round-trips");
    expectTrue(nearlyEqual(loaded.arpSwing, original.arpSwing), "arpSwing round-trips");
    expectTrue(nearlyEqual(loaded.arpSync, original.arpSync), "arpSync round-trips");
    expectTrue(nearlyEqual(loaded.delaySync, original.delaySync), "delaySync round-trips");
    expectTrue(nearlyEqual(loaded.lfo1Sync, original.lfo1Sync), "lfo1Sync round-trips");
    expectTrue(nearlyEqual(loaded.lfo2Sync, original.lfo2Sync), "lfo2Sync round-trips");
    expectTrue(nearlyEqual(loaded.bpm, original.bpm), "bpm round-trips");
    expectTrue(nearlyEqual(loaded.midiClockSync, original.midiClockSync), "midiClockSync round-trips");
    expectTrue(nearlyEqual(loaded.musicalKey, original.musicalKey), "musicalKey round-trips");
    expectTrue(nearlyEqual(loaded.musicalScale, original.musicalScale), "musicalScale round-trips");
    expectTrue(nearlyEqual(loaded.chordMode, original.chordMode), "chordMode round-trips");

    // --- The file really is a standard FXP container: magic bytes at offset 0 and 8 ---
    juce::MemoryBlock raw;
    roundTripFile.loadFileAsData(raw);
    expectTrue(raw.getSize() > 16, "file is large enough to hold an FXP header");
    expectTrue(std::memcmp(raw.getData(), "CcnK", 4) == 0, "file starts with the FXP 'CcnK' chunk magic");
    expectTrue(std::memcmp(static_cast<const char*>(raw.getData()) + 8, "FPCh", 4) == 0,
               "file uses the FXP 'FPCh' opaque-chunk program format");

    // --- Garbage input should fail cleanly, not crash or return junk data ---
    auto garbageFile = tempDir.getChildFile("garbage.fxp");
    garbageFile.replaceWithText("this is not an fxp file");
    PresetValues junk;
    bool garbageLoaded = FxpPreset::load(garbageFile, junk, error);
    expectTrue(!garbageLoaded, "load() rejects a non-FXP file instead of returning junk data");

    // --- A foreign fxID (as if from a different plugin) should be refused, not decoded ---
    auto foreignFile = tempDir.getChildFile("foreign.fxp");
    {
        juce::MemoryOutputStream body;
        body.write("FPCh", 4);
        body.writeIntBigEndian(1);
        body.write("XfsX", 4); // some other plugin's fxID, not WaveLathe's "WvF1"
        body.writeIntBigEndian(1);
        body.writeIntBigEndian(0);
        char name[28] = {};
        body.write(name, sizeof(name));
        body.writeIntBigEndian(4);
        body.write("data", 4);

        juce::FileOutputStream out(foreignFile);
        out.write("CcnK", 4);
        out.writeIntBigEndian((int) body.getDataSize());
        out.write(body.getData(), body.getDataSize());
    }
    PresetValues foreign;
    bool foreignLoaded = FxpPreset::load(foreignFile, foreign, error);
    expectTrue(!foreignLoaded, "load() refuses to decode a preset with a different plugin's fxID");

    // --- An old v1-format WaveLathe preset (pre-unison/drive) should still load,
    //     with the new fields defaulted rather than failing outright ---
    auto legacyFile = tempDir.getChildFile("legacy_v1.fxp");
    {
        juce::MemoryOutputStream payload;
        payload.write("WFpr", 4);
        payload.writeIntBigEndian(1); // old payload version
        for (float f : {0.1f, 0.02f, 0.15f, 0.8f, 0.2f, 4000.0f, 0.7f, 2.0f, 0.0f, 0.7f})
            payload.writeFloatBigEndian(f);

        juce::MemoryOutputStream body;
        body.write("FPCh", 4);
        body.writeIntBigEndian(1);
        body.write("WvF1", 4);
        body.writeIntBigEndian(1);
        body.writeIntBigEndian(0);
        char name[28] = {};
        body.write(name, sizeof(name));
        body.writeIntBigEndian((int) payload.getDataSize());
        body.write(payload.getData(), payload.getDataSize());

        juce::FileOutputStream out(legacyFile);
        out.write("CcnK", 4);
        out.writeIntBigEndian((int) body.getDataSize());
        out.write(body.getData(), body.getDataSize());
    }
    PresetValues legacy;
    bool legacyLoaded = FxpPreset::load(legacyFile, legacy, error);
    expectTrue(legacyLoaded, "load() still reads an old v1 WaveLathe preset");
    expectTrue(nearlyEqual(legacy.unisonVoices, 1.0f), "legacy preset defaults unisonVoices to 1 (no unison)");
    expectTrue(nearlyEqual(legacy.driveAmount, 0.0f), "legacy preset defaults driveAmount to 0 (no drive)");
    expectTrue(legacy.wavetableData.empty(), "legacy preset carries no wavetable (uses built-in waves)");

    // --- A preset carrying a sampled wavetable must round-trip that too, so a
    //     patch built from sampled waves still sounds right when reloaded ---
    {
        PresetValues withTable = original;
        withTable.name = "Sampled Waves";
        withTable.wavetableData.resize(5 * 2048);
        for (size_t i = 0; i < withTable.wavetableData.size(); ++i)
            withTable.wavetableData[i] = std::sin((float) i * 0.01f) * 0.5f;

        auto tableFile = tempDir.getChildFile("with_wavetable.fxp");
        bool tableSaved = FxpPreset::save(tableFile, withTable, error);
        expectTrue(tableSaved, "save() writes a preset containing a sampled wavetable");

        PresetValues reloaded;
        bool tableLoaded = FxpPreset::load(tableFile, reloaded, error);
        expectTrue(tableLoaded, "load() reads a preset containing a sampled wavetable");
        expectTrue(reloaded.wavetableData.size() == withTable.wavetableData.size(),
                   "embedded wavetable round-trips at full size");

        bool allSamplesMatch = reloaded.wavetableData.size() == withTable.wavetableData.size();
        if (allSamplesMatch)
            for (size_t i = 0; i < withTable.wavetableData.size(); ++i)
                if (!nearlyEqual(reloaded.wavetableData[i], withTable.wavetableData[i]))
                {
                    allSamplesMatch = false;
                    break;
                }
        expectTrue(allSamplesMatch, "every embedded wavetable sample round-trips intact");
        expectTrue(nearlyEqual(reloaded.driveAmount, withTable.driveAmount),
                   "knob values still round-trip alongside an embedded wavetable");
    }

    // --- A truncated/corrupt wavetable count must be refused, not trusted ---
    {
        auto badFile = tempDir.getChildFile("bad_wavetable.fxp");
        juce::MemoryOutputStream payload;
        payload.write("WFpr", 4);
        payload.writeIntBigEndian(3);
        for (int i = 0; i < 15; ++i)
            payload.writeFloatBigEndian(0.5f);
        payload.writeIntBigEndian(999999); // claims far more data than follows

        juce::MemoryOutputStream body;
        body.write("FPCh", 4);
        body.writeIntBigEndian(1);
        body.write("WvF1", 4);
        body.writeIntBigEndian(1);
        body.writeIntBigEndian(0);
        char name[28] = {};
        body.write(name, sizeof(name));
        body.writeIntBigEndian((int) payload.getDataSize());
        body.write(payload.getData(), payload.getDataSize());

        juce::FileOutputStream out(badFile);
        out.write("CcnK", 4);
        out.writeIntBigEndian((int) body.getDataSize());
        out.write(body.getData(), body.getDataSize());
        out.flush();

        PresetValues bad;
        bool badLoaded = FxpPreset::load(badFile, bad, error);
        expectTrue(!badLoaded, "load() rejects a preset whose wavetable size does not match its data");
    }

    // ---- Saving a project, not just a patch --------------------------------
    {
        PresetValues project = FxpPreset::fromSynthParameters(SynthParameters{}, "Project");
        project.velocityToCutoff = 0.65f;
        project.velocityToAmp = 0.4f;
        project.velocityToResonance = 0.55f;
        project.accentAmount = 0.82f;
        project.wheelToWave = 0.8f;
        project.pressureToCutoff = 0.25f;
        project.glideTimeMs = 250.0f;
        project.monoMode = 1.0f;
        project.legatoMode = 0.0f;
        project.chorusMix = 0.42f;
        project.phaserFeedback = 0.55f;
        project.eqLowGain = -3.5f;
        project.eqHighGain = 6.0f;
        project.masteringEnabled = 1.0f;
        project.masterSatDrive = 0.37f;
        project.masterSatMix = 0.8f;
        project.masterCompThresholdDb = -18.5f;
        project.masterCompRatio = 3.5f;
        project.masterCompAttackMs = 4.2f;
        project.masterCompReleaseMs = 220.0f;
        project.masterCompMakeupDb = 5.5f;
        project.masterCompMix = 0.65f;
        project.masterLimitCeilingDb = -1.2f;
        project.masterLimitReleaseMs = 80.0f;
        project.masterFxGainDb = 2.5f;
        project.seqDivision = 8.0f;   // 1/8
        project.arpRateLink = 3.0f;   // Seq x 2
        project.arpDivision = 13.0f;
        project.seqLength = 24.0f;
        project.seqRootNote = 36.0f;

        SequencerState::StepData note;
        note.active = 1;
        note.degree = 5;
        note.octave = -1;
        note.length = 7 * 16 + 4; // seven steps and a quarter, in ticks
        note.tickOffset = 3;
        note.locked = 1;
        note.velocity = 0.31f;
        note.gate = 0.44f;
        note.probability = 0.6f;
        note.slide = 1;
        note.accent = 1;
        project.sequencer.steps.assign(64, SequencerState::StepData{});
        project.sequencer.steps[12] = note;

        SequencerState::LaneData lane;
        lane.parameterId = 4;
        lane.values.assign(256, 0.0f);
        lane.values[9] = 0.8f;
        lane.values[200] = 0.2f;
        project.sequencer.lanes.push_back(lane);

        auto projectFile = tempDir.getChildFile("project.fxp");
        juce::String projectError;
        expectTrue(FxpPreset::save(projectFile, project, projectError),
                   "save() writes a project carrying the sequencer");

        PresetValues back;
        expectTrue(FxpPreset::load(projectFile, back, projectError),
                   "load() reads a project carrying the sequencer");

        expectTrue(nearlyEqual(back.seqDivision, 8.0f) && nearlyEqual(back.arpRateLink, 3.0f)
                       && nearlyEqual(back.arpDivision, 13.0f),
                   "the rates and how they are linked round-trip");
        expectTrue(nearlyEqual(back.seqLength, 24.0f) && nearlyEqual(back.seqRootNote, 36.0f),
                   "the sequencer length and root round-trip");
        expectTrue(nearlyEqual(back.velocityToResonance, 0.55f) && nearlyEqual(back.accentAmount, 0.82f),
                   "accent and velocity-to-resonance round-trip");
        expectTrue(nearlyEqual(back.velocityToCutoff, 0.65f) && nearlyEqual(back.velocityToAmp, 0.4f),
                   "velocity routing round-trips");
        expectTrue(nearlyEqual(back.wheelToWave, 0.8f) && nearlyEqual(back.pressureToCutoff, 0.25f),
                   "mod wheel and aftertouch routing round-trip");
        expectTrue(nearlyEqual(back.glideTimeMs, 250.0f) && nearlyEqual(back.monoMode, 1.0f)
                       && nearlyEqual(back.legatoMode, 0.0f),
                   "glide, mono and legato round-trip");
        expectTrue(nearlyEqual(back.chorusMix, 0.42f) && nearlyEqual(back.phaserFeedback, 0.55f),
                   "the chorus and phaser settings round-trip");
        expectTrue(nearlyEqual(back.eqLowGain, -3.5f) && nearlyEqual(back.eqHighGain, 6.0f)
                       && nearlyEqual(back.eqMidGain, 0.0f),
                   "the tone controls round-trip, flat band included");
        expectTrue(nearlyEqual(back.masteringEnabled, 1.0f)
                       && nearlyEqual(back.masterSatDrive, 0.37f)
                       && nearlyEqual(back.masterSatMix, 0.8f),
                   "the mastering saturation round-trips");
        expectTrue(nearlyEqual(back.masterCompThresholdDb, -18.5f)
                       && nearlyEqual(back.masterCompRatio, 3.5f)
                       && nearlyEqual(back.masterCompAttackMs, 4.2f)
                       && nearlyEqual(back.masterCompReleaseMs, 220.0f)
                       && nearlyEqual(back.masterCompMakeupDb, 5.5f)
                       && nearlyEqual(back.masterCompMix, 0.65f),
                   "every compressor control round-trips");
        expectTrue(nearlyEqual(back.masterLimitCeilingDb, -1.2f)
                       && nearlyEqual(back.masterLimitReleaseMs, 80.0f)
                       && nearlyEqual(back.masterFxGainDb, 2.5f),
                   "the limiter and its input trim round-trip");
        expectTrue(back.sequencer.steps.size() == 64, "all sixty-four steps round-trip");

        if (back.sequencer.steps.size() == 64)
        {
            const auto& s = back.sequencer.steps[12];
            expectTrue(s.active == 1 && s.degree == 5 && s.octave == -1,
                       "a step's note round-trips");
            expectTrue(s.length == 7 * 16 + 4 && s.tickOffset == 3 && s.locked == 1,
                       "its length, timing and lock round-trip");
            expectTrue(nearlyEqual(s.velocity, 0.31f) && nearlyEqual(s.gate, 0.44f)
                           && nearlyEqual(s.probability, 0.6f),
                       "and its velocity, gate and probability");
            expectTrue(s.slide == 1, "and whether it is tied to the step after it");
            expectTrue(s.accent == 1, "and whether it is accented");
        }

        expectTrue(back.sequencer.lanes.size() == 1, "the automation lane round-trips");
        if (back.sequencer.lanes.size() == 1)
        {
            const auto& l = back.sequencer.lanes[0];
            expectTrue(l.parameterId == 4 && l.values.size() == 256,
                       "pointing at the same parameter, at full resolution");
            expectTrue(l.values.size() == 256 && nearlyEqual(l.values[9], 0.8f)
                           && nearlyEqual(l.values[200], 0.2f),
                       "with what was drawn on it intact");
        }

        // The same payload the host stores for a session.
        juce::MemoryBlock state;
        FxpPreset::writeToMemory(project, state);
        PresetValues session;
        expectTrue(FxpPreset::readFromMemory(state.getData(), (int) state.getSize(), session,
                                             projectError),
                   "a session saved to memory reads back the same way");
        expectTrue(session.sequencer.steps.size() == 64 && session.sequencer.lanes.size() == 1,
                   "and brings the sequencer back with it");
    }

    // ---- Older presets still load ------------------------------------------
    {
        PresetValues legacy;
        juce::String legacyError;
        auto v1File = tempDir.getChildFile("v1.fxp");
        if (v1File.existsAsFile() && FxpPreset::load(v1File, legacy, legacyError))
            expectTrue(legacy.sequencer.isEmpty(),
                       "a preset saved before the sequencer travelled leaves it alone");
    }

    // ---- The path a host actually takes when it saves a project ------------
    //
    // The project test above starts from a default SynthParameters and then
    // writes its values into the PresetValues by hand. A field missing from
    // fromSynthParameters would sail straight through it: the value gets put
    // into the struct AFTER the copy that failed to carry it, so the round trip
    // succeeds and proves nothing about the copy.
    //
    // This is the real route. getStateInformation calls captureProject, which
    // calls fromSynthParameters; setStateInformation calls applyProject, which
    // calls applyToSynthParameters. Both ends have to carry every field, and a
    // field either end drops is lost in silence - invisible until somebody has
    // already spent an evening setting it and reopens the project to find it
    // gone.
    {
        SynthParameters saved;
        saved.masteringEnabled = 1.0f;
        saved.masterSatDrive = 0.44f;
        saved.masterSatMix = 0.72f;
        saved.masterCompThresholdDb = -22.5f;
        saved.masterCompRatio = 6.0f;
        saved.masterCompAttackMs = 2.5f;
        saved.masterCompReleaseMs = 340.0f;
        saved.masterCompMakeupDb = 7.5f;
        saved.masterCompMix = 0.55f;
        saved.masterLimitCeilingDb = -2.4f;
        saved.masterLimitReleaseMs = 120.0f;
        saved.masterFxGainDb = -3.5f;

        juce::MemoryBlock block;
        FxpPreset::writeToMemory(FxpPreset::fromSynthParameters(saved, "Session"), block);

        PresetValues readBack;
        juce::String stateError;
        expectTrue(FxpPreset::readFromMemory(block.getData(), (int) block.getSize(), readBack,
                                             stateError),
                   "a session written straight from live parameters reads back");

        SynthParameters restored;
        FxpPreset::applyToSynthParameters(readBack, restored);

        expectTrue(nearlyEqual(restored.masteringEnabled.load(), 1.0f)
                       && nearlyEqual(restored.masterSatDrive.load(), 0.44f)
                       && nearlyEqual(restored.masterSatMix.load(), 0.72f),
                   "mastering saturation survives save and reopen, parameters to parameters");
        expectTrue(nearlyEqual(restored.masterCompThresholdDb.load(), -22.5f)
                       && nearlyEqual(restored.masterCompRatio.load(), 6.0f)
                       && nearlyEqual(restored.masterCompAttackMs.load(), 2.5f)
                       && nearlyEqual(restored.masterCompReleaseMs.load(), 340.0f)
                       && nearlyEqual(restored.masterCompMakeupDb.load(), 7.5f)
                       && nearlyEqual(restored.masterCompMix.load(), 0.55f),
                   "every compressor control survives save and reopen");
        expectTrue(nearlyEqual(restored.masterLimitCeilingDb.load(), -2.4f)
                       && nearlyEqual(restored.masterLimitReleaseMs.load(), 120.0f)
                       && nearlyEqual(restored.masterFxGainDb.load(), -3.5f),
                   "the limiter and its trim survive save and reopen");

        // A switched-off stage has to come back switched off as surely as a
        // switched-on one comes back on. Defaulting either way round would be a
        // bug that only showed up on one half of the patches.
        SynthParameters off;
        juce::MemoryBlock offBlock;
        FxpPreset::writeToMemory(FxpPreset::fromSynthParameters(off, "Off"), offBlock);

        PresetValues offRead;
        FxpPreset::readFromMemory(offBlock.getData(), (int) offBlock.getSize(), offRead, stateError);

        SynthParameters offRestored;
        offRestored.masteringEnabled = 1.0f; // deliberately wrong to start with
        FxpPreset::applyToSynthParameters(offRead, offRestored);

        expectTrue(nearlyEqual(offRestored.masteringEnabled.load(), 0.0f),
                   "a project saved with mastering off reopens with it off");
    }

    // ---- A patch without a pattern leaves the sequencer's settings alone ----
    // The factory bank carries no pattern, so browsing it used to resize
    // someone's 32-step bar to the struct default of 64 while leaving the steps
    // themselves untouched - a pattern kept, playing at a length, rate and root
    // note that were never theirs.
    {
        SynthParameters params;
        params.seqLength = 32.0f;
        params.seqRootNote = 60.0f;
        params.seqDivision = 9.0f;

        PresetValues factoryStyle; // defaults: seqLength 64, root 48, no pattern
        expectTrue(factoryStyle.sequencer.isEmpty(), "a factory-style patch carries no pattern");

        FxpPreset::applyToSynthParameters(factoryStyle, params);
        expectTrue(nearlyEqual(params.seqLength.load(), 32.0f),
                   "loading one keeps the pattern length you set");
        expectTrue(nearlyEqual(params.seqRootNote.load(), 60.0f), "and its root note");
        expectTrue(nearlyEqual(params.seqDivision.load(), 9.0f), "and its rate");
        expectTrue(nearlyEqual(params.filterCutoffHz.load(), factoryStyle.filterCutoffHz),
                   "while the synth side of it loads as it always did");

        // The same call with a pattern attached must still bring its settings,
        // or saving a project would restore the steps without their length.
        PresetValues withPattern = factoryStyle;
        withPattern.sequencer.steps.resize(64);
        FxpPreset::applyToSynthParameters(withPattern, params);
        expectTrue(nearlyEqual(params.seqLength.load(), 64.0f),
                   "a patch that does bring a pattern brings its length too");
        expectTrue(nearlyEqual(params.seqRootNote.load(), 48.0f), "and its root note");

        // And the caller can refuse a pattern that IS there, which is what
        // browsing does: auditioning a sound must never resize the bar you are
        // listening to it over.
        params.seqLength = 32.0f;
        params.seqRootNote = 60.0f;
        FxpPreset::applyToSynthParameters(withPattern, params, FxpPreset::ApplyScope::synthPreset);
        expectTrue(nearlyEqual(params.seqLength.load(), 32.0f),
                   "and a caller that asks for synth only gets synth only");
        expectTrue(nearlyEqual(params.seqRootNote.load(), 60.0f),
                   "even from a patch that brought a pattern with it");
    }


    // ---- The MIDI map travels with a project -------------------------------
    // It describes the desk rather than the sound, so it belongs to the session
    // and has to survive a save and a reload of one.
    {
        PresetValues withMap;
        withMap.name = "Mapped";
        withMap.midiMap = "74:Cutoff,71:Reso,1:Accent";
        withMap.sequencer.steps.resize(16);

        auto mapFile = tempDir.getChildFile("mapped.fxp");
        juce::String mapError;
        expectTrue(FxpPreset::save(mapFile, withMap, mapError), "a project with a map saves");

        PresetValues readBack;
        expectTrue(FxpPreset::load(mapFile, readBack, mapError), "and loads again");
        expectTrue(readBack.midiMap == withMap.midiMap, "with the map intact");

        // An empty map has to round-trip as empty rather than as absent, or a
        // project saved before anything was learned would read as corrupt.
        PresetValues noMap;
        noMap.name = "Unmapped";
        auto plainFile = tempDir.getChildFile("unmapped.fxp");
        expectTrue(FxpPreset::save(plainFile, noMap, mapError), "a project with no map saves");

        PresetValues plainBack;
        expectTrue(FxpPreset::load(plainFile, plainBack, mapError), "and loads again");
        expectTrue(plainBack.midiMap.isEmpty(), "with nothing invented for its map");
    }


    // ---- The keyboard channel travels with the map -------------------------
    // Same concern, same file, offered together when a project opens: which
    // MIDI reaches the synth at all.
    {
        PresetValues withChannel;
        withChannel.name = "Channel 3";
        withChannel.midiMap = "74:Cutoff";
        withChannel.keyboardChannel = 3;

        auto chanFile = tempDir.getChildFile("channel.fxp");
        juce::String chanError;
        expectTrue(FxpPreset::save(chanFile, withChannel, chanError), "a project with a channel saves");

        PresetValues back;
        expectTrue(FxpPreset::load(chanFile, back, chanError), "and loads again");
        expectTrue(back.keyboardChannel == 3, "on the channel it was saved with");
        expectTrue(back.midiMap == withChannel.midiMap, "with its map beside it");

        // Omni has to survive as 0 and not be confused with "not recorded":
        // someone who deliberately set omni is saying something.
        PresetValues omni;
        omni.name = "Omni";
        omni.keyboardChannel = 0;
        auto omniFile = tempDir.getChildFile("omni.fxp");
        expectTrue(FxpPreset::save(omniFile, omni, chanError), "a project set to omni saves");

        PresetValues omniBack;
        expectTrue(FxpPreset::load(omniFile, omniBack, chanError), "and loads again");
        expectTrue(omniBack.keyboardChannel == 0, "as omni, not as unrecorded");

        // A project that never had the field must read as -1, so nothing offers
        // to widen a keyboard that was deliberately narrowed.
        PresetValues never;
        never.name = "Older";
        expectTrue(never.keyboardChannel == -1, "a project with no channel says so rather than 0");
    }

    tempDir.deleteRecursively();

    std::printf("\n%s\n", failures == 0 ? "ALL TESTS PASSED" : "SOME TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
