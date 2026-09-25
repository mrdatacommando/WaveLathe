// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "OfflineRenderer.h"
#include "SynthVoice.h"
#include "SynthSound.h"

namespace wavelathe
{
OfflineRenderer::OfflineRenderer(const WavetableSet& tableSet, double sampleRateToUse, int numVoices)
    : sampleRate(sampleRateToUse)
{
    provider.active.store(&tableSet, std::memory_order_release);

    synth.addSound(new SynthSound());
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice(new SynthVoice(provider, parameters));

    synth.setCurrentPlaybackSampleRate(sampleRate);

    // Prepared once here rather than per render: preparing allocates the delay
    // line, and this object is called thousands of times inside the optimiser.
    masterEffects.prepare(sampleRate, renderBlockSize, 2);

    // Same noise on every render, so a patch measures the same each time it is
    // tried. Live playing keeps its genuinely random noise; only measurement
    // needs this, and without it a noisy patch scores differently every time
    // the search looks at it and can win a round on luck alone. The same seed
    // for every voice, deliberately: the synthesiser is free to hand a note to
    // whichever voice is idle, and per-voice seeds would put that choice back
    // into the result.
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
            voice->setRepeatableNoise(0x5eed01);
}

void OfflineRenderer::render(const PresetValues& values, int midiNoteNumber, double durationSeconds,
                             juce::AudioBuffer<float>& outMono, float holdRatio)
{
    FxpPreset::applyToSynthParameters(values, parameters);

    constexpr int blockSize = renderBlockSize;
    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
            voice->prepare(sampleRate, blockSize);

    // Same master chain the live output goes through, so what the optimiser
    // measures (and the waveform view draws) is what you actually hear. Reset
    // rather than re-prepare, so each render starts from silence but we do not
    // reallocate on every call.
    masterEffects.reset();

    int totalSamples = juce::jmax(1, (int) (durationSeconds * sampleRate));
    int noteOffSample = juce::jlimit(1, totalSamples, (int) (totalSamples * holdRatio));

    // Render in stereo (the engine pans unison voices across the stereo field),
    // then sum to mono for comparison against the mono reference.
    juce::AudioBuffer<float> stereo(2, totalSamples);
    stereo.clear();

    juce::MidiBuffer midi;
    midi.addEvent(juce::MidiMessage::noteOn(1, midiNoteNumber, 0.9f), 0);
    midi.addEvent(juce::MidiMessage::noteOff(1, midiNoteNumber), noteOffSample);

    for (int start = 0; start < totalSamples; start += blockSize)
    {
        int numThisBlock = juce::jmin(blockSize, totalSamples - start);

        juce::MidiBuffer blockMidi;
        for (const auto metadata : midi)
            if (metadata.samplePosition >= start && metadata.samplePosition < start + numThisBlock)
                blockMidi.addEvent(metadata.getMessage(), metadata.samplePosition - start);

        synth.renderNextBlock(stereo, blockMidi, start, numThisBlock);
    }

    {
        // Apply master effects across the whole rendered note in one pass.
        juce::AudioBuffer<float> fxView(stereo.getArrayOfWritePointers(), 2, totalSamples);
        masterEffects.process(fxView, parameters, (double) parameters.bpm.load());
    }

    outMono.setSize(1, totalSamples, false, false, true);
    outMono.clear();
    outMono.addFrom(0, 0, stereo, 0, 0, totalSamples, 0.5f);
    outMono.addFrom(0, 0, stereo, 1, 0, totalSamples, 0.5f);

    // Leave no note ringing into the next render.
    synth.allNotesOff(1, false);
}
} // namespace wavelathe
