// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "PresetBrowser.h"

#include <iostream>

using namespace wavelathe;

namespace
{
int failures = 0;

void check(bool condition, const juce::String& what)
{
    if (!condition)
    {
        std::cout << "  FAIL: " << what << std::endl;
        ++failures;
    }
}

constexpr double testSampleRate = 44100.0;
constexpr int testBlockSize = 512;

// On the heap, deliberately. A WaveLatheProcessor is a large object - it owns
// the voices, the built-in wavetables and every effect - and MSVC reserves a
// function's WHOLE frame on entry, summing sibling scopes rather than reusing
// them. Seven of these as locals in main overflows the default 1MB stack
// before the first line of output, and the fix is not a bigger stack: the
// product never stacks them like this, so neither should the test.
//
// A synth, so no inputs.
std::unique_ptr<WaveLatheProcessor> makeProcessor()
{
    auto processor = std::make_unique<WaveLatheProcessor>();
    processor->setPlayConfigDetails(0, 2, testSampleRate, testBlockSize);
    processor->prepareToPlay(testSampleRate, testBlockSize);
    return processor;
}

void runBlocks(WaveLatheProcessor& processor, int count)
{
    juce::AudioBuffer<float> buffer(2, testBlockSize);
    juce::MidiBuffer midi;

    for (int i = 0; i < count; ++i)
    {
        buffer.clear();
        midi.clear();
        processor.processBlock(buffer, midi);
    }
}

// Which ids the sequencer thinks something other than a lane is driving.
juce::Array<int> heldIds(WaveLatheProcessor& processor)
{
    juce::Array<int> held;

    for (int id = 0; id < paramreg::maxParameters; ++id)
        if (processor.getSequencer().isLaneTouched(id))
            held.add(id);

    return held;
}

juce::String listOf(const juce::Array<int>& ids)
{
    if (ids.isEmpty())
        return "nothing";

    juce::StringArray parts;
    for (int id : ids)
        parts.add(juce::String(id));

    return parts.joinIntoString(", ");
}

// The whole point of the suite: a host writing one parameter must put the hold
// on that parameter and on no other. Returns the set actually held so a failure
// can say which id it landed on instead of just "not the one asked for".
void expectOnlyHeld(WaveLatheProcessor& processor, int wroteId, const juce::String& what)
{
    processor.noteHostWroteParameter(wroteId);
    runBlocks(processor, 1);

    const auto held = heldIds(processor);

    check(held.size() == 1 && held[0] == wroteId,
          what + " - expected only id " + juce::String(wroteId) + ", held " + listOf(held));
}
} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    std::cout << "Processor" << std::endl << std::endl;
    std::cout << "  cap is " << paramreg::maxParameters << ", registry has "
              << paramreg::count() << " parameters" << std::endl
              << std::endl;

    // ---- It can be built at all ---------------------------------------------
    // Nothing in this project had ever constructed the main processor. Every
    // suite passed without compiling PluginProcessor.cpp, so processBlock, the
    // host-write drain and the controller hold countdown were outside the net
    // entirely. This is the floor of that: it exists, it prepares, it runs.
    {
        auto owned = makeProcessor();
        auto& processor = *owned;

        check(processor.getTotalNumOutputChannels() == 2, "it is a stereo instrument");
        check(processor.acceptsMidi(), "and it takes MIDI");

        runBlocks(processor, 4);

        std::cout << "  constructs, prepares and processes blocks" << std::endl;
    }

    // ---- A host write puts a hold on that parameter -------------------------
    // noteHostWroteParameter sets a bit from whichever thread the DAW chose;
    // processBlock drains it into a countdown and tells the sequencer the
    // parameter is being driven. That chain is what stops an automation lane
    // fighting the host over the same dial, and until now nothing tested it.
    {
        auto owned = makeProcessor();
        auto& processor = *owned;

        check(heldIds(processor).isEmpty(), "nothing is held before anything is written");

        expectOnlyHeld(processor, 0, "the first registered parameter");

        std::cout << "  a host write marks its own parameter as driven" << std::endl;
    }

    // ---- THE ONE THAT MATTERS ----------------------------------------------
    //
    // The drain used to read the mask as (hostWrites >> id) & 1ull, a single
    // word right-shifted by the id. Past 63 that is undefined behaviour, and on
    // x86 it wraps: a host writing parameter 64 would put the hold on parameter
    // 0 - the Wave dial - and parameter 64 would never be held at all.
    //
    // Nothing about that is visible. No warning, no crash. It presents as a
    // dial that ignores its own automation lane, in a different section of the
    // synth from the one being automated.
    //
    // The ids come from the cap rather than being written down, so this became
    // a real test of the boundary the moment maxParameters passed 64, without
    // anyone having to remember to change it.
    {
        const int topId = paramreg::maxParameters - 1;

        auto owned = makeProcessor();
        auto& processor = *owned;

        expectOnlyHeld(processor, topId, "the highest id the cap allows");

        // The id it would have aliased onto under the old arithmetic:
        // topId % 64 is where a single-word shift would have put it.
        const int wouldHaveAliasedTo = topId & 63;

        if (wouldHaveAliasedTo != topId)
        {
            check(!processor.getSequencer().isLaneTouched(wouldHaveAliasedTo),
                  "and id " + juce::String(wouldHaveAliasedTo)
                      + " is untouched - the shift did not wrap");

            std::cout << "  id " << topId << " holds itself and not id " << wouldHaveAliasedTo
                      << std::endl;
        }
        else
        {
            // One word, so the top id IS its own low six bits and there is no
            // second id to alias onto. Says so rather than printing a
            // reassuring line about a case it did not test.
            std::cout << "  id " << topId << " holds itself (no alias to check at this cap)"
                      << std::endl;
        }
    }

    // ---- The first id in the second word ------------------------------------
    // Separate from the case above because 64 is the one that aliases onto id
    // 0, and id 0 is the Wave dial. Skipped rather than faked while the cap is
    // one word, so the run says plainly which case it covered.
    {
        if (ParameterMask::words > 1)
        {
            auto owned = makeProcessor();
            auto& processor = *owned;

            expectOnlyHeld(processor, 64, "the first id in the second word");

            check(!processor.getSequencer().isLaneTouched(0),
                  "and the Wave dial at id 0 is untouched");

            std::cout << "  id 64 holds itself and not the Wave dial" << std::endl;
        }
        else
        {
            std::cout << "  (id 64 not testable: the cap is one word wide)" << std::endl;
        }
    }

    // ---- Several at once ----------------------------------------------------
    // A host restoring a session writes every parameter it has. If the drain
    // loses whole words, that arrives as most of the panel being held and some
    // of it not.
    {
        auto owned = makeProcessor();
        auto& processor = *owned;

        juce::Array<int> wrote;
        for (int id = 0; id < paramreg::maxParameters; id += 7)
        {
            processor.noteHostWroteParameter(id);
            wrote.add(id);
        }

        runBlocks(processor, 1);

        const auto held = heldIds(processor);
        check(held == wrote, "every id written is held - got " + listOf(held));

        std::cout << "  " << wrote.size() << " ids written together all survive the drain"
                  << std::endl;
    }

    // ---- The hold runs out --------------------------------------------------
    // A bit set without a countdown to run it down would never clear, and the
    // lane would be locked out of that dial for the rest of the session.
    {
        auto owned = makeProcessor();
        auto& processor = *owned;

        processor.noteHostWroteParameter(0);
        runBlocks(processor, 1);
        check(processor.getSequencer().isLaneTouched(0), "held immediately after the write");

        // 0.9 seconds of hold, so a little over that in blocks.
        const int blocks = (int) (1.1 * testSampleRate / testBlockSize) + 1;
        runBlocks(processor, blocks);

        check(!processor.getSequencer().isLaneTouched(0), "and released once the hold expires");

        std::cout << "  the hold expires instead of latching forever" << std::endl;
    }

    // ---- Out of range -------------------------------------------------------
    {
        auto owned = makeProcessor();
        auto& processor = *owned;

        processor.noteHostWroteParameter(-1);
        processor.noteHostWroteParameter(paramreg::maxParameters);
        processor.noteHostWroteParameter(100000);

        runBlocks(processor, 1);

        check(heldIds(processor).isEmpty(), "ids outside the range hold nothing");

        std::cout << "  out-of-range writes are ignored, not folded back in" << std::endl;
    }

    // ---- A pattern drawn on the grid reaches the output --------------------
    //
    // Everything either side of this seam is already tested: SequencerTest
    // proves a nudged hit is reported at the right sample, DrumVoiceTest
    // proves a triggered voice makes the right noise. Neither notices if the
    // processor never joins them up, which is a whole feature failing silently
    // with two green suites either side of it.
    {
        auto silent = makeProcessor();
        auto& silentParams = silent->getParameters();
        silentParams.seqPlaying.store(1.0f);
        silentParams.seqLength.store(4.0f);

        juce::AudioBuffer<float> quiet(2, testBlockSize * 8);
        juce::MidiBuffer noMidi;
        quiet.clear();
        silent->processBlock(quiet, noMidi);

        const auto floorLevel = quiet.getMagnitude(0, 0, quiet.getNumSamples());

        auto drumming = makeProcessor();
        auto& drumParams = drumming->getParameters();
        drumParams.seqPlaying.store(1.0f);
        drumParams.seqLength.store(4.0f);

        auto& sequencer = drumming->getSequencer();
        sequencer.setDrumCellActive(0, 0, true);   // a kick on the downbeat

        juce::AudioBuffer<float> beat(2, testBlockSize * 8);
        juce::MidiBuffer alsoNoMidi;
        beat.clear();
        drumming->processBlock(beat, alsoNoMidi);

        const auto beatLevel = beat.getMagnitude(0, 0, beat.getNumSamples());

        check(beatLevel > 0.01f, "a kick drawn on the grid is audible at the output");
        check(beatLevel > floorLevel * 10.0f, "and an empty pattern is not");

        std::cout << "  drums through processBlock: " << juce::String(beatLevel, 4)
                  << " against " << juce::String(floorLevel, 4) << " with nothing drawn" << std::endl;
    }

    // ---- Clicking a voice's name plays it, with the transport stopped ------
    //
    // The audition crosses threads: the editor sets a bit, the audio thread
    // drains it. Worth a test because the failure is not a crash - it is a
    // click that does nothing, which reads as a dead control.
    {
        auto processor = makeProcessor();
        processor->getParameters().seqPlaying.store(0.0f);

        juce::AudioBuffer<float> before(2, testBlockSize);
        juce::MidiBuffer midi;
        before.clear();
        processor->processBlock(before, midi);
        check(before.getMagnitude(0, 0, testBlockSize) < 0.001f, "a stopped processor is quiet");

        processor->auditionDrumVoice(1);   // the snare

        juce::AudioBuffer<float> after(2, testBlockSize);
        after.clear();
        processor->processBlock(after, midi);

        check(after.getMagnitude(0, 0, testBlockSize) > 0.01f,
              "and clicking a voice's name plays it even so");

        // Taken and cleared, so one click is one hit rather than a voice that
        // retriggers on every block from now on.
        juce::AudioBuffer<float> later(2, testBlockSize);
        later.clear();
        processor->processBlock(later, midi);

        check(later.getMagnitude(0, 0, testBlockSize) < after.getMagnitude(0, 0, testBlockSize),
              "one click is one hit, not a voice left retriggering");
    }

    // ---- A registered drum dial reaches the audio --------------------------
    //
    // The chain under test is four links long: a registry id, the flat array
    // in SynthParameters it names, the copy into drums::KitParameters, and the
    // voice that reads it. Each link is plausible on its own and the whole
    // thing is untestable anywhere but here.
    //
    // Level, because it is the one control whose failure is unambiguous - a
    // wrong Tune sounds like a different drum, and a wrong Level is silence.
    {
        const auto kickLevel = paramreg::idForName("Kick Level");
        check(kickLevel >= 0, "the drum mixer is in the registry by name");

        const auto strike = [](WaveLatheProcessor& processor)
        {
            processor.auditionDrumVoice(0);

            juce::AudioBuffer<float> buffer(2, testBlockSize * 4);
            juce::MidiBuffer midi;
            buffer.clear();
            processor.processBlock(buffer, midi);
            return buffer.getMagnitude(0, 0, buffer.getNumSamples());
        };

        auto loud = makeProcessor();
        paramreg::writeNormalised(loud->getParameters(), kickLevel, 1.0f);
        const auto loudLevel = strike(*loud);

        auto quiet = makeProcessor();
        paramreg::writeNormalised(quiet->getParameters(), kickLevel, 0.0f);
        const auto quietLevel = strike(*quiet);

        check(loudLevel > 0.05f, "a kick at full Level is audible");
        check(quietLevel == 0.0f, "and at zero Level is exactly silent, not merely quiet");

        // Pan, because it is the control whose units the processor converts:
        // the panel holds -1 to 1 and the kit holds 0 to 1, and getting that
        // backwards would put every voice on the wrong side.
        //
        // Written into the storage array rather than through a registry id,
        // because Pan no longer has one. It is still a control, still saved
        // and still on the panel - it simply lost the argument over which
        // four of six the host gets to see, and the conversion behind it is
        // exactly as able to be wrong as it was before.
        const auto kickPan = (size_t) project::drumControlIndex(0, project::DrumControl::pan);

        auto left = makeProcessor();
        left->getParameters().drumControls[kickPan].store(-1.0f);   // fully left
        left->auditionDrumVoice(0);

        juce::AudioBuffer<float> panned(2, testBlockSize * 4);
        juce::MidiBuffer midi;
        panned.clear();
        left->processBlock(panned, midi);

        check(panned.getMagnitude(0, 0, panned.getNumSamples()) > 0.05f, "panned left, the kick is in the left channel");
        check(panned.getMagnitude(1, 0, panned.getNumSamples()) < 0.001f, "and not in the right");

        std::cout << "  Kick Level is id " << kickLevel << " of " << paramreg::count()
                  << ", and drives the audio: " << juce::String(loudLevel, 4)
                  << " against " << juce::String(quietLevel, 4) << std::endl;
    }

    // ---- A chain slot reaches the audio ------------------------------------
    //
    // The same four-link chain as Level, with three more on the end: the
    // selector has to survive being rounded into the kit, the kit has to run
    // the voice through that unit, and the Amount has to mix the result back.
    //
    // Measured PAST the end of the voice, and where that is took measuring.
    // A kick's decay is 0.42 s and a voice is cut after six of them, so it is
    // still sounding two and a half seconds later - the first draft of this
    // asked whether it had stopped after one and was simply wrong about the
    // instrument. Past the cut the dry path is not quiet but exactly zero, so
    // anything at all in this window arrived through the chain.
    {
        using DC = project::DrumControl;
        using FX = project::DrumFxControl;

        const auto slot = (size_t) project::drumControlIndex(0, DC::send1);
        const auto amount = (size_t) project::drumControlIndex(0, DC::send1Amount);

        const int samples = (int) (testSampleRate * 4.0);
        const int windowStart = (int) (testSampleRate * 2.8);

        const auto strikeAndMeasureTail = [samples, windowStart](WaveLatheProcessor& processor)
        {
            processor.auditionDrumVoice(0);

            juce::AudioBuffer<float> buffer(2, samples);
            juce::MidiBuffer midi;
            buffer.clear();
            processor.processBlock(buffer, midi);

            return buffer.getMagnitude(windowStart, samples - windowStart);
        };

        auto dry = makeProcessor();
        const auto dryTail = strikeAndMeasureTail(*dry);

        auto wet = makeProcessor();
        wet->getParameters().drumControls[slot].store(1.0f);     // the reverb
        wet->getParameters().drumControls[amount].store(1.0f);
        wet->getParameters().drumFx[(size_t) FX::reverbSize].store(1.0f);
        wet->getParameters().drumFx[(size_t) FX::reverbMix].store(1.0f);
        const auto wetTail = strikeAndMeasureTail(*wet);

        check(dryTail == 0.0f, "past its own life a kick is exactly silent, not merely quiet");
        check(wetTail > 0.001f, "run through the reverb, it is still sounding");

        // The selector has to be the selector. A kit that ran whichever unit
        // it felt like would pass the check above without ever reading the
        // number - so the same slot pointed at the distortion, which has no
        // tail of its own, must produce nothing here.
        auto distorted = makeProcessor();
        distorted->getParameters().drumControls[slot].store(2.0f);   // the distortion
        distorted->getParameters().drumControls[amount].store(1.0f);
        distorted->getParameters().drumFx[(size_t) FX::reverbSize].store(1.0f);
        distorted->getParameters().drumFx[(size_t) FX::reverbMix].store(1.0f);
        const auto distortedTail = strikeAndMeasureTail(*distorted);

        check(distortedTail < wetTail * 0.1f,
              "and pointed at the distortion instead it is not - the number picks the unit");

        // A unit selected with the Amount left at zero must be exactly as
        // absent as no unit at all. This is the half the new shape could
        // plausibly get wrong: the selector says "reverb" and the only thing
        // keeping it out of the path is that the mix is zero.
        auto selected = makeProcessor();
        selected->getParameters().drumControls[slot].store(1.0f);
        selected->getParameters().drumControls[amount].store(0.0f);
        selected->getParameters().drumFx[(size_t) FX::reverbSize].store(1.0f);
        selected->getParameters().drumFx[(size_t) FX::reverbMix].store(1.0f);
        check(strikeAndMeasureTail(*selected) == 0.0f,
              "and an Amount of zero is exactly dry, whatever is selected beside it");

        std::cout << "  FX 1 at 2.8 s: " << juce::String(wetTail, 4) << " through the reverb, "
                  << juce::String(distortedTail, 4) << " through the distortion, "
                  << juce::String(dryTail, 4) << " dry" << std::endl;
    }

    // ---- The host is told about the tail the drums add ---------------------
    {
        auto processor = makeProcessor();
        const auto bare = processor->getTailLengthSeconds();

        processor->getSequencer().setDrumCellActive(10, 0, true);   // a crash
        const auto withCrash = processor->getTailLengthSeconds();

        check(withCrash > bare, "a crash in the pattern lengthens the reported tail");
        check(withCrash > 5.0, "by enough for a crash to actually finish");

        processor->getSequencer().clearDrumPattern();
        check(processor->getTailLengthSeconds() == bare,
              "and a project with no drums pays nothing for them");

        std::cout << "  tail: " << juce::String(bare, 2) << " s bare, "
                  << juce::String(withCrash, 2) << " s with a crash on the grid" << std::endl;
    }

    // ---- The Master page's bus catches the drums ----------------------------
    //
    // The bus is the one place synth and drums pass through TOGETHER, which is
    // the whole reason there is a second copy of the chain - and whether it
    // actually sits after the drums is a question about processBlock's order
    // that nothing but a real processor can answer. A bus placed where the
    // synth's own effects are would pass every test of the chain and leave
    // the kit untouched.
    //
    // So: one kick through a unity bus, one through -12 dB of bus gain. The
    // kit goes through nothing of the synth's on its way, so if the bus did
    // not catch it the two would be identical.
    {
        const auto strike = [](WaveLatheProcessor& processor)
        {
            processor.auditionDrumVoice(0);

            juce::AudioBuffer<float> buffer(2, testBlockSize * 4);
            juce::MidiBuffer midi;
            buffer.clear();
            processor.processBlock(buffer, midi);
            return buffer.getMagnitude(0, 0, buffer.getNumSamples());
        };

        auto unity = makeProcessor();
        const auto unityLevel = strike(*unity);

        auto lowered = makeProcessor();
        lowered->getParameters().busFx[(size_t) project::BusFxControl::outputGain].store(-12.0f);
        const auto loweredLevel = strike(*lowered);

        const auto ratio = unityLevel > 0.0f ? loweredLevel / unityLevel : 0.0f;

        std::cout << "  a kick through -12 dB of bus gain: " << juce::String(ratio, 4)
                  << " of its level through a unity bus" << std::endl;

        check(unityLevel > 0.05f, "the kick is heard through an untouched bus");
        check(std::abs(ratio - 0.2512f) < 0.01f, "and the bus gain takes the kit down by what it says");

        // And the host hears about the bus's tail. A drum pattern is left out
        // so the only thing that can move the number is the bus.
        auto roomy = makeProcessor();
        const auto bare = roomy->getTailLengthSeconds();

        roomy->getParameters().busFx[(size_t) project::BusFxControl::reverbMix].store(0.4f);
        roomy->getParameters().busFx[(size_t) project::BusFxControl::reverbSize].store(1.0f);
        const auto withRoom = roomy->getTailLengthSeconds();

        std::cout << "  tail: " << juce::String(bare, 2) << " s with a dry bus, "
                  << juce::String(withRoom, 2) << " s with a large bus reverb" << std::endl;

        check(withRoom >= bare + 4.0, "a bus reverb is added to the tail the host is told about");
    }

    // ---- Auditioning, with and without an accent ----------------------------
    //
    // The struck slots and the accented ones share ONE atomic word - the low
    // bits for hits, the high bits for accents - so that a hit arriving between
    // two exchanges cannot have its accent read against the next hit instead of
    // its own. Which means the two halves can be got the wrong way round, and
    // the failure would be an accent on the wrong drum.
    {
        std::cout << std::endl << "Auditioning with an accent" << std::endl;

        auto owned = makeProcessor();
        auto* processor = owned.get();

        constexpr int kick = 0;
        constexpr int crash = 10;

        juce::AudioBuffer<float> buffer(2, testBlockSize);
        juce::MidiBuffer midi;

        const auto peakOfBlock = [&]
        {
            buffer.clear();
            processor->processBlock(buffer, midi);

            float peak = 0.0f;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    peak = juce::jmax(peak, std::abs(buffer.getSample(ch, i)));

            return peak;
        };

        processor->auditionDrumVoice(kick, false);
        const auto plain = peakOfBlock();

        // Let the first hit finish before measuring the second, so the two
        // windows are of one drum each.
        for (int i = 0; i < 400; ++i)
            peakOfBlock();

        processor->auditionDrumVoice(kick, true);
        const auto accented = peakOfBlock();

        check(plain > 0.0f, "an audition makes a sound");
        check(accented > plain * 1.1f,
              "and an accented one is louder: " + juce::String(accented, 4) + " against "
                  + juce::String(plain, 4));

        for (int i = 0; i < 400; ++i)
            peakOfBlock();

        // A high slot accented, to prove the two halves of the word are not
        // crossed. Slot 10's accent bit is bit 26; read as a hit it would be
        // a slot that does not exist, and read against slot 0 it would accent
        // the kick instead.
        processor->auditionDrumVoice(crash, true);
        const auto crashAccented = peakOfBlock();

        for (int i = 0; i < 600; ++i)
            peakOfBlock();

        processor->auditionDrumVoice(crash, false);
        const auto crashPlain = peakOfBlock();

        check(crashAccented > crashPlain * 1.1f,
              "and the accent reaches a high slot too, so the two halves of the word are not "
              "crossed");
    }

    // ---- Undo puts the sample slots back ------------------------------------
    //
    // Found by hand, not by this: loading a sample onto the Kick and pressing
    // Ctrl+Z printed "Undid kick sample" on the display and left the sample
    // sitting on the pad. Two paths put a whole captured state back - opening
    // a project, and undo/redo/crash recovery - and only the first restored
    // the slots. This is the second one.
    {
        std::cout << std::endl << "Undo and the sample slots" << std::endl;

        auto owned = makeProcessor();
        auto* processor = owned.get();

        constexpr int kick = 0;
        constexpr int snare = 1;

        const auto makeSample = [](const juce::String& name, float value)
        {
            auto sample = std::make_shared<drums::Sample>();
            sample->data.assign(2000, value);
            sample->sourceRate = testSampleRate;
            sample->name = name;
            return drums::SampleRef(sample);
        };

        check(processor->getDrumSample(kick) == nullptr, "the kick starts on its own circuit");

        // A slot that was already filled before the undo point, so the test
        // covers "put the old sample back" and not only "clear it".
        processor->setDrumSample(snare, makeSample("Before", 0.25f));

        processor->recordUndoPoint("Kick sample");
        processor->setDrumSample(kick, makeSample("After", 0.5f));
        processor->setDrumSample(snare, makeSample("Replaced", 0.75f));

        check(processor->getDrumSampleName(kick) == "After", "loading one puts it on the pad");

        PresetValues restored;
        check(processor->undo(restored), "and there is something to undo");

        // What the editor does with what undo hands back. Calling it directly
        // is the point: the bug was that the editor's path never did.
        processor->restoreDrumSamples(restored);

        check(processor->getDrumSample(kick) == nullptr,
              "undoing a sample load takes it back off the pad");
        check(processor->getDrumSampleName(snare) == "Before",
              "and a slot that already held one gets that one back, not nothing");

        PresetValues redone;
        check(processor->redo(redone), "and it can be redone");
        processor->restoreDrumSamples(redone);

        check(processor->getDrumSampleName(kick) == "After",
              "which brings the sample back again");
        check(processor->getDrumSampleName(snare) == "Replaced",
              "along with everything else the state was holding");
    }

    // ---- Backing out of the preset browser puts the patch back once --------
    // Cancel put the patch back and closed the window; closing the window
    // destroyed the browser, and its destructor - there for the title bar's
    // close box, which is also a cancel - put it back a second time. The sound
    // survived that, but "Browsing cancelled" was printed twice, and anything
    // that counted on a restore meaning something would have been told twice.
    //
    // Pressed through the buttons' own onClick, as a click would, so this is
    // the path a person takes. Nothing here opens a window.
    {
        const auto buttonCalled = [](juce::Component& parent, const juce::String& text) -> juce::Button*
        {
            for (auto* child : parent.getChildren())
                if (auto* button = dynamic_cast<juce::Button*>(child))
                    if (button->getButtonText() == text)
                        return button;

            return nullptr;
        };

        int restores = 0;
        int commits = 0;

        const auto makeBrowser = [&restores, &commits]
        {
            return std::make_unique<PresetBrowser>([](const FactoryPresets::Entry&) {},
                                                   [](const juce::File&) {},
                                                   [&restores] { ++restores; },
                                                   [&commits] { ++commits; });
        };

        const auto press = [&buttonCalled](PresetBrowser& browser, const juce::String& text)
        {
            auto* button = buttonCalled(browser, text);
            check(button != nullptr && button->onClick != nullptr, "the browser has a " + text + " button");

            if (button != nullptr && button->onClick != nullptr)
                button->onClick();
        };

        {
            auto browser = makeBrowser();
            press(*browser, "Cancel");
        }
        check(restores == 1 && commits == 0,
              "Cancel puts the patch back once - it went back " + juce::String(restores) + " times");

        restores = commits = 0;
        {
            auto browser = makeBrowser();
            press(*browser, "Cancel");
            press(*browser, "Cancel");
        }
        check(restores == 1, "and a second press before the window goes does not do it again - it went back "
                                 + juce::String(restores) + " times");

        restores = commits = 0;
        {
            auto browser = makeBrowser();
            press(*browser, "Load");
        }
        check(commits == 1 && restores == 0, "Load keeps what is playing and puts nothing back");

        restores = commits = 0;
        {
            // The title bar's close box: no button at all, the window just goes.
            auto browser = makeBrowser();
        }
        check(restores == 1 && commits == 0, "closing the window any other way is still a cancel");

        std::cout << "  backing out of the preset browser puts the patch back once" << std::endl;
    }

    // ---- Saving a project says what went into it ----------------------------
    // The line under "Saved project" counted the synth's notes and lanes and
    // nothing else, so a project that was only a beat was reported as "Synth
    // settings only - the sequencer is empty". Wrong twice over: a project is
    // every page, and the beat was in it.
    {
        const PresetValues empty;
        const auto nothing = WaveLatheEditor::describeSavedProject(empty);
        check(nothing == "Every page's settings - the pattern is empty.",
              "an empty pattern is still every page's settings - it said: " + nothing);

        // A saved pattern always has its steps, switched off; only the kick's
        // row has anything in it.
        PresetValues beat;
        beat.sequencer.steps.resize(16);

        SequencerState::DrumVoiceData kickRow;
        kickRow.voice = 0;
        kickRow.cells.resize(16);
        for (size_t i = 0; i < kickRow.cells.size(); i += 4)
            kickRow.cells[i].active = 1;
        beat.sequencer.drumVoices.push_back(kickRow);

        const auto drumsOnly = WaveLatheEditor::describeSavedProject(beat);
        check(drumsOnly == "Every page's settings, with 4 drum hits.",
              "a project that is only a beat says it has one - it said: " + drumsOnly);

        PresetValues song = beat;
        song.sequencer.steps[0].active = 1;
        song.sequencer.lanes.push_back({5, std::vector<float>(16, 0.5f)});
        song.drumSamples[0] = std::make_shared<drums::Sample>();

        const auto everything = WaveLatheEditor::describeSavedProject(song);
        check(everything == "Every page's settings, with 1 note, 4 drum hits, 1 automation lane and 1 drum sample.",
              "and names the rest, one of a thing in the singular - it said: " + everything);

        std::cout << "  saving a project says what went into it, the beat included" << std::endl;
    }

    // ---- A readout asked for no decimals gets none ---------------------------
    // Every dial readout that drops its decimal above ten asked JUCE for zero
    // places, and JUCE reads zero as "the stream's default format". So the
    // Master page's threshold read -10.5827dB, found in a screenshot for the
    // guide; a cutoff above 10k would have read 12.3456k, and the sub wave
    // "sine 37.7953%".
    {
        const auto whole = ui::toFixed(-10.5827, 0);
        check(whole == "-11", "a readout with no decimals is a whole number - it read " + whole);

        const auto kilohertz = ui::toFixed(12345.6 / 1000.0, 0);
        check(kilohertz == "12", "a cutoff above 10k reads in whole kilohertz - it read " + kilohertz);

        const auto tenth = ui::toFixed(-9.87, 1);
        check(tenth == "-9.9", "and one decimal is still exactly one - it read " + tenth);

        std::cout << "  a readout asked for no decimals gets none" << std::endl;
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL PROCESSOR TESTS PASSED" << std::endl;
    else
        std::cout << failures << " PROCESSOR TEST(S) FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
