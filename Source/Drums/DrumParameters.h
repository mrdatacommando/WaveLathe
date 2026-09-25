// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <array>
#include <atomic>

// The drum kit's own parameter block.
//
// Deliberately NOT SynthParameters, for the reason MasteringParameters gives:
// a library that reaches into WaveLathe's parameter struct could only ever be
// used by WaveLathe. Everything the kit needs to know is here, and whatever is
// driving it copies its own dials in.
//
// Namespace is "drums" rather than "wavelathe::drums" for the same reason the
// mastering chain is "mastering". The boundary is the point.
namespace drums
{
// Twelve slots, which is an 808's eleven instruments and one left over.
//
// It was sixteen, on the reasoning that four spare rows could hold the things
// people reach for anyway. The count came down when it was checked against the
// machine it is modelled on: an 808 - and the RD-8 that clones it - has ELEVEN
// instrument channels and gets a whole genre out of them. The four extra rows
// were width and height spent on a grid nobody was filling.
//
// Nothing was lost by it, because which SOUND a slot makes is now a choice
// rather than its position - see numEngines. The four voices that came off the
// kit are still here and can be put on any slot; what went away is four rows
// that were always those four sounds whether you wanted them or not.
constexpr int numVoices = 12;

// How many synthesised drums there are to choose FROM, which is not the same
// number and never has to be.
//
// The 808's own switch is the argument: on the hardware, one channel is Low
// Tom or Low Conga depending on a toggle, because a tom and a conga are not
// worth a channel each. Generalised, that is this - a slot plays whichever of
// these its own engine setting names, or a sample, or nothing.
//
// So the table can grow without costing a row, a strip, four host parameters
// and sixteen cells of grid per entry, which is what adding a sound used to
// cost when a sound WAS a slot.
constexpr int numEngines = 16;

// What a slot's engine setting holds when it is not playing any of them: a
// blank slot, silent until a sample is dropped on it.
//
// One of the twelve starts here. The user asked for "eleven instruments and
// one blank for samples", and a blank slot is the honest way to say that -
// rather than filling it with a sound nobody chose and calling it a default.
constexpr int noEngine = -1;

// What slot N plays on a kit nobody has touched: engine N for the eleven the
// 808 has, and noEngine for the twelfth.
//
// The identity mapping for the first eleven is not laziness - it is what keeps
// every pattern and every automation lane written before slots could choose
// their own sound meaning exactly what it meant. The spec table's order is the
// other half of that promise, and says so.
//
// Here rather than beside the spec table because KitParameters below has to
// call it to lay out a fresh kit, and this header cannot see DrumVoice.h. It
// needs neither - a slot's default is arithmetic on the two constants above.
constexpr int defaultEngineFor(int slot)
{
    return (slot < 0 || slot >= numVoices - 1) ? noEngine : slot;
}

// How many shared effect units the sends can reach, and therefore how many
// positions a send dial has counting Off.
//
// The library's own number, like numVoices above, for the same reason: this
// header must compile knowing nothing about the synth. The synth's
// project::numDrumSendDestinations has to agree with it, and where the two
// meet is in the synth's own code - DrumPatternTest holds them equal.
constexpr int numFxUnits = 4;

// Plain atomics read by the audio thread and written by whatever drives it,
// exactly as SynthParameters and mastering::Parameters do it. No locks: a dial
// mid-move may be read as either side of the move, and over a drum hit lasting
// a fifth of a second nobody could hear which.
struct VoiceParameters
{
    // Which of the synthesised drums this slot is, or noEngine for a blank
    // one. An int rather than a float, unlike everything else here, because it
    // is not a dial: nothing sweeps from a cowbell to a clap, and a value
    // between two of them means nothing.
    //
    // Latched at the hit rather than read per sample, in startHit - changing
    // what a slot IS half way through a ring would be a cymbal deciding it is
    // now a kick.
    std::atomic<int> engine{noEngine};

    // The mixer fader. Read every block, NOT latched at the hit - see the note
    // on latching in DrumVoice.h. 0 is silence, exactly.
    std::atomic<float> level{0.8f};

    // Pitch, and with it the voice's filter, plus or minus an octave. 0.5 is
    // the voice's own tuning: the number in its spec, untouched.
    std::atomic<float> tune{0.5f};

