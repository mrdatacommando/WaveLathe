// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <juce_dsp/juce_dsp.h>
#include <set>
#include <memory>
#include "FactoryPresets.h"
#include "OfflineRenderer.h"
#include "WavetableOscillator.h"
#include <cstdio>
#include <cmath>

using namespace wavelathe;

namespace
{
int failures = 0;

void fail(const juce::String& preset, const juce::String& what)
{
    std::printf("  FAIL  %-16s %s\n", preset.toRawUTF8(), what.toRawUTF8());
    ++failures;
}

struct Measurement
{
    float peak = 0.0f;
    float rms = 0.0f;
    bool finite = true;
};

Measurement measure(const juce::AudioBuffer<float>& buffer)
{
    Measurement m;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0)
        return m;

    const float* data = buffer.getReadPointer(0);
    double sum = 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float value = data[i];

        if (!std::isfinite(value))
        {
            m.finite = false;
            return m;
        }

        m.peak = juce::jmax(m.peak, std::abs(value));
        sum += (double) value * (double) value;
    }

    m.rms = (float) std::sqrt(sum / (double) numSamples);
    return m;
}

float decibels(float magnitude)
{
    return magnitude > 1.0e-6f ? 20.0f * std::log10(magnitude) : -120.0f;
}
} // namespace

