// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MatchTestCommon.h"

// The guards: things about the matcher that must keep being true, cheap enough
// to run after every change. Anything here that starts costing minutes belongs
// in MatchProbe instead - the whole point of the split is that this one stays
// short enough to actually be run.
//
//   MatchTest                 every section
//   MatchTest phrase bank     only sections whose names contain those words
int main(int argc, char* argv[])
{
    collectArguments(argc, argv);

    std::cout << "Match guards - what has to keep being true" << std::endl << std::endl;

    auto tables = std::make_unique<WavetableSet>();

    // ---- The set the search is allowed to move -----------------------------
    if (wanted("searched set"))
    {
        Section section("searched set");
        std::cout << "Searched parameters: " << SoundMatch::numSearchedParameters() << std::endl;

        const char* expected[] = {"chorusRate",     "chorusDepth", "chorusMix", "phaserRate",
                                  "phaserFeedback", "phaserMix",   "eqLowGain", "eqMidGain",
                                  "eqHighGain"};

        for (const auto* name : expected)
            check(SoundMatch::isParameterSearched(name),
                  juce::String(name) + " is something the search can reach");

        // Which stage each one is fitted in. The split is by what can stand in
        // for what, not by where in the signal path a control lives - so the
        // chorus is fitted with the detune it competes with, and the equaliser
        // is held back from the filter it can cancel.
        auto stageOf = [](const char* name)
        {
            for (int i = 0; i < SoundMatch::numSearchedParameters(); ++i)
                if (juce::String(SoundMatch::searchedParameterName(i)) == name)
                    return juce::String(SoundMatch::parameterStage(i));
            return juce::String("missing");
        };

        // The static sound: what it is before anything moves it.
        for (const auto* name : {"filterCutoffHz", "wavePosition", "attack", "driveAmount", "unisonDetuneCents"})
            check(stageOf(name) == "voice", juce::String(name) + " is fitted with the voice");

        // Everything that makes it move, wherever it lives in the signal path.
        for (const auto* name : {"lfoRateHz", "lfoDepth", "modEnvToCutoff",

                                 "modEnvDecay"})
            check(stageOf(name) == "modulation", juce::String(name) + " is fitted with the movement");

        // Space, and the tone control that has the last word.
        for (const auto* name : {"reverbMix", "delayFeedback", "eqLowGain", "chorusMix", "phaserFeedback"})
            check(stageOf(name) == "effect", juce::String(name) + " is fitted with the master chain");

        // Things a single held note cannot show, which would drift to an
        // arbitrary value that is silent while matching and loud when played.
        for (const auto* name : {"glideTimeMs", "monoMode", "legatoMode", "masterGain", "unisonWidth"})
            check(!SoundMatch::isParameterSearched(name),
                  juce::String(name) + " is left alone, being unobservable in one note");
    }

    // ---- How much of the target those parameters account for ---------------
    if (wanted("what they are worth"))
    {
        Section section("what they are worth");
        std::cout << std::endl << "What the new parameters are worth:" << std::endl;

        // On the heap: a renderer owns a whole synthesiser, which is more than
        // a thread stack wants to hold.
        auto renderer = std::make_unique<OfflineRenderer>(*tables, sampleRate);
        juce::AudioBuffer<float> reference;
        renderer->render(targetPatch(), midiNote, duration, reference, holdRatio);

        auto scoreOf = [&](const PresetValues& p)
        {
            return SoundMatch::scorePreset(*tables, reference.getReadPointer(0),
                                            reference.getNumSamples(), sampleRate, midiNote, p,
                                            holdRatio);
        };

        float itself = scoreOf(targetPatch());

        // The same patch with only the newly-searchable parts removed. This is
        // the best the old parameter set could have done on this reference even
        // if it had got everything else exactly right - there was no way to
        // produce the movement or the tonal tilt at all.
        auto stripped = targetPatch();
        stripped.chorusMix = 0.0f;
        stripped.eqLowGain = 0.0f;
        stripped.eqHighGain = 0.0f;
        float withoutThem = scoreOf(stripped);

        std::cout << "  the target against itself:            " << juce::String(itself, 4) << std::endl;
        std::cout << "  the same patch with chorus and tone off: " << juce::String(withoutThem, 4)
                  << std::endl;

        check(itself < 0.001f, "a patch matches itself exactly");
        check(withoutThem > itself + 1.0f,
              "and the chorus and tone settings are a large part of what it sounds like");
    }

    // ---- The search can actually find its way toward them ------------------
    if (wanted("a short search"))
    {
        Section section("a short search");
        std::cout << std::endl << "A short search:" << std::endl;

        // On the heap: a renderer owns a whole synthesiser, which is more than
        // a thread stack wants to hold.
        auto renderer = std::make_unique<OfflineRenderer>(*tables, sampleRate);
        juce::AudioBuffer<float> reference;
        renderer->render(targetPatch(), midiNote, duration, reference, holdRatio);

        // A plain starting point with none of the target's character.
        PresetValues seed;
        seed.name = "Seed";
        seed.filterCutoffHz = 8000.0f;

        SoundMatch::OptimizerSettings settings;
        settings.populationSize = 40;
        settings.generations = 12;
        settings.randomSeed = 7;

        auto result = SoundMatch::optimize(*tables, reference.getReadPointer(0),
                                            reference.getNumSamples(), sampleRate, midiNote, seed,
                                            settings, {}, holdRatio);

        std::cout << "  started at " << juce::String(result.startingDistance, 4) << ", reached "
                  << juce::String(result.bestDistance, 4) << " after " << result.evaluations
                  << " renders" << std::endl;

        check(result.bestDistance < result.startingDistance,
              "the search gets closer than the starting point");
        check(result.evaluations > 0, "and it actually rendered candidates");
    }

    // ---- Does the shipped bank know anything useful? ------------------------
    // Before any searching, every factory preset is played at the reference's
    // pitch and measured. If that ranking is meaningful, the search gets a
    // coherent patch to start from instead of a pile of independently random
    // numbers - and if it is not, this says so.
    if (wanted("factory bank audition"))
    {
        Section section("factory bank audition");
        std::cout << std::endl << "Auditioning the factory bank:" << std::endl;

        auto renderer = std::make_unique<OfflineRenderer>(*tables, sampleRate);
        juce::AudioBuffer<float> reference;
        renderer->render(targetPatch(), midiNote, duration, reference, holdRatio);

        PresetValues estimate;
        estimate.name = "Estimate";
        estimate.filterCutoffHz = 8000.0f;

        auto ranked = SoundMatch::auditionFactoryPresets(*tables, reference.getReadPointer(0),
                                                          reference.getNumSamples(), sampleRate, midiNote,
                                                          &estimate, holdRatio);

        check(ranked.size() == wavelathe::FactoryPresets::all().size() + 1,
              "every shipped preset is auditioned, plus the estimate");

        std::cout << "  closest five:" << std::endl;
        for (size_t i = 0; i < ranked.size() && i < 5; ++i)
            std::cout << "    " << (i + 1) << ": " << ranked[i].name << "  "
                      << juce::String(ranked[i].distance, 2) << std::endl;

        std::cout << "  furthest: " << ranked.back().name << "  "
                  << juce::String(ranked.back().distance, 2) << std::endl;

        // Where the estimate lands among them. It measured the actual
        // recording, so if the bank beats it the bank is telling us something
        // the estimate's handful of features could not.
        size_t estimateRank = ranked.size();
        for (size_t i = 0; i < ranked.size(); ++i)
            if (ranked[i].bankIndex < 0)
                estimateRank = i + 1;

        std::cout << "  the estimate from the recording ranks " << estimateRank << " of "
                  << ranked.size() << std::endl;

        check(ranked.front().distance < ranked.back().distance,
              "the audition separates close patches from distant ones");
        check(ranked.front().distance < 1.0e8f, "and the closest one is not silence");
    }

    // ---- Which part of the engine does not repeat itself? ------------------
    if (wanted("repeatability"))
    {
        Section section("repeatability");
        std::cout << std::endl << "Repeatability, part by part:" << std::endl;

        auto probe = [&](const char* label, const PresetValues& p)
        {
            auto r = std::make_unique<OfflineRenderer>(*tables, sampleRate);
            juce::AudioBuffer<float> b;

            auto sumOf = [&]()
            {
                r->render(p, midiNote, duration, b, holdRatio);
                double s = 0.0;
                for (int i = 0; i < b.getNumSamples(); ++i)
                    s += std::abs((double) b.getSample(0, i));
                return s;
            };

            double first = sumOf();
            double second = sumOf();
            double third = sumOf();

            bool stable = std::abs(first - second) < 1.0e-6 && std::abs(second - third) < 1.0e-6;
            std::cout << "    " << juce::String(label).paddedRight(' ', 24)
                      << (stable ? "repeats" : "DRIFTS")
                      << "   " << juce::String(first, 4) << " / " << juce::String(second, 4) << " / "
                      << juce::String(third, 4) << std::endl;
            return stable;
        };

        PresetValues bare;
        bare.name = "Bare";
        bare.osc1Level = 1.0f;
        bare.filterCutoffHz = 2600.0f;

        probe("plain oscillator", bare);

        auto withUnison = bare; withUnison.unisonVoices = 3.0f; withUnison.unisonDetuneCents = 14.0f;
        probe("+ unison", withUnison);

        auto withLfo = bare; withLfo.lfoRateHz = 1.4f; withLfo.lfoDepth = 0.5f;
        probe("+ lfo to cutoff", withLfo);

        auto withNoise = bare; withNoise.noiseLevel = 0.4f;
        probe("+ noise", withNoise);

        auto withChorus = bare; withChorus.chorusRate = 1.4f; withChorus.chorusDepth = 0.7f; withChorus.chorusMix = 0.75f;
        probe("+ chorus", withChorus);

        auto withPhaser = bare; withPhaser.phaserRate = 1.4f; withPhaser.phaserMix = 0.75f;
        probe("+ phaser", withPhaser);

        auto withDelay = bare; withDelay.delayMix = 0.5f; withDelay.delayFeedback = 0.4f;
        probe("+ delay", withDelay);

        auto withReverb = bare; withReverb.reverbMix = 0.5f; withReverb.reverbSize = 0.6f;
        probe("+ reverb", withReverb);

        auto withEq = bare; withEq.eqLowGain = -7.0f; withEq.eqHighGain = 8.0f;
        probe("+ eq", withEq);

        check(probe("the target patch", targetPatch()) || true, "(reported, not asserted)");
    }

    // ---- Is any of this repeatable? ----------------------------------------
    if (wanted("determinism"))
    {
        Section section("determinism");
        std::cout << std::endl << "Determinism probe:" << std::endl;

        auto target = targetPatch();

        auto renderOnce = [&](OfflineRenderer& r)
        {
            juce::AudioBuffer<float> b;
            r.render(target, midiNote, duration, b, holdRatio);
            double sum = 0.0;
            for (int i = 0; i < b.getNumSamples(); ++i)
                sum += std::abs((double) b.getSample(0, i));
            return sum;
        };

        auto a1 = std::make_unique<OfflineRenderer>(*tables, sampleRate);
        auto a2 = std::make_unique<OfflineRenderer>(*tables, sampleRate);

        double fresh1 = renderOnce(*a1);
        double fresh2 = renderOnce(*a2);
        std::cout << "  two fresh renderers, same patch: " << juce::String(fresh1, 6) << " vs "
                  << juce::String(fresh2, 6) << std::endl;

        double again1 = renderOnce(*a1);
        std::cout << "  same renderer, rendered twice:   " << juce::String(fresh1, 6) << " vs "
                  << juce::String(again1, 6) << std::endl;

        // Render something else in between, then come back to it. If a renderer
        // carries state forward, this is where it shows: in the search, which
        // worker picks up which candidate varies run to run.
        PresetValues other = targetPatch();
        other.noiseLevel = 0.5f;
        other.filterCutoffHz = 400.0f;
        {
            juce::AudioBuffer<float> junk;
            a1->render(other, midiNote, duration, junk, holdRatio);
        }
        double after = renderOnce(*a1);
        std::cout << "  after rendering something else:  " << juce::String(fresh1, 6) << " vs "
                  << juce::String(after, 6) << std::endl;

        check(std::abs(fresh1 - fresh2) < 1.0e-6, "two renderers agree on the same patch");
        check(std::abs(fresh1 - again1) < 1.0e-6, "a renderer repeats itself");
        check(std::abs(fresh1 - after) < 1.0e-6, "and is not disturbed by what it rendered before");
    }

    // ---- Settings nobody chose ---------------------------------------------
    // Roughly twenty of the forty-nine searched settings do nothing at all for
    // a plain target, because whatever they belong to is switched off - a
    // second oscillator's tuning at zero level, a reverb's size at zero mix.
    // The search cannot hear those, so it leaves them wherever they landed, and
    // they stay silent right up until the user turns the parent up and finds
    // the oscillator seventeen semitones sharp.
    if (wanted("inactive settings"))
    {
        Section section("inactive settings");
        std::cout << std::endl << "Settings the search could not hear:" << std::endl;

        auto renderer = std::make_unique<OfflineRenderer>(*tables, sampleRate);
        juce::AudioBuffer<float> reference;
        auto target = targetPatch();
        renderer->render(target, midiNote, duration, reference, holdRatio);

        auto scoreOf = [&](const PresetValues& p)
        {
            return SoundMatch::scorePreset(*tables, reference.getReadPointer(0),
                                            reference.getNumSamples(), sampleRate, midiNote, p,
                                            holdRatio);
        };

        // A patch with everything switched off but its dependants left at
        // ridiculous values - which is exactly the shape of thing the search
        // hands back.
        PresetValues messy = target;
        messy.osc2Level = 0.0f;   messy.osc2Semitones = 17.0f;  messy.osc2Fine = 74.0f;
        messy.subLevel = 0.0f;    messy.subOctave = 2.0f;
        messy.noiseLevel = 0.0f;  messy.noiseColour = 0.9f;
        messy.reverbMix = 0.0f;   messy.reverbSize = 0.95f;
        messy.phaserMix = 0.0f;   messy.phaserRate = 7.3f;

        float before = scoreOf(messy);

        PresetValues tidied = messy;
        int moved = SoundMatch::tidyInactiveParameters(tidied);

        float after = scoreOf(tidied);

        std::cout << "  settings put back: " << moved << std::endl;
        std::cout << "  distance before " << juce::String(before, 4) << ", after "
                  << juce::String(after, 4) << std::endl;
        std::cout << "  osc2 semitones " << juce::String(messy.osc2Semitones, 0) << " -> "
                  << juce::String(tidied.osc2Semitones, 0) << ",  reverb size "
                  << juce::String(messy.reverbSize, 2) << " -> "
                  << juce::String(tidied.reverbSize, 2) << std::endl;

        check(moved > 0, "settings that do nothing are put back");

        // The whole justification: this cannot change how the patch sounds. If
        // it ever does, something that mattered has just been thrown away.
        check(std::abs(before - after) < 0.001f,
              "and doing so does not change the sound by even a fraction");

        // And it must leave alone anything that IS doing something.
        PresetValues live = target;
        live.osc2Level = 0.6f;
        live.osc2Semitones = 7.0f;

        PresetValues liveTidied = live;
        SoundMatch::tidyInactiveParameters(liveTidied);

        check(std::abs(liveTidied.osc2Semitones - 7.0f) < 0.01f,
              "while a setting that is actually in use is left alone");
    }

    // ---- A tune, not a single note -----------------------------------------
    // The thing a user is most likely to have lying around is a phrase, not an
    // isolated note. It has to cope with that: find where the notes are, and
    // match one of them rather than averaging the lot into mush.
    if (wanted("a four-note phrase"))
    {
        Section section("a four-note phrase");
        std::cout << std::endl << "A four-note phrase:" << std::endl;

        auto renderer = std::make_unique<OfflineRenderer>(*tables, sampleRate);

        PresetValues voice;
        voice.name = "Tune";
        voice.attack = 0.01f;
        voice.decay = 0.3f;
        voice.sustain = 0.6f;
        voice.release = 0.15f;
        voice.filterCutoffHz = 3000.0f;

        // Four notes of different pitches and lengths, with silence between
        // them - the third is deliberately the longest, so which one gets
        // picked is something the test can actually check.
        struct Note { int midi; double seconds; };
        const Note tune[] = {{48, 0.5}, {64, 0.4}, {67, 1.1}, {84, 0.6}};

        double gap = 0.25;
        int totalSamples = 0;
        for (const auto& n : tune)
            totalSamples += (int) ((n.seconds + gap) * sampleRate);

        juce::AudioBuffer<float> phrase(1, totalSamples);
        phrase.clear();

        int writePosition = 0;
        for (const auto& n : tune)
        {
            juce::AudioBuffer<float> note;
            renderer->render(voice, n.midi, n.seconds, note, 0.75f);
            phrase.copyFrom(0, writePosition, note, 0, 0, note.getNumSamples());
            writePosition += (int) ((n.seconds + gap) * sampleRate);
        }

        auto wavFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                           .getChildFile("WaveLatheMatchTest")
                           .getChildFile("phrase.wav");
        wavFile.getParentDirectory().createDirectory();
        wavFile.deleteFile();

        {
            juce::WavAudioFormat format;
            std::unique_ptr<juce::OutputStream> stream(wavFile.createOutputStream());

            const auto options = juce::AudioFormatWriterOptions{}
                                     .withSampleRate(sampleRate)
                                     .withNumChannels(1)
                                     .withBitsPerSample(16);

            auto writer = format.createWriterFor(stream, options);

            if (writer != nullptr)
                writer->writeFromAudioSampleBuffer(phrase, 0, phrase.getNumSamples());
        }

        check(wavFile.existsAsFile(), "the phrase was written out to load back");

        // ---- It finds the notes --------------------------------------------
        SampleMatch::MatchContext context;
        juce::String error;
        bool analysed = SampleMatch::analyzeFile(wavFile, -1, context, error);

        check(analysed, "a multi-note file loads and analyses: " + error);

        if (analysed)
        {
            std::cout << "  found " << (int) context.reports.size() << " notes:" << std::endl;
            for (size_t i = 0; i < context.reports.size(); ++i)
            {
                const auto& rep = context.reports[i];
                std::cout << "    " << (i + 1) << ": " << SampleMatch::noteName(rep.fundamentalHz)
                          << "  " << juce::String(rep.durationSec, 2) << "s  " << rep.clarityVerdict()
                          << std::endl;
            }

            check((int) context.reports.size() == 4, "all four notes are found separately");

            // Every note's pitch, not just the one that gets used: a note heard an
            // octave out would be rendered at the wrong pitch during matching,
            // and nothing about the resulting patch would fit.
            const int rendered[] = {48, 64, 67, 84};
            for (size_t i = 0; i < context.reports.size() && i < 4; ++i)
            {
                int heard = SampleMatch::midiNoteForFrequency(context.reports[i].fundamentalHz);
                std::cout << "    note " << (i + 1) << ": rendered " << rendered[i] << ", heard "
                          << heard << " (" << juce::String(context.reports[i].fundamentalHz, 1)
                          << " Hz)" << std::endl;
                check(std::abs(heard - rendered[i]) <= 1,
                      "note " + juce::String((int) i + 1) + " is heard at the pitch it was played");
            }

            // With no note asked for, the longest is taken - it gives the
            // search the most to work with.
            check(context.chosenIndex == 2, "the longest note is the one chosen by default");

            if (context.chosenIndex < (int) context.reports.size())
            {
                int chosenMidi =
                    SampleMatch::midiNoteForFrequency(context.reports[(size_t) context.chosenIndex].fundamentalHz);
                std::cout << "  chose note " << (context.chosenIndex + 1) << ", heard as MIDI "
                          << chosenMidi << " (rendered as 67)" << std::endl;
                check(std::abs(chosenMidi - 67) <= 1, "and its pitch is read correctly");
            }
        }

        // ---- Any one of them can be asked for ------------------------------
        SampleMatch::MatchContext second;
        if (SampleMatch::analyzeFile(wavFile, 1, second, error))
        {
            int midi = SampleMatch::midiNoteForFrequency(second.reports[(size_t) second.chosenIndex].fundamentalHz);
            std::cout << "  asking for note 2 gives MIDI " << midi << " (rendered as 64)" << std::endl;
            check(second.chosenIndex == 1, "a particular note can be asked for by index");
            check(std::abs(midi - 64) <= 1, "and that note is the one analysed");
        }
        else
        {
            check(false, "asking for one note of the phrase: " + error);
        }

        // ---- And a preset comes out of it ----------------------------------
        PresetValues matched;
        std::vector<SampleMatch::SegmentReport> allReports;
        bool ok = SampleMatch::matchFile(wavFile, "Phrase", -1, matched, error, &allReports);

        check(ok, "a preset is estimated from the phrase: " + error);
        check(allReports.size() == 4, "and every note in it is reported back to the caller");

        if (ok)
            std::cout << "  matched preset: cutoff " << juce::String(matched.filterCutoffHz, 0)
                      << " Hz, attack " << juce::String(matched.attack, 3) << "s" << std::endl;

        // ---- The path Quick Match takes on a real file ---------------------
        // File in, one note chosen out of it, and the whole bank auditioned
        // against that note's window - the same three steps the menu action
        // runs, so a change that breaks it is caught here rather than by
        // clicking.
        SampleMatch::MatchContext qmContext;
        if (SampleMatch::analyzeFile(wavFile, -1, qmContext, error) && qmContext.chosenIndex >= 0)
        {
            const auto& seg = qmContext.segments[(size_t) qmContext.chosenIndex];
            const auto& rep = qmContext.reports[(size_t) qmContext.chosenIndex];
            int qmNote = SampleMatch::midiNoteForFrequency(rep.fundamentalHz);

            float qmHold = 0.85f;
            int qmLength = SampleMatch::windowWithTail(qmContext, qmContext.chosenIndex, 0.8, qmHold);

            PresetValues qmEstimate;
            SampleMatch::matchFile(wavFile, "Phrase", -1, qmEstimate, error);

            auto ranked = SoundMatch::auditionFactoryPresets(
                *tables, qmContext.mono.getReadPointer(0) + seg.startSample, qmLength,
                qmContext.sampleRate, qmNote, &qmEstimate, qmHold);

            check(!ranked.empty(), "the quick-match path returns a ranking for a real file");

            if (!ranked.empty())
            {
                size_t estimateRank = ranked.size();
                for (size_t i = 0; i < ranked.size(); ++i)
                    if (ranked[i].bankIndex < 0)
                        estimateRank = i + 1;

                std::cout << "  quick match picks " << ranked.front().name << "  ("
                          << juce::String(ranked.front().distance, 1) << "); the estimate ranks "
                          << estimateRank << " of " << ranked.size() << std::endl;

                check(ranked.front().distance <= ranked[(size_t) (estimateRank - 1)].distance,
                      "and never picks something worse than the estimate it started with");
            }
        }
        else
        {
            check(false, "the quick-match path could not analyse the phrase: " + error);
        }

        wavFile.getParentDirectory().deleteRecursively();
    }

    return finish("match");
}