    // How long the hit takes to arrive, from nothing at 0 to whatever this
    // particular voice has room for at 1 - see VoiceSpec::maxAttackSeconds.
    //
    // Zero by default, everywhere, because zero is what a drum machine does:
    // a strike is instant, and this is here to take an edge off one or to
    // swell a cymbal, not to change what the kit is.
    std::atomic<float> attack{0.0f};

    // How long the hit rings, a third to three times its own length. 0.5 is the
    // voice's own decay.
    std::atomic<float> decay{0.5f};

    // 0 hard left, 0.5 centre, 1 hard right. Constant power, so a voice panned
    // across does not dip in the middle.
    std::atomic<float> pan{0.5f};

    // The two effect slots, in order: a voice goes through slot 1's unit and
    // then through slot 2's. 0 is off, 1 to numFxUnits picks one.
    //
    // A CHAIN and not two parallel sends, which is what makes the amounts a
    // wet/dry mix rather than a send level - and what makes each voice need
    // its own copy of whatever unit it is pointed at.
    //
    // The selector is an int rather than a float because a destination is a
    // choice, and rounding it at every read would be rounding the same number
    // sixteen times a block. Whatever drives the kit rounds once.
    //
    // Both read every block, like Level and Pan and for the same reason: they
    // are the mixing desk, not the hit. An amount of exactly 0 leaves the
    // signal untouched, sample for sample, and costs nothing.
    std::atomic<int> send1{0};
    std::atomic<float> send1Amount{0.0f};
    std::atomic<int> send2{0};
    std::atomic<float> send2Amount{0.0f};
};

// How the four units are set, in the kit's own units: everything 0-1 except
// the delay's time, which is in seconds because that is what a delay line
// needs and converting a musical division into one requires a tempo the kit
// has no business knowing.
//
// One set of settings, sixteen sets of processors. Turning the reverb's Size
// down turns it down for every voice running through a reverb, because they
// are all running through a copy of THIS - but each keeps its own tail, since
// a chain cannot share one.
//
// Each unit's third control is its own output level, applied to what the unit
// produced before the voice's Amount mixes it back in. Not the same thing as
// the Amount: this scales the unit for every voice at once, which is what a
// distortion needs after its drive has changed the loudness.
struct FxParameters
{
    std::atomic<float> reverbSize{0.5f};
    std::atomic<float> reverbDamp{0.5f};
    std::atomic<float> reverbLevel{0.35f};

    std::atomic<float> driveAmount{0.4f};
    std::atomic<float> driveTone{0.5f};
    std::atomic<float> driveLevel{0.5f};

    // Shelf gains as a multiplier, 1 being flat. A multiplier and not decibels
    // for the reason the delay's time is in seconds: the panel owns the units
    // a person reads, and the DSP owns the ones it multiplies by.
    std::atomic<float> eqLowGain{1.0f};
    std::atomic<float> eqHighGain{1.0f};
    std::atomic<float> eqLevel{0.5f};

    std::atomic<float> delaySeconds{0.25f};
    std::atomic<float> delayFeedback{0.35f};
    std::atomic<float> delayLevel{0.35f};
};

struct KitParameters
{
    // An untouched kit is the 808's eleven and one blank, which is what
    // defaultEngineFor says and has to be said HERE rather than left to
    // whatever drives the kit.
    //
    // A VoiceParameters on its own cannot know: it does not know which slot it
    // is, so noEngine is the only honest member default for one. This is the
    // first place that knows, so it is the first place that can answer - and
    // without it a freshly constructed kit would be twelve blank slots and
    // completely silent, which is a thing a library should not hand back.
    KitParameters()
    {
        for (int slot = 0; slot < numVoices; ++slot)
            voices[(size_t) slot].engine.store(defaultEngineFor(slot));
    }

    std::array<VoiceParameters, numVoices> voices;

    // One fader over the whole kit. Read every block like the voice levels.
    std::atomic<float> masterLevel{0.8f};

    FxParameters fx;
};

// Is anything actually running through an effect?
//
// A slot pointed at a unit with an Amount of zero is not in use: the mix
// leaves the signal untouched, so there is nothing to process and nothing to
// wait for at the end of a render. Both halves of that matter - the render
// path skips the work and the tail report does not charge for it.
inline bool anyVoiceUsesFx(const KitParameters& params)
{
    for (const auto& voice : params.voices)
    {
        if (voice.send1.load() > 0 && voice.send1Amount.load() > 0.0f)
            return true;

        if (voice.send2.load() > 0 && voice.send2Amount.load() > 0.0f)
            return true;
    }

    return false;
}
} // namespace drums
