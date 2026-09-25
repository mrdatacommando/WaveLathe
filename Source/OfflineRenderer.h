// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "WavetableOscillator.h"
#include "SynthParameters.h"
#include "FxpPreset.h"
#include "MasterEffects.h"

namespace wavelathe
{
// Renders WaveLathe's real synth engine offline (no audio device), so a
// candidate parameter set can be measured against a reference recording.
// This deliberately reuses the exact production voice/DSP path used at
// playback time, so whatever the optimizer tunes is what you actually hear.
//
// One instance owns one Synthesiser, so give each worker thread its own.
class OfflineRenderer
{
public:
    static constexpr int renderBlockSize = 512;

    explicit OfflineRenderer(const WavetableSet& tableSet, double sampleRate = 44100.0, int numVoices = 4);

    // Renders a single held note: note-on at t=0, note-off at holdRatio of the
    // total duration so the release tail is captured too. Output is mono.
    void render(const PresetValues& values, int midiNoteNumber, double durationSeconds,
                juce::AudioBuffer<float>& outMono, float holdRatio = 0.85f);

    // Points this renderer at a different wavetable set (which must outlive it).
    void setTables(const WavetableSet& tableSet) { provider.active.store(&tableSet, std::memory_order_release); }

    double getSampleRate() const { return sampleRate; }

private:
    double sampleRate;
    SynthParameters parameters;
    WavetableProvider provider;
    juce::Synthesiser synth;
    MasterEffects masterEffects;
};
} // namespace wavelathe
