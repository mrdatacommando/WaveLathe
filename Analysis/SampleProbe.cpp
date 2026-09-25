// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "DrumSampleLoader.h"

#include <cmath>
#include <iostream>

// What the drum sample loader makes of a file, and why.
//
// Exists because "could not load that" is the one thing a user can report and
// the one thing that does not say what went wrong. Point this at the file
// they named and it prints the answer: the rate it found, what it trimmed,
// or the reason it refused.
int main(int argc, char* argv[])
{
    if (argc < 2)
    {
        std::cout << "usage: SampleProbe <audio file> [more files...]" << std::endl;
        return 1;
    }

    int failures = 0;

    for (int i = 1; i < argc; ++i)
    {
        const juce::File file(juce::String::fromUTF8(argv[i]));

        std::cout << file.getFileName() << std::endl;
        std::cout << "  " << file.getSize() << " bytes on disk" << std::endl;

        juce::String error;
        const auto sample = wavelathe::drumsamples::load(file, error);

        if (sample == nullptr)
        {
            std::cout << "  REFUSED: " << error << std::endl << std::endl;
            ++failures;
            continue;
        }

        float peak = 0.0f;
        double sumOfSquares = 0.0;

        for (const auto value : sample->data)
        {
            peak = juce::jmax(peak, std::abs(value));
            sumOfSquares += (double) value * (double) value;
        }

        const auto rms = sample->data.empty()
                             ? 0.0
                             : std::sqrt(sumOfSquares / (double) sample->data.size());

        std::cout << "  loaded as \"" << sample->name << "\"" << std::endl;
        std::cout << "  " << sample->sourceRate << " Hz, " << sample->length()
                  << " frames, " << juce::String(sample->seconds(), 4) << " s" << std::endl;
        // How often the waveform crosses zero, as the frequency that implies.
        //
        // This is the line that says whether the bytes were read the right
        // way round. PCM decoded with the endianness swapped is white noise,
        // and white noise crosses zero about half the time - which at 44.1kHz
        // reads as eleven thousand hertz. Real audio is far below that, and a
        // peak near full scale with a low crossing rate is a recording.
        int crossings = 0;

        for (size_t s = 1; s < sample->data.size(); ++s)
            if ((sample->data[s - 1] < 0.0f) != (sample->data[s] < 0.0f))
                ++crossings;

        const auto impliedHz = sample->data.size() > 1
                                   ? (double) crossings * sample->sourceRate
                                         / (double) (sample->data.size() - 1) / 2.0
                                   : 0.0;

        // How much each sample resembles the one before it.
        //
        // The decisive test for endianness, and the reason a crossing rate
        // alone is not enough: a cymbal is legitimately bright and can cross
        // zero nearly as often as noise does. But PCM read with the bytes the
        // wrong way round is white noise, and white noise has no correlation
        // between one sample and the next at all. Anything clearly above zero
        // is a waveform; around zero is a decoding mistake.
        double lagged = 0.0;

        for (size_t s = 1; s < sample->data.size(); ++s)
            lagged += (double) sample->data[s - 1] * (double) sample->data[s];

        const auto correlation = sumOfSquares > 0.0 ? lagged / sumOfSquares : 0.0;

        std::cout << "  peak " << juce::String(peak, 4) << ", rms "
                  << juce::String(rms, 4) << ", zero crossings imply "
                  << juce::String(impliedHz, 0) << " Hz" << std::endl;
        std::cout << "  sample-to-sample correlation " << juce::String(correlation, 3)
                  << (correlation > 0.15 ? "  (a waveform)" : "  (LOOKS LIKE NOISE)")
                  << std::endl << std::endl;
    }

    return failures == 0 ? 0 : 1;
}
