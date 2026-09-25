// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

// Command-line front end for the SampleMatch acoustic-analysis engine
// (Source/SampleMatcher.h): prints a report on a reference WAV recording and
// optionally writes out a WaveLathe .fxp preset estimated from it. This only
// looks at rendered audio a user provides as a reference - it does not read
// or decode any synth's proprietary preset/project format.

#include <juce_core/juce_core.h>
#include "SampleMatcher.h"
#include "PresetOptimizer.h"
#include "WavetableOscillator.h"
#include "OfflineRenderer.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <cstdio>
#include <cmath>
#include <memory>

using namespace wavelathe;
using namespace wavelathe::SampleMatch;

namespace
{
void printGlobalStats(const float* data, int numSamples, double sampleRate)
{
    float peakAbs = 0.0f;
    double sumSquares = 0.0;
    for (int i = 0; i < numSamples; ++i)
    {
        peakAbs = juce::jmax(peakAbs, std::abs(data[i]));
        sumSquares += (double) data[i] * data[i];
    }
    double rms = std::sqrt(sumSquares / numSamples);
    double crestFactorDb = 20.0 * std::log10((peakAbs + 1e-9) / (rms + 1e-9));
    std::printf("Peak: %.3f (%.1f dBFS)   RMS: %.4f   Crest factor: %.1f dB\n",
                peakAbs, 20.0 * std::log10(peakAbs + 1e-9), rms, crestFactorDb);

    int envHop = (int) (sampleRate * 0.01);
    std::printf("\nAmplitude envelope (10ms steps, 0-9 = level):\n");
    for (int i = 0; i < numSamples; i += envHop)
    {
        int end = juce::jmin(i + envHop, numSamples);
        double sum = 0.0;
        for (int j = i; j < end; ++j) sum += std::abs(data[j]);
        double level = sum / (end - i);
        int bar = juce::jlimit(0, 9, (int) (level / (peakAbs + 1e-9) * 9.0));
        std::putchar('0' + bar);
    }
    std::printf("\n\n");
}
} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf("Usage: WavAnalyzer <path-to-wav> [--preset <output.fxp>] [--name \"Preset Name\"]\n");
        std::printf("                   [--note N] [--optimize] [--generations N]\n");
        return 1;
    }

    juce::String pathArg(argv[1]);
    juce::File file(pathArg);

    juce::String presetOutputPath;
    juce::String presetName = file.getFileNameWithoutExtension();
    int chosenNoteNumber = -1; // 1-based; -1 = auto-pick the longest/most reliable note
    bool runOptimizer = false;
    int generations = 60;
    juce::String sweepParam;
    bool sampleWavetable = false;
    bool aliasTest = false;
    juce::String renderMatchPath;

    for (int a = 2; a < argc; ++a)
    {
        juce::String arg(argv[a]);
        if (arg == "--preset" && a + 1 < argc) presetOutputPath = juce::String(argv[++a]);
        else if (arg == "--name" && a + 1 < argc) presetName = juce::String(argv[++a]);
        else if (arg == "--note" && a + 1 < argc) chosenNoteNumber = juce::String(argv[++a]).getIntValue();
        else if (arg == "--optimize") runOptimizer = true;
        else if (arg == "--generations" && a + 1 < argc) generations = juce::String(argv[++a]).getIntValue();
        else if (arg == "--sweep" && a + 1 < argc) sweepParam = juce::String(argv[++a]);
        else if (arg == "--sample-wavetable") sampleWavetable = true;
        else if (arg == "--alias-test") aliasTest = true;
        else if (arg == "--render-match" && a + 1 < argc) renderMatchPath = juce::String(argv[++a]);
    }

    double sampleRate = 0.0;
    auto mono = loadMono(file, sampleRate);

    if (mono.getNumSamples() == 0)
    {
        std::printf("Could not open/decode: %s\n", file.getFullPathName().toRawUTF8());
        return 1;
    }

    auto* data = mono.getReadPointer(0);
    int numSamples = mono.getNumSamples();
    double duration = numSamples / sampleRate;

    std::printf("File: %s\n", file.getFileName().toRawUTF8());
    std::printf("Sample rate: %.0f Hz, Duration: %.2f s, Samples: %d\n\n", sampleRate, duration, numSamples);

    printGlobalStats(data, numSamples, sampleRate);

    PresetValues presetValues;
    std::vector<SegmentReport> reports;
    juce::String matchError;
    matchFile(file, presetName, chosenNoteNumber - 1, presetValues, matchError, &reports);

    if (reports.size() == 1)
    {
        auto& report = reports.front();

        std::printf("Detected a single note event (no separate notes found).\n");
        std::printf("Estimated fundamental pitch: %.1f Hz  (~%s)\n",
                     report.fundamentalHz, noteName(report.fundamentalHz).toRawUTF8());
        std::printf("Pitch clarity: %.0f%%  (%s)\n", report.harmonicFitPercent,
                     report.clarityVerdict().toRawUTF8());
        std::printf("Spectral centroid (brightness): %.0f Hz\n", report.spectral.centroidHz);
        std::printf("85%% energy rolloff (approx filter cutoff): %.0f Hz\n", report.spectral.rolloff85Hz);

        std::printf("\nAmplitude beating (oscillator detune / LFO signature):\n");
        std::printf("  Beat rate: %.2f Hz   Modulation depth: %.0f%%%s\n",
                     report.beat.beatHz, report.beat.depthPercent,
                     report.beat.reliable ? "" : "  (short window - low confidence)");
        if (report.fundamentalHz > 0.0f && report.beat.beatHz > 0.0f)
        {
            double detuneCents = 1200.0 * std::log2(1.0 + report.beat.beatHz / report.fundamentalHz);
            std::printf("  Implied detune between voices: ~%.1f cents (if a 2-voice beat)\n", detuneCents);
        }
    }
    else
    {
        // Multiple notes detected: report each, then compare beat rate across pitch
        // to tell oscillator-detune beating (scales with pitch) apart from a
        // fixed-rate LFO (constant regardless of pitch).
        std::printf("Detected %d note events (played one after another):\n\n", (int) reports.size());
        std::printf("%-6s %-8s %-8s %-8s %-9s %-9s %-8s %-8s %s\n",
                    "Note#", "Start(s)", "Dur(s)", "Pitch", "NoteName", "Centroid", "Beat(Hz)", "Clarity", "Verdict");

        for (size_t i = 0; i < reports.size(); ++i)
        {
            auto& r = reports[i];
            std::printf("%-6d %-8.2f %-8.2f %-8.1f %-9s %-9.0f %-8.2f %-8.0f %s\n",
                         (int) i + 1, r.startTimeSec, r.durationSec, r.fundamentalHz,
                         noteName(r.fundamentalHz).toRawUTF8(), r.spectral.centroidHz,
                         r.beat.beatHz, r.harmonicFitPercent, r.clarityVerdict().toRawUTF8());
        }

        int cleanCount = 0, unreliableCount = 0;
        for (auto& r : reports)
        {
            if (r.isCleanSingleNote()) ++cleanCount;
            if (r.looksPolyphonic()) ++unreliableCount;
        }

        std::printf("\nNote detection is monophonic: it separates notes played one after another,\n"
                     "but cannot resolve simultaneous notes. 'Clarity' is how cleanly the segment\n"
                     "repeats at one period - low means a chord, overlapping notes, or a note\n"
                     "ringing over the previous one's tail, so its pitch is not trustworthy.\n");
        std::printf("%d of %d segments are clean single notes; %d are unreliable.\n", cleanCount,
                     (int) reports.size(), unreliableCount);

        std::vector<double> beatHzVals, beatRatioVals;
        for (auto& r : reports)
        {
            if (r.beat.beatHz > 0.0f && r.fundamentalHz > 0.0f && r.beat.reliable && r.beat.depthPercent > 10.0f)
            {
                beatHzVals.push_back(r.beat.beatHz);
                beatRatioVals.push_back(r.beat.beatHz / r.fundamentalHz);
            }
        }

        std::printf("\n");
        if (beatHzVals.size() < 2)
        {
            std::printf("Not enough reliable beating notes across different pitches to compare scaling.\n");
        }
        else
        {
            auto coeffOfVariation = [](const std::vector<double>& v)
            {
                double mean = 0.0;
                for (auto x : v) mean += x;
                mean /= v.size();
                double var = 0.0;
                for (auto x : v) var += (x - mean) * (x - mean);
                var /= v.size();
                return mean > 0.0 ? std::sqrt(var) / mean : 1e9;
            };

            double cvBeatHz = coeffOfVariation(beatHzVals);
            double cvBeatRatio = coeffOfVariation(beatRatioVals);

            std::printf("Beat rate consistency across notes:\n");
            std::printf("  Raw beat Hz variability:        %.1f%% (constant Hz would suggest an LFO)\n", cvBeatHz * 100.0);
            std::printf("  Beat/pitch ratio variability:   %.1f%% (constant ratio would suggest cents-based unison detune)\n", cvBeatRatio * 100.0);

            if (cvBeatRatio < cvBeatHz * 0.7)
                std::printf("  -> Beat rate scales with pitch: consistent with oscillator UNISON DETUNE (in cents).\n");
            else if (cvBeatHz < cvBeatRatio * 0.7)
                std::printf("  -> Beat rate stays roughly constant regardless of pitch: consistent with an LFO.\n");
            else
                std::printf("  -> Ambiguous - likely a mix of both detune and LFO modulation.\n");
        }
    }

    int idx = chosenNoteNumber - 1;
    if (idx < 0 || idx >= (int) reports.size())
    {
        idx = 0;
        for (int i = 1; i < (int) reports.size(); ++i)
            if (reports[(size_t) i].durationSec > reports[(size_t) idx].durationSec)
                idx = i;
    }

    if (runOptimizer)
    {
        SampleMatch::MatchContext context;
        juce::String contextError;
        if (!SampleMatch::analyzeFile(file, idx, context, contextError))
        {
            std::printf("\nOptimizer failed: %s\n", contextError.toRawUTF8());
            return 1;
        }

        const auto& seg = context.segments[(size_t) context.chosenIndex];
        const auto& chosenReport = context.reports[(size_t) context.chosenIndex];
        int midiNote = SampleMatch::midiNoteForFrequency(chosenReport.fundamentalHz);

        if (midiNote <= 0)
        {
            std::printf("\nOptimizer skipped: no reliable pitch detected for note #%d.\n", idx + 1);
        }
        else
        {
            std::printf("\nClosed-loop matching against note #%d (%s, MIDI %d), %d generations...\n",
                         idx + 1, noteName(chosenReport.fundamentalHz).toRawUTF8(), midiNote, generations);

            auto tablesOwner = std::make_unique<WavetableSet>();
            auto& tables = *tablesOwner;
            if (sampleWavetable)
            {
                juce::String tableError;
                if (tables.loadFromAudio(context.mono.getReadPointer(0) + seg.startSample,
                                          seg.endSample - seg.startSample, context.sampleRate,
                                          chosenReport.fundamentalHz, tableError))
                {
                    std::printf("Using wavetable sampled from the reference itself (%d frames).\n", numTables);
                    // Travel with the preset, so it still sounds right on reload.
                    presetValues.wavetableData = tables.getRawTables();
                }
                else
                {
                    std::printf("Wavetable sampling failed (%s); using built-in waves.\n", tableError.toRawUTF8());
                }
            }

            SoundMatch::OptimizerSettings settings;
            settings.generations = generations;

            auto startTime = juce::Time::getMillisecondCounterHiRes();

            // Include the note's tail so its release is actually measurable.
            float holdRatio = 0.85f;
            int windowLength = SampleMatch::windowWithTail(context, context.chosenIndex, 0.8, holdRatio);

            auto optimized = SoundMatch::optimize(
                tables, context.mono.getReadPointer(0) + seg.startSample, windowLength,
                context.sampleRate, midiNote, presetValues, settings,
                [](int gen, int total, float best)
                {
                    if (gen % 10 == 0 || gen == total)
                        std::printf("  generation %3d/%d   best distance %.3f\n", gen, total, best);
                },
                holdRatio);

            double elapsedSeconds = (juce::Time::getMillisecondCounterHiRes() - startTime) / 1000.0;

            std::printf("\n  Heuristic seed distance: %.3f\n", optimized.startingDistance);
            std::printf("  Optimized distance:      %.3f  (%.1f%% closer)\n", optimized.bestDistance,
                         optimized.startingDistance > 0.0f
                             ? 100.0 * (optimized.startingDistance - optimized.bestDistance) / optimized.startingDistance
                             : 0.0);
            std::printf("  %d renders in %.1fs\n", optimized.evaluations, elapsedSeconds);

            presetValues = optimized.best;
        }
    }

    if (aliasTest)
    {
        // Renders a wavetable sampled from this file across a wide pitch range
        // and measures how much energy lands away from the harmonic series.
        // Aliased partials are inharmonic, so a high figure here means the
        // table is folding its upper harmonics back down as audible junk.
        SampleMatch::MatchContext context;
        juce::String contextError;
        if (SampleMatch::analyzeFile(file, idx, context, contextError))
        {
            const auto& seg = context.segments[(size_t) context.chosenIndex];
            const auto& chosenReport = context.reports[(size_t) context.chosenIndex];

            auto tablesOwner = std::make_unique<WavetableSet>();
            auto& tables = *tablesOwner;
            juce::String tableError;
            if (tables.loadFromAudio(context.mono.getReadPointer(0) + seg.startSample,
                                      seg.endSample - seg.startSample, context.sampleRate,
                                      chosenReport.fundamentalHz, tableError))
            {
                std::printf("\nAliasing check - wavetable sampled from %s (%s), played across the keyboard:\n",
                             file.getFileName().toRawUTF8(), noteName(chosenReport.fundamentalHz).toRawUTF8());
                std::printf("  %-8s %-8s %-10s %s\n", "note", "Hz", "mip", "inharmonic energy");

                PresetValues clean;
                clean.wavePosition = 0.5f;
                clean.attack = 0.005f;
                clean.decay = 0.5f;
                clean.sustain = 1.0f;
                clean.release = 0.05f;
                clean.filterCutoffHz = 20000.0f;
                clean.filterResonance = 0.2f;
                clean.unisonVoices = 1.0f;
                clean.unisonDetuneCents = 0.0f;
                clean.driveAmount = 0.0f;
                clean.lfoDepth = 0.0f;
                clean.lfoAmpDepth = 0.0f;
                clean.masterGain = 0.8f;

                constexpr double renderRate = 44100.0;
                OfflineRenderer renderer(tables, renderRate);

                for (int midiNote = 24; midiNote <= 108; midiNote += 12)
                {
                    double f0 = juce::MidiMessage::getMidiNoteInHertz(midiNote);
                    juce::AudioBuffer<float> rendered;
                    renderer.render(clean, midiNote, 0.5, rendered, 0.9f);

                    // Steady portion only, so the attack transient doesn't count.
                    int start = rendered.getNumSamples() / 4;
                    int length = 16384;
                    if (start + length > rendered.getNumSamples())
                        length = rendered.getNumSamples() - start;

                    int order = 14;
                    while ((1 << order) > length) --order;
                    int fftSize = 1 << order;

                    juce::dsp::FFT fft(order);
                    juce::dsp::WindowingFunction<float> window((size_t) fftSize,
                                                                juce::dsp::WindowingFunction<float>::hann);
                    std::vector<float> frame((size_t) fftSize * 2, 0.0f);
                    std::copy(rendered.getReadPointer(0) + start, rendered.getReadPointer(0) + start + fftSize,
                              frame.begin());
                    window.multiplyWithWindowingTable(frame.data(), (size_t) fftSize);
                    fft.performFrequencyOnlyForwardTransform(frame.data());

                    double binHz = renderRate / fftSize;
                    double harmonicEnergy = 0.0, totalEnergy = 0.0;

                    for (int bin = 1; bin < fftSize / 2; ++bin)
                    {
                        double freq = bin * binHz;
                        double energy = (double) frame[(size_t) bin] * frame[(size_t) bin];
                        totalEnergy += energy;

                        // Within a few bins of any harmonic of the fundamental?
                        double nearestHarmonic = std::round(freq / f0);
                        if (nearestHarmonic >= 1.0 && std::abs(freq - nearestHarmonic * f0) <= binHz * 3.0)
                            harmonicEnergy += energy;
                    }

                    double inharmonicPercent =
                        totalEnergy > 0.0 ? 100.0 * (totalEnergy - harmonicEnergy) / totalEnergy : 0.0;

                    std::printf("  %-8s %-8.1f %-10d %.2f%%\n", noteName((float) f0).toRawUTF8(), f0,
                                 mipLevelForFrequency((float) f0), inharmonicPercent);
                }
            }
            else
            {
                std::printf("\nAliasing check skipped: %s\n", tableError.toRawUTF8());
            }
        }
    }

    if (renderMatchPath.isNotEmpty())
    {
        // Render the current settings at the reference note and write both out
        // back to back, so the match can be judged by ear rather than only by
        // the distance number.
        SampleMatch::MatchContext context;
        juce::String contextError;
        if (SampleMatch::analyzeFile(file, idx, context, contextError))
        {
            const auto& seg = context.segments[(size_t) context.chosenIndex];
            const auto& chosenReport = context.reports[(size_t) context.chosenIndex];
            int midiNote = SampleMatch::midiNoteForFrequency(chosenReport.fundamentalHz);

            if (midiNote > 0)
            {
                auto tablesOwner = std::make_unique<WavetableSet>();
                auto& tables = *tablesOwner;

                if (sampleWavetable)
                {
                    juce::String tableError;
                    tables.loadFromAudio(context.mono.getReadPointer(0) + seg.startSample,
                                          seg.endSample - seg.startSample, context.sampleRate,
                                          chosenReport.fundamentalHz, tableError);
                }

                int segLength = seg.endSample - seg.startSample;
                double durationSeconds = segLength / context.sampleRate;

                OfflineRenderer renderer(tables, context.sampleRate);
                juce::AudioBuffer<float> rendered;
                renderer.render(presetValues, midiNote, durationSeconds, rendered);

                juce::File outFile(renderMatchPath);
                juce::String writeError;
                if (SampleMatch::writeAbComparison(outFile, context.mono.getReadPointer(0) + seg.startSample,
                                                   segLength, rendered.getReadPointer(0),
                                                   rendered.getNumSamples(), context.sampleRate, writeError))
                    std::printf("\nWrote A/B comparison: %s\n  (reference note first, then the match, "
                                 "level-matched)\n",
                                 outFile.getFullPathName().toRawUTF8());
                else
                    std::printf("\nCould not write A/B comparison: %s\n", writeError.toRawUTF8());
            }
        }
    }

    if (sweepParam.isNotEmpty())
    {
        SampleMatch::MatchContext context;
        juce::String contextError;
        if (SampleMatch::analyzeFile(file, idx, context, contextError))
        {
            const auto& seg = context.segments[(size_t) context.chosenIndex];
            const auto& chosenReport = context.reports[(size_t) context.chosenIndex];
            int midiNote = SampleMatch::midiNoteForFrequency(chosenReport.fundamentalHz);

            if (midiNote > 0)
            {
                auto tablesOwner = std::make_unique<WavetableSet>();
                auto& tables = *tablesOwner;
                std::printf("\nSweeping '%s' (all other settings held at the current preset):\n",
                             sweepParam.toRawUTF8());
                std::printf("  %-10s %-12s\n", "value", "distance");

                for (int step = 0; step <= 10; ++step)
                {
                    float t = step / 10.0f;
                    auto variant = presetValues;
                    float value = 0.0f;

                    if (sweepParam == "drive") { value = t; variant.driveAmount = value; }
                    else if (sweepParam == "resonance") { value = 0.1f + t * 0.9f; variant.filterResonance = value; }
                    else if (sweepParam == "wave") { value = t; variant.wavePosition = value; }
                    else if (sweepParam == "detune") { value = t * 50.0f; variant.unisonDetuneCents = value; }
                    else { std::printf("  unknown parameter '%s'\n", sweepParam.toRawUTF8()); break; }

                    float score = SoundMatch::scorePreset(tables, context.mono.getReadPointer(0) + seg.startSample,
                                                           seg.endSample - seg.startSample, context.sampleRate,
                                                           midiNote, variant);
                    std::printf("  %-10.3f %-12.3f\n", value, score);
                }
            }
        }
    }

    if (presetOutputPath.isNotEmpty())
    {
        juce::File outFile(presetOutputPath);
        outFile.getParentDirectory().createDirectory();
        juce::String err;
        if (FxpPreset::save(outFile, presetValues, err))
        {
            std::printf("\nGenerated WaveLathe preset: %s  (from note #%d)\n",
                         outFile.getFullPathName().toRawUTF8(), idx + 1);
            std::printf("  Wave: %.2f   Attack: %.3fs   Decay: %.3fs   Sustain: %.2f   Release: %.3fs\n",
                         presetValues.wavePosition, presetValues.attack, presetValues.decay,
                         presetValues.sustain, presetValues.release);
            std::printf("  Filter Cutoff: %.0f Hz   Resonance: %.2f\n",
                         presetValues.filterCutoffHz, presetValues.filterResonance);
            std::printf("  Unison Voices: %.0f   Detune: %.1f cents   Width: %.2f\n",
                         presetValues.unisonVoices, presetValues.unisonDetuneCents, presetValues.unisonWidth);
            std::printf("  Drive: %.2f   LFO Rate: %.2f Hz   LFO->Amp: %.2f   LFO->Filter: %.2f\n",
                         presetValues.driveAmount, presetValues.lfoRateHz, presetValues.lfoAmpDepth,
                         presetValues.lfoDepth);
            std::printf("  MOD ModEnv->Wave: %+.2f  ModEnv->Cutoff: %+.2f  (A %.3fs D %.3fs S %.2f R %.3fs)\n",
                         presetValues.modEnvToWave, presetValues.modEnvToCutoff, presetValues.modEnvAttack,
                         presetValues.modEnvDecay, presetValues.modEnvSustain, presetValues.modEnvRelease);
            std::printf("      LFO1->Wave: %.2f   LFO2: %.2f Hz ->Wave %.2f ->Cutoff %.2f\n",
                         presetValues.lfoToWave, presetValues.lfo2RateHz, presetValues.lfo2ToWave,
                         presetValues.lfo2ToCutoff);
            std::printf("  FX  Distortion: %.2f   Delay: %.0f ms fb %.2f mix %.2f   Reverb: size %.2f mix %.2f\n",
                         presetValues.fxDistortion, presetValues.delayTimeMs, presetValues.delayFeedback,
                         presetValues.delayMix, presetValues.reverbSize, presetValues.reverbMix);
            std::printf("  This is a starting-point approximation from the audio - tune Wave Position and\n");
            std::printf("  Resonance by ear, since those can't be reliably inferred from the recording alone.\n");
        }
        else
        {
            std::printf("\nFailed to write preset: %s\n", err.toRawUTF8());
        }
    }

    return 0;
}
