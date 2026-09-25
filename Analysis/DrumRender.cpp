// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "Drums/DrumKit.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <iostream>
#include <memory>
#include <vector>

// Renders a bar of drums to a WAV, so the kit can be listened to.
//
// Not a test, and named so that Tools\run-tests.ps1 leaves it alone - it sits
// with WavAnalyzer, MatchProbe and VerifyPreset, which are tools rather than
// suites. DrumVoiceTest measures what the voices do and will fail if any of it
// changes; this is for the half of "does it sound right" that no measurement
// settles, and for hearing phase 3 before phase 4 wires it to the sequencer.
//
//     DrumRender out.wav [bpm] [bars]
//
// Takes no parameters beyond tempo and length on purpose. Every voice is at its
// own default, which is what the kit will sound like the first time anybody
// loads it - and that is the thing worth listening to.
using namespace drums;

namespace
{
constexpr int kick = 0, snare = 1, rim = 2, clap = 3;
constexpr int loTom = 4, cowbell = 7;
constexpr int closedHat = 8, openHat = 9, crash = 10;
constexpr int clave = 12, maraca = 13;

struct Beat
{
    int voice;
    int cell;        // 32nds from the top of the bar; 32 to a bar of four four
    float velocity;
    bool accent;
    int nudge;       // 32nds are the grid, this is the feel: +/- a 96th or so
};

// A bar with something for most of the rows to do, and deliberately not a
// quantised one. The open hat on the and-of-four is choked by the closed hat
// that follows it, which is the one interaction in the kit worth hearing.
const std::vector<Beat> bar = {
    { kick,       0, 1.00f, true,   0 },
    { closedHat,  2, 0.55f, false,  2 },
    { clave,      3, 0.45f, false,  0 },
    { closedHat,  4, 0.70f, false,  0 },
    { snare,      8, 0.95f, true,   0 },
    { closedHat, 10, 0.55f, false,  2 },
    { maraca,    11, 0.40f, false,  0 },
    { closedHat, 12, 0.65f, false,  0 },
    { kick,      14, 0.80f, false, -2 },
    { kick,      16, 0.95f, false,  0 },
    { closedHat, 18, 0.55f, false,  2 },
    { rim,       19, 0.50f, false,  0 },
    { closedHat, 20, 0.70f, false,  0 },
    { snare,     24, 1.00f, true,   0 },
    { clap,      24, 0.75f, false,  1 },
    { openHat,   26, 0.70f, false,  0 },
    { cowbell,   27, 0.45f, false,  0 },
    { closedHat, 28, 0.80f, false,  0 },   // chokes the open hat above it
    { loTom,     30, 0.70f, false,  0 },
    { kick,      31, 0.60f, false,  1 }
};
} // namespace

int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "DrumRender <out.wav> [bpm] [bars]" << std::endl;
        return 1;
    }

    const juce::File out = juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]);
    const auto bpm = argc > 2 ? juce::jlimit(40.0, 240.0, juce::String(argv[2]).getDoubleValue()) : 96.0;
    const auto bars = argc > 3 ? juce::jlimit(1, 64, juce::String(argv[3]).getIntValue()) : 4;

    constexpr double sampleRate = 44100.0;
    constexpr int cellsPerBar = 32;

    const auto samplesPerCell = (int) (sampleRate * 60.0 / bpm / 8.0);   // a 32nd
    const auto samplesPerNudge = juce::jmax(1, samplesPerCell / 4);
    const auto barSamples = samplesPerCell * cellsPerBar;

    // A tail on the end, so the crash that lands on the last bar is not cut off
    // by the file ending. Three seconds is past the longest voice in the kit at
    // its default decay.
    const auto total = barSamples * bars + (int) (3.0 * sampleRate);

    auto kit = std::make_unique<Kit>();
    kit->prepare(sampleRate);

    auto params = std::make_unique<KitParameters>();

    juce::AudioBuffer<float> buffer(2, total);
    buffer.clear();

    // Rendered in host-sized blocks rather than one long call, because that is
    // how it will actually be played - and because DrumVoiceTest proves the two
    // agree sample for sample, which means this is a fair rehearsal of it.
    constexpr int blockSize = 512;
    int position = 0;

    while (position < total)
    {
        const auto block = juce::jmin(blockSize, total - position);

        for (int repeat = 0; repeat < bars; ++repeat)
        {
            const auto barStart = repeat * barSamples;

            for (const auto& beat : bar)
            {
                const auto at = barStart + beat.cell * samplesPerCell + beat.nudge * samplesPerNudge;

                if (at >= position && at < position + block)
                    kit->trigger(beat.voice, beat.velocity, beat.accent, at - position);
            }
        }

        // One crash to open with, so the first bar has a downbeat worth having.
        if (position == 0)
            kit->trigger(crash, 0.8f, false, 0);

        kit->renderAdding(buffer, position, block, *params);
        position += block;
    }

    const auto dropped = kit->takeDroppedHits();
    if (dropped > 0)
        std::cout << "  WARNING: " << dropped << " hits did not fit in the queue" << std::endl;

    float peak = 0.0f;
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        peak = juce::jmax(peak, buffer.getMagnitude(channel, 0, total));

    juce::WavAudioFormat format;
    std::unique_ptr<juce::FileOutputStream> file(out.createOutputStream());

    if (file == nullptr)
    {
        std::cout << "  could not write " << out.getFullPathName() << std::endl;
        return 1;
    }

    file->setPosition(0);
    file->truncate();

    std::unique_ptr<juce::OutputStream> stream = std::move(file);

    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(sampleRate)
                             .withNumChannels(2)
                             .withBitsPerSample(24);

    auto writer = format.createWriterFor(stream, options);

    if (writer == nullptr)
    {
        std::cout << "  could not make a writer" << std::endl;
        return 1;
    }

    writer->writeFromAudioSampleBuffer(buffer, 0, total);
    writer.reset();

    std::cout << "  wrote " << out.getFullPathName() << std::endl;
    std::cout << "  " << bars << " bars at " << juce::String(bpm, 1) << " bpm, "
              << juce::String(total / sampleRate, 2) << " s, peak "
              << juce::String(juce::Decibels::gainToDecibels(peak), 1) << " dB" << std::endl;

    return 0;
}
