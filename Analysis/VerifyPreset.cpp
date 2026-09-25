// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <juce_core/juce_core.h>
#include "FxpPreset.h"
#include <cstdio>

using namespace wavelathe;

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("Usage: VerifyPreset <path-to-fxp>\n");
        return 1;
    }

    juce::String pathArg(argv[1]);
    juce::File file(pathArg);

    std::printf("File: %s\n", file.getFullPathName().toRawUTF8());
    std::printf("Exists: %s   Size: %lld bytes\n", file.existsAsFile() ? "yes" : "NO", (long long) file.getSize());

    PresetValues values;
    juce::String error;
    bool ok = FxpPreset::load(file, values, error);

    if (!ok)
    {
        std::printf("LOAD FAILED: %s\n", error.toRawUTF8());
        return 1;
    }

    std::printf("LOAD OK. name=\"%s\"\n", values.name.toRawUTF8());
    std::printf("  wave=%.3f attack=%.3f decay=%.3f sustain=%.3f release=%.3f\n",
                values.wavePosition, values.attack, values.decay, values.sustain, values.release);
    std::printf("  cutoff=%.1f resonance=%.3f\n", values.filterCutoffHz, values.filterResonance);
    std::printf("  lfoRate=%.3f lfoDepth=%.3f lfoAmpDepth=%.3f\n", values.lfoRateHz, values.lfoDepth, values.lfoAmpDepth);
    std::printf("  unisonVoices=%.1f detune=%.2f width=%.3f drive=%.3f gain=%.3f\n",
                values.unisonVoices, values.unisonDetuneCents, values.unisonWidth, values.driveAmount, values.masterGain);
    return 0;
}