// Every preset in the bank, played and listened to by a machine. Not whether
// they sound good - nothing here can judge that - but whether they make a sound
// at all, and whether it is a sound anyone can use.
//
// The failures this catches are the ones a bank actually ships with: a patch
// whose envelope never opens, one whose gain staging leaves it twenty decibels
// below everything around it, and one that is loud enough to clip before the
// user has touched anything.
int main()
{
    // ---- Names, before a single sample is rendered -------------------------
    // Since 0.38.2 the bank is also the host's preset menu, and a host shows it
    // as one flat list of names. A blank one is a row nobody can choose; two
    // alike are two rows nobody can tell apart, and the one they pick is not
    // the one they meant. Neither shows up in the browser here, which groups by
    // category and would let a duplicate hide in a different group.
    //
    // Cheap, so it runs first: a bank that cannot be listed is not worth
    // spending two minutes rendering.
    {
        std::printf("Names, as a host would list them:\n");
        const auto& bank = FactoryPresets::all();
        juce::StringArray seen;
        int blank = 0, duplicate = 0;

        for (const auto& entry : bank)
        {
            const auto listed = juce::String(FactoryPresets::categoryName((int) entry.category))
                                + ": " + entry.name;

            if (entry.name.trim().isEmpty())
            {
                fail("(blank)", "has no name, so a host would list an empty row");
                ++blank;
            }

            if (seen.contains(listed))
            {
                fail(entry.name, "appears twice in the bank under the same category");
                ++duplicate;
            }

            seen.add(listed);
        }

        std::printf("  %d presets, %d blank, %d duplicated\n\n",
                    (int) bank.size(), blank, duplicate);
    }

    // On the heap, not the stack: a wavetable set is half a megabyte of tables
    // and the renderer holds a synthesiser and a full effects chain. MSVC
    // reserves a function's whole frame on entry, so as locals these two
    // overflow the default stack before the first line of output is printed.
    auto tables = std::make_unique<WavetableSet>();
    auto renderer = std::make_unique<OfflineRenderer>(*tables, 44100.0, 8);

    // Two notes, because a patch can be perfectly audible at one pitch and
    // silent at another - a filter parked below the fundamental is the usual
    // way, and it only shows up low.
    const int notes[] = {36, 60};
    constexpr double seconds = 1.6;

    // Anything quieter than this is not a quiet patch, it is a broken one.
    constexpr float silenceDb = -60.0f;

    // What every preset should arrive at. Six decibels is enough room to play a
    // chord, stack a second instrument, or lean on the drive, without the first
    // thing anyone does being to reach for the gain.
    constexpr float targetDb = -6.0f;

    // The window either side of it. Tight enough that browsing the bank does
    // not change the listening level, wide enough that a patch is not retuned
    // for a decibel nobody can hear.
    constexpr float toleranceDb = 2.5f;

    // And the spread between the quietest and the loudest matters as much as
    // either: a bank you have to ride the gain through is a bank you stop
    // browsing.
    float quietestDb = 0.0f;
    float loudestDb = -120.0f;
    juce::String quietestName, loudestName;

    const auto& bank = FactoryPresets::all();
    std::printf("Auditing %d presets\n\n", (int) bank.size());
    std::printf("  %-16s %8s %8s\n", "PRESET", "PEAK", "RMS");

    for (const auto& entry : bank)
    {
        float worstPeak = 0.0f;
        float bestPeak = 0.0f;
        float bestRms = 0.0f;
        bool finite = true;

        for (int note : notes)
        {
            juce::AudioBuffer<float> rendered;
            renderer->render(entry.values, note, seconds, rendered);

            const auto m = measure(rendered);

            if (!m.finite)
                finite = false;

            bestPeak = juce::jmax(bestPeak, m.peak);
            bestRms = juce::jmax(bestRms, m.rms);
            worstPeak = worstPeak == 0.0f ? m.peak : juce::jmin(worstPeak, m.peak);
        }

        const float peakDb = decibels(bestPeak);
        // What the output gain would have to be for this patch to arrive where
        // the rest of the bank does. Printed for every preset, not only the
        // failing ones, so a levelling pass is a matter of reading a column
        // rather than of measuring each patch by hand.
        const float suggested = juce::jlimit(
            0.02f, 1.0f, entry.values.masterGain * std::pow(10.0f, (targetDb - peakDb) / 20.0f));

        std::printf("  %-16s %7.1f  %7.1f   %5.2f  %5.2f\n", entry.name.toRawUTF8(), peakDb,
                    decibels(bestRms), entry.values.masterGain, suggested);

        if (!finite)
            fail(entry.name, "produced a sample that is not a number");

        if (peakDb < silenceDb)
            fail(entry.name, "is silent at every note tried");
        else if (decibels(worstPeak) < silenceDb)
            fail(entry.name, "is silent at one of the two notes but not the other");

        if (peakDb > targetDb + toleranceDb)
            fail(entry.name, "is too loud for the bank - see the gain suggested above");
        // The floor does not apply to effects. A riser or a sweep is laid UNDER
        // something else and is supposed to sit below it; holding one to the
        // level of an instrument would make it the loudest thing in the bar.
        // The ceiling still applies to them, because nothing may clip.
        else if (entry.category != FactoryPresets::Category::effect && peakDb < targetDb - toleranceDb
                 && peakDb > silenceDb)
            fail(entry.name, "is too quiet for the bank - see the gain suggested above");

        if (peakDb > loudestDb)
        {
            loudestDb = peakDb;
            loudestName = entry.name;
        }

        if (peakDb < quietestDb)
        {
            quietestDb = peakDb;
            quietestName = entry.name;
        }
    }

    std::printf("\n  loudest:  %-16s %.1f dB\n", loudestName.toRawUTF8(), loudestDb);
    std::printf("  quietest: %-16s %.1f dB\n", quietestName.toRawUTF8(), quietestDb);
    std::printf("  spread:   %.1f dB\n", loudestDb - quietestDb);

    if (loudestDb - quietestDb > 30.0f)
    {
        std::printf("  FAIL  the bank spans more than 30 dB, which is a gain dial per preset\n");
        ++failures;
    }

    // Names are how a preset is found, and two of anything is one too few to
    // tell apart in a list.
    std::set<juce::String> names;
    for (const auto& entry : bank)
        names.insert(entry.name.toLowerCase());

    if ((int) names.size() != (int) bank.size())
    {
        std::printf("  FAIL  two presets share a name\n");
        ++failures;
    }

    std::printf("\n%s\n", failures == 0 ? "ALL BANK AUDIT TESTS PASSED" : "SOME BANK AUDIT TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
