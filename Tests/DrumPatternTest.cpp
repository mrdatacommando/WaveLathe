// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "StepSequencer.h"
#include "FxpPreset.h"
#include "ParameterRegistry.h"
#include "Drums/DrumKit.h"
#include "Drums/DrumSample.h"
#include "Drums/DrumVoice.h"

#include <cstring>
#include <cmath>

#include <iostream>

using namespace wavelathe;

// The one place that can see both halves.
//
// The drum kit is a library that deliberately knows nothing about the
// sequencer, so it declares its own drums::numVoices rather than taking
// project::numDrumVoices - and two independent sixteens are two numbers that
// can drift apart. A pattern with more rows than the kit has voices would send
// hits to a drum that does not exist; fewer, and a row would be silent with
// nothing to say why.
//
// This file links both, which makes it the only place the two can be held
// together. A compile-time failure, because there is no reason to wait for a
// test run to find out.
static_assert(drums::numVoices == project::numDrumVoices,
              "the drum kit and the drum pattern disagree about how many voices there are");

// The same seam, one layer up. The kit counts its own effect units and the
// synth counts send destinations including Off, so the two differ by exactly
// one - and a fifth unit added on one side only would give the dial a position
// that routes nowhere, or a unit no dial can reach.
static_assert(drums::numFxUnits + 1 == project::numDrumSendDestinations,
              "the rack and the send dial disagree about how many units there are");

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

bool nearly(float a, float b, float tolerance = 0.001f)
{
    return std::abs(a - b) <= tolerance;
}

// A recognisable beat: kick on the four, snare on the backbeat, hats on the
// off-eighths. Something with a shape, so a round trip that quietly transposed
// or dropped a row would be visible rather than arithmetically plausible.
constexpr int kick = 0;
constexpr int snare = 1;
constexpr int hat = 2;

// A cell index for a whole step, so the beat below stays a beat whatever
// drumSubdivisions is. Writing cell numbers directly would have quietly halved
// the tempo of this pattern the moment a step became two cells.
constexpr int cellOfStep(int step) { return step * StepSequencer::drumSubdivisions; }

void writeBeat(StepSequencer& sequencer)
{
    for (int step = 0; step < 16; step += 4)
        sequencer.setDrumCellActive(kick, cellOfStep(step), true);

    for (int step = 4; step < 16; step += 8)
        sequencer.setDrumCellActive(snare, cellOfStep(step), true);

    for (int step = 2; step < 16; step += 4)
        sequencer.setDrumCellActive(hat, cellOfStep(step), true);
}

// Turn a file this build just wrote into the file the PREVIOUS build would have
// written for the same patch.
//
// The alternative was to write a v17 file containing no drums and call that a
// compatibility test. It is not one: it exercises the v17 reader on a v17 file
// and would pass just as happily if the version guard were missing altogether.
// The only thing that proves a v16 file still loads is a v16 file.
//
// This is exact rather than approximate, because the drum block is the LAST
// thing the writer appends and a patch with no drums writes it as a single
// zeroed int. Removing those four bytes and putting the version number back
// yields the identical bytes v16 produced. Three lengths have to follow it
// down - the outer size, the chunk size, and nothing else, because every other
// field is positional.
//
//   0  CcnK        8  FPCh       16  WvF1      24  numParams   56  chunkSize
//   4  byteSize   12  version    20  fxVersion 28  prgName     60  payload
//                                                              64  payloadVersion
int readBigInt(const juce::MemoryBlock& block, int at)
{
    return (int) juce::ByteOrder::bigEndianInt(static_cast<const uint8_t*>(block.getData()) + at);
}

void writeBigInt(juce::MemoryBlock& block, int at, int value)
{
    auto* bytes = static_cast<uint8_t*>(block.getData());
    bytes[at + 0] = (uint8_t) ((value >> 24) & 0xff);
    bytes[at + 1] = (uint8_t) ((value >> 16) & 0xff);
    bytes[at + 2] = (uint8_t) ((value >> 8) & 0xff);
    bytes[at + 3] = (uint8_t) (value & 0xff);
}

// Strips a fixed-size block off the tail, leaving the file as the version
// before that block ends.
//
// Every helper below was written when the drum pattern was the last thing in
// the payload, and they patch the tail. v19 appended the mixer behind it, v20
// the selectors and the rack, v21 the amounts, v22 the attacks and v23 the
// samples - so without these they would be rewriting the newest block and
// leaving the drum pattern alone, which produced a file that read as no
// version at all and failed seven checks at once when v19 first went in.
juce::MemoryBlock withoutTailBlock(const juce::MemoryBlock& written, int bytes)
{
    if (written.getSize() < (size_t) bytes)
        return {};

    juce::MemoryBlock older(written.getData(), written.getSize() - (size_t) bytes);

    writeBigInt(older, 56, readBigInt(older, 56) - bytes);  // chunk size
    writeBigInt(older, 4, readBigInt(older, 4) - bytes);    // outer byte size

    return older;
}

// Written from the FROZEN shape of each block rather than from whatever the
// mixer currently has in it. v19 wrote four controls a voice and always will;
// counting today's nine and multiplying would make these helpers describe the
// present rather than the file on disk, which is the one thing a downgrade
// helper must not do.
constexpr int v19MixerBytes = project::numDrumVoices * 4 * 4;
constexpr int v20RackBytes = project::numDrumVoices * 2 * 4 + project::numDrumFxControls * 4;
constexpr int v21AmountBytes = project::numDrumVoices * 2 * 4;
constexpr int v22AttackBytes = project::numDrumVoices * 4;

// v23 is the first block that is not a fixed size: it is a count and then
// however many samples are loaded. Every caller below works on a project with
// no samples in it, where the whole block is one zeroed int - so this strips
// four bytes and REFUSES if those four bytes are not zero, rather than
// silently producing a file that means something else.
constexpr int v23EmptyBytes = 4;

// v24: a count and then one int per slot, saying which drum is on it.
constexpr int v24EngineBytes = 4 + project::numDrumVoices * 4;

// v25: a count and then one float per bus effect control.
constexpr int v25BusBytes = 4 + project::numBusFxControls * 4;

// The newest block, so the first one off. Refuses unless the count really is
// the bus's control count, for the reason every helper here refuses: a helper
// that strips the wrong bytes produces a file that means something else, and
// the failure surfaces as a confusing read error several versions further
// down rather than here.
juce::MemoryBlock withoutV25Bus(const juce::MemoryBlock& written)
{
    if (written.getSize() < (size_t) v25BusBytes)
        return {};

    if (readBigInt(written, (int) written.getSize() - v25BusBytes) != project::numBusFxControls)
        return {};

    return withoutTailBlock(written, v25BusBytes);
}

// Second off now, behind the bus. Refuses unless the count really is this
// kit's slot count, for the reason the v23 helper refuses a non-zero sample
// count.
//
// It used to take the tail directly, and would have read the bus's floats as
// a slot count the moment v25 went on behind it - refusing every file, and
// taking every older-version test down with it.
juce::MemoryBlock withoutV24Engines(const juce::MemoryBlock& written)
{
    auto v24File = withoutV25Bus(written);

    if (v24File.getSize() < (size_t) v24EngineBytes)
        return {};

    if (readBigInt(v24File, (int) v24File.getSize() - v24EngineBytes) != project::numDrumVoices)
        return {};

    return withoutTailBlock(v24File, v24EngineBytes);
}

// ---- Building a genuine old file, rather than stripping a new one ----------
//
// Every version from v19 to v23 only APPENDED, so an older file could be made
// by cutting blocks off the tail: the blocks left behind kept their shape.
//
// v24 is the first version to change a block's SHAPE - sixteen voices became
// twelve - and that breaks the technique. A stripped v24 file is not a v21
// file; it is a v21 file with a quarter missing from every block. The reader
// then does exactly what it was told, reads the sixteen voices a v21 file
// promises, and swallows the front of the next block. The v19-only case
// happens to survive that (its over-read runs off the end of the file and
// returns zeros nobody checks), which is worth knowing: it passed while the
// v20 and v21 cases failed, and a test that passes for that reason is not
// testing what it says.
//
// So the old blocks are BUILT here, at the count the old versions wrote.
constexpr int oldDrumVoices = 16;

// A value per voice and control that is distinguishable from every other one,
// so a block read at the wrong offset cannot accidentally match.
float oldValueFor(int voice, int slot)
{
    return 0.05f + 0.01f * (float) voice + 0.002f * (float) slot;
}

// Big-endian, because that is what the format is and what readFloatBigEndian
// on the other side expects. memcpy rather than a reinterpret_cast, which
// would be reading a float through an int pointer.
void appendFloat(juce::MemoryBlock& block, float value)
{
    uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));

    const uint8_t bytes[4] = { (uint8_t) ((bits >> 24) & 0xff), (uint8_t) ((bits >> 16) & 0xff),
                               (uint8_t) ((bits >> 8) & 0xff), (uint8_t) (bits & 0xff) };
    block.append(bytes, 4);
}

// Appends the v19-v22 drum blocks to a v18 file, at sixteen voices, and labels
// the result with the version asked for.
juce::MemoryBlock withOldDrumBlocks(const juce::MemoryBlock& v18File, int version)
{
    if (v18File.getSize() == 0)
        return {};

    juce::MemoryBlock tail;
    int slot = 0;

    // v19: level, tune, decay, pan.
    for (int voice = 0; voice < oldDrumVoices; ++voice)
        for (int i = 0; i < 4; ++i)
            appendFloat(tail, oldValueFor(voice, slot + i));

    slot += 4;

    if (version >= 20)
    {
        // The two selectors, then the rack.
        for (int voice = 0; voice < oldDrumVoices; ++voice)
            for (int i = 0; i < 2; ++i)
                appendFloat(tail, oldValueFor(voice, slot + i));

        slot += 2;

        for (int i = 0; i < project::numDrumFxControls; ++i)
            appendFloat(tail, 0.25f + 0.01f * (float) i);
    }

    if (version >= 21)
    {
        for (int voice = 0; voice < oldDrumVoices; ++voice)
            for (int i = 0; i < 2; ++i)
                appendFloat(tail, oldValueFor(voice, slot + i));

        slot += 2;
    }

    if (version >= 22)
        for (int voice = 0; voice < oldDrumVoices; ++voice)
            appendFloat(tail, oldValueFor(voice, slot));

    juce::MemoryBlock built(v18File.getData(), v18File.getSize());
    built.append(tail.getData(), tail.getSize());

    const auto added = (int) tail.getSize();
    writeBigInt(built, 56, readBigInt(built, 56) + added);
    writeBigInt(built, 4, readBigInt(built, 4) + added);
    writeBigInt(built, 64, version);

    return built;
}

juce::MemoryBlock withoutV23Samples(const juce::MemoryBlock& written)
{
    auto v23File = withoutV24Engines(written);

    if (v23File.getSize() < (size_t) v23EmptyBytes)
        return {};

    if (readBigInt(v23File, (int) v23File.getSize() - v23EmptyBytes) != 0)
        return {};

    return withoutTailBlock(v23File, v23EmptyBytes);
}

juce::MemoryBlock withoutV22Attack(const juce::MemoryBlock& written)
{
    auto v22File = withoutV23Samples(written);

    if (v22File.getSize() == 0)
        return {};

    return withoutTailBlock(v22File, v22AttackBytes);
}

juce::MemoryBlock withoutV21Amounts(const juce::MemoryBlock& written)
{
    auto v21File = withoutV22Attack(written);

    if (v21File.getSize() == 0)
        return {};

    return withoutTailBlock(v21File, v21AmountBytes);
}

juce::MemoryBlock withoutV20Rack(const juce::MemoryBlock& written)
{
    auto v20File = withoutV21Amounts(written);

    if (v20File.getSize() == 0)
        return {};

    return withoutTailBlock(v20File, v20RackBytes);
}

// Takes a file this build wrote back to where v18 left it, peeling one block
// at a time: the attacks, then the amounts, then the rack, then the mixer.
// Newest block off the tail first, every time - which is the whole reason they
// go on the tail.
juce::MemoryBlock withoutV19Mixer(const juce::MemoryBlock& written)
{
    auto v19File = withoutV20Rack(written);

    if (v19File.getSize() == 0)
        return {};

    return withoutTailBlock(v19File, v19MixerBytes);
}

juce::MemoryBlock asVersion16(const juce::MemoryBlock& written)
{
    auto older = withoutV19Mixer(written);
    if (older.getSize() == 0)
        return {};

    auto readInt = [&older](int at) { return readBigInt(older, at); };
    auto writeInt = [&older](int at, int value) { writeBigInt(older, at, value); };

    // The four bytes about to be removed must be the empty drum block. If they
    // are not, the writer has changed and this helper is lying about what it
    // produces - so say so rather than quietly making a malformed file.
    const int trailingInt = readInt((int) older.getSize() - 4);
    if (trailingInt != 0)
        return {};

    writeInt(64, 16);                       // payload version
    writeInt(56, readInt(56) - 4);          // chunk size
    writeInt(4, readInt(4) - 4);            // outer byte size
    older.setSize(older.getSize() - 4, false);

    return older;
}

// The same trick as asVersion16, one version further on: take the file this
// build wrote and replace its trailing drum block with the v17 form of the same
// pattern - 64 entries to a row instead of 128, and four fields instead of five.
//
// Worth the arithmetic rather than faking it. v17 is already pushed, so real
// files with real beats in them exist, and "the old rows are spread onto the
// new grid" is a claim that only a genuine old row can test.
juce::MemoryBlock asVersion17(const juce::MemoryBlock& written,
                              const std::vector<SequencerState::DrumVoiceData>& voices)
{
    constexpr int sub = StepSequencer::drumSubdivisions;

    // What this build appended: count, then per voice an index, a length and
    // five fields per cell.
    size_t v18BlockSize = 4;
    for (const auto& voice : voices)
        v18BlockSize += 8 + voice.cells.size() * 20;

    // The mixer comes off first, so the drum block really is the tail again.
    const auto v18File = withoutV19Mixer(written);

    if (v18File.getSize() < v18BlockSize)
        return {};

    juce::MemoryOutputStream older;
    older.writeIntBigEndian((int) voices.size());

    for (const auto& voice : voices)
    {
        const int steps = (int) (voice.cells.size() / (size_t) sub);

        older.writeIntBigEndian(voice.voice);
        older.writeIntBigEndian(steps);

        for (int step = 0; step < steps; ++step)
        {
            // v17 held one entry per step, which is cell step*sub now. Anything
            // written on the cells between steps simply did not exist then.
            const auto& cell = voice.cells[(size_t) (step * sub)];
            older.writeIntBigEndian(cell.active);
            older.writeFloatBigEndian(cell.velocity);
            older.writeFloatBigEndian(cell.probability);
            older.writeIntBigEndian(cell.accent);
        }
    }

    juce::MemoryBlock result(v18File.getData(), v18File.getSize() - v18BlockSize);
    result.append(older.getData(), older.getDataSize());

    auto readInt = [&result](int at) { return readBigInt(result, at); };
    auto writeInt = [&result](int at, int value) { writeBigInt(result, at, value); };

    const int delta = (int) older.getDataSize() - (int) v18BlockSize;

    writeInt(64, 17);                          // payload version
    writeInt(56, readInt(56) + delta);         // chunk size
    writeInt(4, readInt(4) + delta);           // outer byte size

    return result;
}

bool beatIsIntact(const StepSequencer& sequencer)
{
    // Checks every CELL in the first sixteen steps, not just the ones the beat
    // was written on. A migration that landed the hits on the right steps but
    // also left copies on the half-beats between them would pass a test that
    // only looked where it expected something.
    for (int cell = 0; cell < cellOfStep(16); ++cell)
    {
        const bool onAStep = (cell % StepSequencer::drumSubdivisions) == 0;
        const int step = cell / StepSequencer::drumSubdivisions;

        const bool wantKick = onAStep && (step % 4) == 0;
        const bool wantSnare = onAStep && (step % 8) == 4;
        const bool wantHat = onAStep && (step % 4) == 2;

        if (sequencer.getDrumCell(kick, cell).active != wantKick) return false;
        if (sequencer.getDrumCell(snare, cell).active != wantSnare) return false;
        if (sequencer.getDrumCell(hat, cell).active != wantHat) return false;
    }

    return true;
}
} // namespace

int main()
{
    std::cout << "Drum pattern" << std::endl << std::endl;
    std::cout << "  " << StepSequencer::numDrumVoices << " voices of "
              << StepSequencer::numDrumCells << " cells (" << StepSequencer::drumSubdivisions << " to a step)" << std::endl
              << std::endl;

    // ---- The grid's rows and the kit's DEFAULT drums are the same twelve ---
    //
    // Two lists of names, in two files that cannot see each other, describing
    // one column of the interface. The static_assert above catches a count that
    // has drifted; this catches the subtler version, where both lists are the
    // right length but row 9 says OHat on the grid and something else in the
    // kit. Nothing would crash - the wrong drum would simply play, which is the
    // kind of fault that gets blamed on the pattern.
    //
    // The eleven, not all sixteen engines: past the eleventh the engine table
    // keeps going and the grid does not, because a slot's sound is a choice
    // now rather than its position. The twelfth row is blank by default and
    // has no engine to be compared against - which this asserts below rather
    // than skipping quietly, since "no engine" is exactly what an off-by-one
    // in defaultEngineFor would also look like.
    {
        bool allMatch = true;

        check(drums::defaultEngineFor(StepSequencer::numDrumVoices - 1) == drums::noEngine,
              "the last row starts blank");

        for (int voice = 0; voice < StepSequencer::numDrumVoices - 1; ++voice)
        {
            const juce::String onTheGrid(StepSequencer::drumVoiceName(voice));
            const juce::String inTheKit(drums::engineName(drums::defaultEngineFor(voice)));

            if (onTheGrid != inTheKit)
            {
                std::cout << "    row " << voice << ": grid says " << onTheGrid
                          << ", kit says " << inTheKit << std::endl;
                allMatch = false;
            }
        }

        check(allMatch, "every row of the grid names the voice the kit will actually play");
    }

    // ---- Empty to begin with ------------------------------------------------
    {
        StepSequencer sequencer;

        bool anythingOn = false;
        for (int v = 0; v < StepSequencer::numDrumVoices; ++v)
            for (int s = 0; s < StepSequencer::numDrumCells; ++s)
                if (sequencer.getDrumCell(v, s).active)
                    anythingOn = true;

        check(!anythingOn, "a fresh sequencer has no drums on it");
        check(sequencer.getNumUsedDrumVoices() == 0, "and reports no voices in use");

        std::cout << "  a fresh pattern is empty" << std::endl;
    }

    // ---- One cell is one cell ----------------------------------------------
    // The grid is two-dimensional and both indices are new, so this is the
    // place a row/column mix-up would show: setting voice 3 step 5 must not
    // touch voice 5 step 3.
    {
        StepSequencer sequencer;
        sequencer.setDrumCellActive(3, 5, true);

        check(sequencer.getDrumCell(3, 5).active, "the cell that was set is on");
        check(!sequencer.getDrumCell(5, 3).active, "and its transpose is not");

        int onCount = 0;
        for (int v = 0; v < StepSequencer::numDrumVoices; ++v)
            for (int s = 0; s < StepSequencer::numDrumCells; ++s)
                if (sequencer.getDrumCell(v, s).active)
                    ++onCount;

        check(onCount == 1, "exactly one cell in the whole grid is on");
        check(sequencer.getNumUsedDrumVoices() == 1, "and exactly one voice is in use");

        std::cout << "  setting one cell sets exactly one cell" << std::endl;
    }

    // ---- Out of range -------------------------------------------------------
    // Matches the melodic accessors: ignore rather than write somewhere else.
    {
        StepSequencer sequencer;

        sequencer.setDrumCellActive(-1, 0, true);
        sequencer.setDrumCellActive(StepSequencer::numDrumVoices, 0, true);
        sequencer.setDrumCellActive(0, -1, true);
        sequencer.setDrumCellActive(0, StepSequencer::numDrumCells, true);
        sequencer.setDrumCellActive(9999, 9999, true);

        check(sequencer.getNumUsedDrumVoices() == 0, "out-of-range cells set nothing");
        check(!sequencer.getDrumCell(-1, 0).active, "and read back as off");
        check(!sequencer.getDrumCell(0, StepSequencer::numDrumCells).active,
              "at either index");

        std::cout << "  out-of-range cells are ignored, not folded back in" << std::endl;
    }

    // ---- A cell keeps its settings when switched off ------------------------
    // Velocity and probability belong to the cell, not to it being on. A step
    // tuned quiet and then switched off and on again must come back quiet,
    // rather than resetting to full and having to be tuned twice.
    {
        StepSequencer sequencer;

        StepSequencer::DrumCell step;
        step.active = true;
        step.velocity = 0.3f;
        step.probability = 0.5f;
        step.accent = true;
        sequencer.setDrumCell(kick, 0, step);

        sequencer.setDrumCellActive(kick, 0, false);
        check(!sequencer.getDrumCell(kick, 0).active, "switching a cell off switches it off");
        check(nearly(sequencer.getDrumCell(kick, 0).velocity, 0.3f),
              "and leaves its velocity alone");

        sequencer.setDrumCellActive(kick, 0, true);
        const auto back = sequencer.getDrumCell(kick, 0);
        check(back.active && nearly(back.velocity, 0.3f) && nearly(back.probability, 0.5f)
                  && back.accent,
              "and it comes back exactly as it was");

        std::cout << "  a cell's settings survive being switched off" << std::endl;
    }

    // ---- Clearing -----------------------------------------------------------
    {
        StepSequencer sequencer;
        writeBeat(sequencer);

        check(sequencer.getNumUsedDrumVoices() == 3, "the beat uses three voices");
        check(sequencer.isDrumVoiceUsed(kick), "the kick row is in use");
        check(!sequencer.isDrumVoiceUsed(7), "an untouched row is not");

        sequencer.clearDrumVoice(snare);
        check(!sequencer.isDrumVoiceUsed(snare), "clearing one voice empties it");
        check(sequencer.isDrumVoiceUsed(kick) && sequencer.isDrumVoiceUsed(hat),
              "and leaves the others alone");

        sequencer.clearDrumPattern();
        check(sequencer.getNumUsedDrumVoices() == 0, "clearing the pattern empties every row");

        std::cout << "  clearing a voice and clearing the pattern" << std::endl;
    }

    // ---- The melodic pattern is a separate thing ----------------------------
    // Both live on the same transport and under the same lock, which is exactly
    // the arrangement where one could end up writing over the other.
    {
        StepSequencer sequencer;

        StepSequencer::Step note;
        note.active = true;
        note.degree = 4;
        sequencer.setStep(0, note);

        writeBeat(sequencer);

        check(sequencer.getStep(0).active && sequencer.getStep(0).degree == 4,
              "writing drums leaves the melodic pattern alone");

        sequencer.clearDrumPattern();
        check(sequencer.getStep(0).active, "and clearing the drums does not clear the notes");

        sequencer.setDrumCellActive(kick, 0, true);
        sequencer.clearPattern();
        check(sequencer.getDrumCell(kick, 0).active,
              "nor does clearing the notes clear the drums");

        std::cout << "  the two patterns do not reach into each other" << std::endl;
    }

    // ---- Capture and restore ------------------------------------------------
    {
        StepSequencer sequencer;
        writeBeat(sequencer);

        StepSequencer::DrumCell tuned;
        tuned.active = true;
        tuned.velocity = 0.42f;
        tuned.probability = 0.6f;
        tuned.accent = true;
        sequencer.setDrumCell(hat, cellOfStep(6), tuned);

        const auto state = sequencer.captureState();

        check((int) state.drumVoices.size() == 3, "only the voices in use are captured");
        check(!state.isEmpty(), "a project with a beat in it is not empty");

        StepSequencer restored;
        restored.restoreState(state);

        check(beatIsIntact(restored), "the beat comes back exactly as it was written");

        const auto back = restored.getDrumCell(hat, cellOfStep(6));
        check(nearly(back.velocity, 0.42f) && nearly(back.probability, 0.6f) && back.accent,
              "with each cell's own settings intact");

        std::cout << "  a beat survives capture and restore" << std::endl;
    }

    // ---- Restoring replaces, rather than merging ----------------------------
    // A saved pattern names the voices it uses. A voice it does not name has to
    // be silenced, or loading a sparse project would leave the previous beat
    // showing through the gaps.
    {
        StepSequencer sequencer;
        writeBeat(sequencer);
        const auto sparse = sequencer.captureState();

        StepSequencer busy;
        for (int v = 0; v < StepSequencer::numDrumVoices; ++v)
            busy.setDrumCellActive(v, 0, true);

        check(busy.getNumUsedDrumVoices() == StepSequencer::numDrumVoices,
              "every row is in use before the load");

        busy.restoreState(sparse);

        check(busy.getNumUsedDrumVoices() == 3, "loading leaves only the loaded voices");
        check(beatIsIntact(busy), "and the loaded beat is the one that was saved");

        std::cout << "  loading a beat replaces the old one rather than merging" << std::endl;
    }

    // ---- An empty pattern costs nothing -------------------------------------
    {
        StepSequencer sequencer;

        StepSequencer::Step note;
        note.active = true;
        sequencer.setStep(0, note);

        const auto state = sequencer.captureState();
        check(state.drumVoices.empty(), "a project with no beat carries no drum rows");

        std::cout << "  a patch with no drums adds no drum data" << std::endl;
    }

    // ---- Through the preset format ------------------------------------------
    // The round trip that matters: a beat has to survive being written to a
    // file and read back, not only captured in memory.
    {
        StepSequencer sequencer;
        writeBeat(sequencer);

        StepSequencer::DrumCell tuned;
        tuned.active = true;
        tuned.velocity = 0.42f;
        tuned.probability = 0.6f;
        tuned.accent = true;
        sequencer.setDrumCell(hat, cellOfStep(6), tuned);

        PresetValues values;
        values.name = "Beat";
        values.sequencer = sequencer.captureState();

        juce::MemoryBlock data;
        FxpPreset::writeToMemory(values, data);

        PresetValues readBack;
        juce::String error;
        check(FxpPreset::readFromMemory(data.getData(), (int) data.getSize(), readBack, error),
              "a preset carrying a beat reads back: " + error);

        check(readBack.sequencer.drumVoices.size() == values.sequencer.drumVoices.size(),
              "with the same number of voices");

        StepSequencer restored;
        restored.restoreState(readBack.sequencer);

        check(beatIsIntact(restored), "and the beat itself is intact");

        const auto back = restored.getDrumCell(hat, cellOfStep(6));
        check(nearly(back.velocity, 0.42f) && nearly(back.probability, 0.6f) && back.accent,
              "including the settings on individual cells");

        std::cout << "  a beat survives the preset format" << std::endl;
    }

    // ---- Between the beats --------------------------------------------------
    // The reason the grid stopped being one cell per step. A hit on the cell
    // after a step is a 32nd against a 1/16 rate, and it must be its own hit -
    // not a rounding of the step it sits next to.
    {
        StepSequencer sequencer;

        check(StepSequencer::drumSubdivisions >= 2, "a step holds more than one cell");
        check(StepSequencer::numDrumCells
                  == StepSequencer::maxSteps * StepSequencer::drumSubdivisions,
              "and the row is that many times longer");

        const int onStep = cellOfStep(3);
        const int between = onStep + 1;

        sequencer.setDrumCellActive(hat, onStep, true);
        check(sequencer.getDrumCell(hat, onStep).active, "a hit on the step is on");
        check(!sequencer.getDrumCell(hat, between).active, "and the cell after it is not");

        sequencer.setDrumCellActive(hat, between, true);
        check(sequencer.getDrumCell(hat, onStep).active
                  && sequencer.getDrumCell(hat, between).active,
              "both can be on at once - which is what makes a roll possible");

        // They are genuinely different instants, half a step apart.
        check(StepSequencer::tickOfDrumCell(between, 0)
                      - StepSequencer::tickOfDrumCell(onStep, 0)
                  == StepSequencer::ticksPerDrumCell,
              "and they fall one cell apart on the tick grid");

        std::cout << "  two hits inside one step, " << StepSequencer::ticksPerDrumCell
                  << " ticks apart" << std::endl;
    }

    // ---- Nudge --------------------------------------------------------------
    // Feel rather than placement: the cell says which 32nd a hit belongs to,
    // the nudge says how tightly it sits on it. Signed, because pushing early
    // is half of what makes a groove and late-only cannot express it.
    {
        StepSequencer sequencer;
        const int cell = cellOfStep(4);

        sequencer.setDrumCellActive(kick, cell, true);
        check(sequencer.getDrumCell(kick, cell).nudge == 0, "a new hit sits dead on its cell");

        sequencer.setDrumCellNudge(kick, cell, 2);
        check(sequencer.getDrumCell(kick, cell).nudge == 2, "it can be pushed late");

        sequencer.setDrumCellNudge(kick, cell, -2);
        check(sequencer.getDrumCell(kick, cell).nudge == -2, "and early");

        check(StepSequencer::tickOfDrumCell(cell, -2)
                  < StepSequencer::tickOfDrumCell(cell, 0),
              "an early hit really does fall earlier on the tick grid");

        // Clamped, not wrapped. A nudge that could reach the next cell would
        // give two different cells a claim on the same instant.
        sequencer.setDrumCellNudge(kick, cell, 9999);
        check(sequencer.getDrumCell(kick, cell).nudge == StepSequencer::maxDrumNudge,
              "a huge nudge clamps to half a cell");

        sequencer.setDrumCellNudge(kick, cell, -9999);
        check(sequencer.getDrumCell(kick, cell).nudge == -StepSequencer::maxDrumNudge,
              "and so does a huge negative one");

        check(StepSequencer::tickOfDrumCell(cell, -StepSequencer::maxDrumNudge)
                  > StepSequencer::tickOfDrumCell(cell - 1, StepSequencer::maxDrumNudge),
              "so a nudged hit can never reach its neighbour's position");

        // Feel belongs to a hit that exists.
        const int empty = cellOfStep(9);
        sequencer.setDrumCellNudge(kick, empty, 3);
        check(sequencer.getDrumCell(kick, empty).nudge == 0,
              "nudging an empty cell stores nothing");

        // And it survives the hit being switched off and on, like velocity.
        sequencer.setDrumCellNudge(kick, cell, 3);
        sequencer.setDrumCellActive(kick, cell, false);
        sequencer.setDrumCellActive(kick, cell, true);
        check(sequencer.getDrumCell(kick, cell).nudge == 3, "and it survives a hit being retoggled");

        std::cout << "  nudge runs " << -StepSequencer::maxDrumNudge << " to +"
                  << StepSequencer::maxDrumNudge << " ticks and clamps at both ends" << std::endl;
    }

    // ---- Nudge through the file ---------------------------------------------
    {
        StepSequencer sequencer;
        writeBeat(sequencer);

        const int pushed = cellOfStep(8);
        sequencer.setDrumCellActive(hat, pushed, true);
        sequencer.setDrumCellNudge(hat, pushed, -3);

        const int rolled = cellOfStep(12) + 1;
        sequencer.setDrumCellActive(hat, rolled, true);

        PresetValues values;
        values.sequencer = sequencer.captureState();

        juce::MemoryBlock data;
        FxpPreset::writeToMemory(values, data);

        PresetValues readBack;
        juce::String error;
        check(FxpPreset::readFromMemory(data.getData(), (int) data.getSize(), readBack, error),
              "a preset carrying nudged hits reads back: " + error);

        StepSequencer restored;
        restored.restoreState(readBack.sequencer);

        check(restored.getDrumCell(hat, pushed).nudge == -3, "an early hit stays early");
        check(restored.getDrumCell(hat, rolled).active, "and a hit between steps stays there");

        std::cout << "  nudge and off-beat hits survive the file" << std::endl;
    }

    // ---- A patch with no drums in it ----------------------------------------
    {
        PresetValues before;
        before.name = "No drums here";
        before.wavePosition = 0.33f;

        StepSequencer sequencer;
        StepSequencer::Step note;
        note.active = true;
        note.degree = 2;
        sequencer.setStep(0, note);
        before.sequencer = sequencer.captureState();

        juce::MemoryBlock data;
        FxpPreset::writeToMemory(before, data);

        PresetValues readBack;
        juce::String error;
        check(FxpPreset::readFromMemory(data.getData(), (int) data.getSize(), readBack, error),
              "a preset with no drums reads back: " + error);
        check(readBack.sequencer.drumVoices.empty(), "and carries no drum rows");
        check(nearly(readBack.wavePosition, 0.33f), "with the rest of it unharmed");

        std::cout << "  a patch without drums round-trips unchanged" << std::endl;
    }

    // ---- AN ACTUAL v16 FILE -------------------------------------------------
    //
    // The format is additive and every version before this one has to keep
    // working. The danger is not that v16 fails loudly - it is that the reader
    // runs off the end of the payload looking for a drum block that was never
    // written, and returns a patch with plausible rubbish in it.
    //
    // Note what this does NOT do. Writing a v17 file with no drums in it and
    // checking that it loads proves nothing about v16: it exercises the v17
    // reader on a v17 file, and would pass with the version guard deleted. The
    // file below is byte-for-byte what the previous build produced.
    {
        PresetValues before;
        before.name = "From v16";
        before.wavePosition = 0.33f;
        before.driveAmount = 0.44f;

        StepSequencer sequencer;
        StepSequencer::Step note;
        note.active = true;
        note.degree = 2;
        note.velocity = 0.61f;
        sequencer.setStep(0, note);
        sequencer.setLaneValue(3, 0, 0.7f);
        before.sequencer = sequencer.captureState();

        juce::MemoryBlock current;
        FxpPreset::writeToMemory(before, current);

        const auto older = asVersion16(current);
        check(older.getSize() == current.getSize() - 4 - (size_t) v19MixerBytes
                                     - (size_t) v20RackBytes - (size_t) v21AmountBytes
                                     - (size_t) v22AttackBytes - (size_t) v23EmptyBytes
                                     - (size_t) v24EngineBytes - (size_t) v25BusBytes,
              "the v16 file is this one minus every block added since");

        PresetValues readBack;
        juce::String error;
        check(FxpPreset::readFromMemory(older.getData(), (int) older.getSize(), readBack, error),
              "a genuine v16 preset still loads: " + error);

        check(readBack.sequencer.drumVoices.empty(), "and arrives with no drums");
        check(nearly(readBack.wavePosition, 0.33f) && nearly(readBack.driveAmount, 0.44f),
              "with its synth settings intact");
        check(readBack.sequencer.steps.size() == before.sequencer.steps.size()
                  && readBack.sequencer.steps[0].active != 0
                  && readBack.sequencer.steps[0].degree == 2
                  && nearly(readBack.sequencer.steps[0].velocity, 0.61f),
              "and its pattern intact, note for note");
        check(readBack.sequencer.lanes.size() == 1, "and its automation lane still there");

        StepSequencer restored;
        restored.restoreState(readBack.sequencer);
        check(restored.getNumUsedDrumVoices() == 0, "loading it leaves the drum grid empty");

        std::cout << "  a real v16 file still loads, and brings no drums with it" << std::endl;
    }

    // ---- AN ACTUAL v17 FILE -------------------------------------------------
    //
    // v17 is already pushed, so patterns written with one cell per step exist
    // on disk. Each of those entries has to land on the step it was written on
    // - cell i * drumSubdivisions - with nothing appearing on the new positions
    // between them, and no nudge.
    //
    // Same discipline as the v16 case above: the file is the bytes v17 actually
    // produced, rebuilt from the pattern, not a v18 file with the version
    // number altered.
    {
        StepSequencer sequencer;
        writeBeat(sequencer);

        // Something on a cell v17 could not express, to prove the rebuild drops
        // it rather than the reader inventing it.
        sequencer.setDrumCellActive(hat, cellOfStep(6) + 1, true);

        PresetValues values;
        values.name = "From v17";
        values.sequencer = sequencer.captureState();

        juce::MemoryBlock current;
        FxpPreset::writeToMemory(values, current);

        const auto older = asVersion17(current, values.sequencer.drumVoices);
        check(older.getSize() > 0 && older.getSize() < current.getSize(),
              "the v17 file is smaller - half the entries, four fields not five");

        PresetValues readBack;
        juce::String error;
        check(FxpPreset::readFromMemory(older.getData(), (int) older.getSize(), readBack, error),
              "a genuine v17 preset still loads: " + error);

        check(readBack.sequencer.drumVoices.size() == values.sequencer.drumVoices.size(),
              "with the same voices");

        StepSequencer restored;
        restored.restoreState(readBack.sequencer);

        check(beatIsIntact(restored), "and every old hit lands on the step it was written on");
        check(!restored.getDrumCell(hat, cellOfStep(6) + 1).active,
              "with nothing on the cells v17 had no way to write");

        bool anyNudge = false;
        for (int v = 0; v < StepSequencer::numDrumVoices; ++v)
            for (int c = 0; c < StepSequencer::numDrumCells; ++c)
                if (restored.getDrumCell(v, c).nudge != 0)
                    anyNudge = true;

        check(!anyNudge, "and no nudge, because v17 had no such field");

        std::cout << "  a real v17 pattern spreads onto the finer grid" << std::endl;
    }

    // ---- The drum mixer survives a save, and a preset does not touch it ----
    //
    // Ninety-six values across two blocks and twelve more for the rack, saved
    // with the project rather than the patch. Both halves matter: a mixer that
    // did not persist would be rebuilt every session, and one that DID travel
    // with a preset would retune somebody's kit the moment they auditioned a
    // pad.
    {
        using DC = project::DrumControl;

        PresetValues before;
        before.name = "Mixer";

        // Something distinctive in every slot, so a round trip that shifted
        // the array by one would show up rather than landing on a neighbour's
        // identical default.
        //
        // The sends in particular: they are written in a different block from
        // the other four, so an off-by-one between the two blocks is exactly
        // the mistake this arrangement could make.
        for (int voice = 0; voice < project::numDrumVoices; ++voice)
        {
            before.drumControls[(size_t) project::drumControlIndex(voice, DC::level)] = 0.1f + 0.05f * (float) voice;
            before.drumControls[(size_t) project::drumControlIndex(voice, DC::tune)] = (float) (voice - 8);
            before.drumControls[(size_t) project::drumControlIndex(voice, DC::decay)] = 0.9f - 0.05f * (float) voice;
            before.drumControls[(size_t) project::drumControlIndex(voice, DC::pan)] = -1.0f + 0.125f * (float) voice;
            before.drumControls[(size_t) project::drumControlIndex(voice, DC::send1)] = (float) (voice % 5);
            before.drumControls[(size_t) project::drumControlIndex(voice, DC::send2)] = (float) (4 - voice % 5);
            before.drumControls[(size_t) project::drumControlIndex(voice, DC::send1Amount)] = 0.03f * (float) voice;
            before.drumControls[(size_t) project::drumControlIndex(voice, DC::send2Amount)] = 1.0f - 0.03f * (float) voice;
            before.drumControls[(size_t) project::drumControlIndex(voice, DC::attack)] = 0.02f * (float) voice;
        }

        for (int i = 0; i < project::numDrumFxControls; ++i)
            before.drumFx[(size_t) i] = 0.07f * (float) (i + 1);

        juce::MemoryBlock block;
        FxpPreset::writeToMemory(before, block);

        PresetValues after;
        juce::String error;
        check(FxpPreset::readFromMemory(block.getData(), (int) block.getSize(), after, error),
              "a project with a drum mixer saves and loads: " + error);

        bool allMatch = true;
        for (size_t i = 0; i < before.drumControls.size(); ++i)
            if (!nearly(before.drumControls[i], after.drumControls[i]))
                allMatch = false;

        check(allMatch, "every one of the " + juce::String(project::numDrumStoredControls)
                            + " comes back as it went in");

        bool rackMatches = true;
        for (size_t i = 0; i < before.drumFx.size(); ++i)
            if (!nearly(before.drumFx[i], after.drumFx[i]))
                rackMatches = false;

        check(rackMatches, "and so does every dial in the rack");

        // A v21 file has everything but the attack, and zero is what it must
        // arrive at: a kit written before there was an attack struck
        // instantly, and zero is instant.
        //
        // Built at SIXTEEN voices, because that is what a v21 file has. The
        // amounts are the fourth block in, so they only read correctly if all
        // three before them were consumed at the right size - which makes this
        // the check that the frozen count is being honoured rather than a
        // check about amounts.
        // The v18 shape: the drum PATTERN still there, every mixer block since
        // stripped. asVersion16 takes the pattern off too, so building on that
        // produced a file whose mixer bytes the reader consumed as a pattern
        // header - it refused them, correctly, and the checks below failed
        // with an empty error because the refusal happened before any of them.
        const auto v18 = withoutV19Mixer(block);
        const auto v21 = withOldDrumBlocks(v18, 21);

        if (v21.getSize() > 0)
        {
            PresetValues old;
            juce::String oldError;
            check(FxpPreset::readFromMemory(v21.getData(), (int) v21.getSize(), old, oldError),
                  "a sixteen-voice v21 file still loads: " + oldError);

            bool attacksAreOff = true;
            bool amountsSurvived = true;

            for (int voice = 0; voice < project::numDrumVoices; ++voice)
            {
                const auto attack = (size_t) project::drumControlIndex(voice, DC::attack);
                if (!nearly(old.drumControls[attack], 0.0f))
                    attacksAreOff = false;

                const auto amount = (size_t) project::drumControlIndex(voice, DC::send1Amount);
                if (!nearly(old.drumControls[amount], oldValueFor(voice, 6)))
                    amountsSurvived = false;
            }

            check(attacksAreOff, "with every voice striking instantly");
            check(amountsSurvived,
                  "and the amounts v21 did write still read correctly - so all four blocks "
                  "before them were read at sixteen voices, not twelve");
        }

        // A v20 file has the selectors but no amounts. Zero is the right
        // answer for a reason worth separating from the arithmetic: a project
        // written before the chain existed had nothing running through any
        // effect, and zero is exactly "no effect" rather than merely what a
        // stream past its end happens to return.
        const auto v20 = withOldDrumBlocks(v18, 20);
        if (v20.getSize() > 0)
        {
            PresetValues old;
            juce::String oldError;
            check(FxpPreset::readFromMemory(v20.getData(), (int) v20.getSize(), old, oldError),
                  "a sixteen-voice v20 file still loads: " + oldError);

            bool amountsAreZero = true;
            bool selectorsSurvived = true;

            for (int voice = 0; voice < project::numDrumVoices; ++voice)
            {
                const auto amount1 = (size_t) project::drumControlIndex(voice, DC::send1Amount);
                const auto amount2 = (size_t) project::drumControlIndex(voice, DC::send2Amount);

                if (!nearly(old.drumControls[amount1], 0.0f) || !nearly(old.drumControls[amount2], 0.0f))
                    amountsAreZero = false;

                const auto select1 = (size_t) project::drumControlIndex(voice, DC::send1);
                if (!nearly(old.drumControls[select1], oldValueFor(voice, 4)))
                    selectorsSurvived = false;
            }

            check(amountsAreZero, "with every chain dry, which is what a project with no chain meant");
            check(selectorsSurvived, "and the selectors v20 did write still read correctly");
        }

        // A v19 file has the mixer but no selectors and no rack. The rack must
        // arrive at its DEFAULTS, not at zero - the stream returns zero past
        // its end, so a missing version guard here would leave the delay with
        // a time of nothing and every unit at silence.
        const auto v19 = withOldDrumBlocks(v18, 19);
        if (v19.getSize() > 0)
        {
            PresetValues old;
            juce::String oldError;
            check(FxpPreset::readFromMemory(v19.getData(), (int) v19.getSize(), old, oldError),
                  "a sixteen-voice v19 file still loads: " + oldError);

            check(old.drumFx == defaultDrumFx(),
                  "and arrives with an untouched rack rather than a silenced one");

            bool slotsAreOff = true;
            bool levelsSurvived = true;

            for (int voice = 0; voice < project::numDrumVoices; ++voice)
            {
                if (!nearly(old.drumControls[(size_t) project::drumControlIndex(voice, DC::send1)], 0.0f)
                    || !nearly(old.drumControls[(size_t) project::drumControlIndex(voice, DC::send2)], 0.0f))
                    slotsAreOff = false;

                const auto level = (size_t) project::drumControlIndex(voice, DC::level);
                if (!nearly(old.drumControls[level], oldValueFor(voice, 0)))
                    levelsSurvived = false;
            }

            check(slotsAreOff, "with both slots off");
            check(levelsSurvived, "and the four controls v19 did write still read correctly");
        }

        // A v18 file has no mixer at all.
        const auto v18File = asVersion16(block);
        if (v18File.getSize() > 0)
        {
            PresetValues old;
            juce::String oldError;
            check(FxpPreset::readFromMemory(v18File.getData(), (int) v18File.getSize(), old, oldError),
                  "a file older than the mixer still loads: " + oldError);
            check(old.drumControls == defaultDrumControls(),
                  "and arrives with an untouched mixer rather than a silenced one");
        }

        // And the project/preset line: applying as a PRESET leaves the mixer
        // alone, applying as a project restores it.
        SynthParameters params;
        const auto levelIndex = (size_t) project::drumControlIndex(0, DC::level);
        params.drumControls[levelIndex].store(0.42f);
        params.drumFx[(size_t) project::DrumFxControl::reverbMix].store(0.11f);

        FxpPreset::applyToSynthParameters(before, params, FxpPreset::ApplyScope::synthPreset);
        check(nearly(params.drumControls[levelIndex].load(), 0.42f),
              "loading a preset leaves the drum mixer where it was");
        check(nearly(params.drumFx[(size_t) project::DrumFxControl::reverbMix].load(), 0.11f),
              "and leaves the rack where it was");

        FxpPreset::applyToSynthParameters(before, params, FxpPreset::ApplyScope::project);
        check(nearly(params.drumControls[levelIndex].load(), before.drumControls[levelIndex]),
              "loading a project restores it");
        check(nearly(params.drumFx[(size_t) project::DrumFxControl::reverbMix].load(),
                     before.drumFx[(size_t) project::DrumFxControl::reverbMix]),
              "and restores the rack with it");

        std::cout << "  " << project::numDrumStoredControls
                  << " mixer values plus a rack, across four format versions,"
                     " all travelling with the project" << std::endl;
    }

    // ---- A sample travels inside the project -------------------------------
    //
    // Embedded rather than referenced by path, which is the whole decision:
    // a project you send to somebody else has to still play, and a path into
    // your own Samples folder does not survive the journey.
    //
    // So the audio itself has to come back byte for byte. Not "close" - these
    // are the exact floats that were in memory, and a format that quietly
    // requantised them would degrade a project every time it was reopened.
    {
        PresetValues before;
        before.name = "With samples";

        const auto makeSample = [](const juce::String& name, double rate, int frames)
        {
            auto sample = std::make_shared<drums::Sample>();
            sample->name = name;
            sample->sourceRate = rate;
            sample->data.resize((size_t) frames);

            for (int i = 0; i < frames; ++i)
                sample->data[(size_t) i] = std::sin((float) i * 0.05f) * 0.8f;

            return sample;
        };

        // Two slots rather than one, at DIFFERENT rates, and on voices that
        // are not adjacent. A block that wrote the rate once, or that assumed
        // the slots were the first two, would pass with one.
        before.drumSamples[0] = makeSample("Kick909", 44100.0, 1000);
        before.drumSamples[10] = makeSample("CrashRide", 48000.0, 733);

        juce::MemoryBlock block;
        FxpPreset::writeToMemory(before, block);

        PresetValues after;
        juce::String error;
        check(FxpPreset::readFromMemory(block.getData(), (int) block.getSize(), after, error),
              "a project carrying samples saves and loads: " + error);

        int matched = 0;

        for (int voice = 0; voice < project::numDrumVoices; ++voice)
        {
            const auto& sent = before.drumSamples[(size_t) voice];
            const auto& got = after.drumSamples[(size_t) voice];

            if (sent == nullptr)
            {
                check(got == nullptr,
                      "voice " + juce::String(voice) + " had no sample and still has none");
                continue;
            }

            if (got == nullptr)
            {
                check(false, "voice " + juce::String(voice) + " lost its sample");
                continue;
            }

            check(got->name == sent->name, "and it kept its name: " + got->name);
            check(got->sourceRate == sent->sourceRate,
                  "and its own rate (" + juce::String(got->sourceRate, 0) + " Hz)");
            check(got->data == sent->data, "and every sample of its audio, exactly");
            ++matched;
        }

        check(matched == 2, "both slots came back");

        // A slot loaded is a slot the sequencer settings carry, so the same
        // project/preset line the mixer is on. Applying a PRESET must not
        // silently unload somebody's kit.
        const auto sizeWithSamples = block.getSize();

        PresetValues bare;
        bare.name = "No samples";
        juce::MemoryBlock bareBlock;
        FxpPreset::writeToMemory(bare, bareBlock);

        check(bareBlock.getSize() < sizeWithSamples,
              "a project with no samples is smaller than one with two");

        // And the size is roughly the audio plus the rest, which is the price
        // of embedding stated out loud rather than discovered later: 1733
        // floats is just under seven kilobytes.
        const auto added = (int) (sizeWithSamples - bareBlock.getSize());
        check(added > 1733 * 4 && added < 1733 * 4 + 200,
              "and the difference is the audio itself (" + juce::String(added) + " bytes)");

        std::cout << "  samples travel inside the project: " << added
                  << " bytes for 1733 frames across two slots" << std::endl;
    }

    // ---- A corrupt sample block is refused rather than allocated -----------
    //
    // Every field in the v23 block came out of a file that this program may
    // not have written. A frame count of two billion is an allocation
    // request, not a project.
    {
        PresetValues before;
        before.name = "Corrupt";

        auto sample = std::make_shared<drums::Sample>();
        sample->name = "S";
        sample->sourceRate = 44100.0;
        sample->data.resize(64, 0.25f);
        before.drumSamples[3] = sample;

        juce::MemoryBlock block;
        FxpPreset::writeToMemory(before, block);

        // The frame count is the last int before the audio. It used to sit
        // exactly that many floats from the END of the file, and does not any
        // more: v24's engine block goes on behind the samples, so that has to
        // come off the offset too. Without it this patched a float of audio
        // instead, the file read back perfectly well, and the test failed
        // saying a corrupt block was accepted - which was true of the test
        // rather than of the reader. v25's bus block went on behind that, and
        // comes off for the same reason.
        auto corrupted = block;
        const auto countAt = (int) corrupted.getSize() - v25BusBytes - v24EngineBytes - 64 * 4 - 4;
        writeBigInt(corrupted, countAt, 2000000000);

        PresetValues out;
        juce::String error;

        check(!FxpPreset::readFromMemory(corrupted.getData(), (int) corrupted.getSize(), out, error),
              "a sample block claiming two billion frames is refused");
        check(error.isNotEmpty(), "and says why: " + error);

        std::cout << "  and a corrupt one is refused rather than allocated" << std::endl;
    }

    // ---- Which controls the host can see -----------------------------------
    //
    // The arithmetic that decides this is spread over three places - the index
    // function in ProjectState, the loop that builds the rows, and the panel
    // asking for an id - and the only thing holding them together is that they
    // agree. So they are checked against each other here rather than trusted.
    {
        using DC = project::DrumControl;

        // 171 of 192, all nine controls on all twelve voices. It was 111 of 128
        // while four of the nine were registered, and 127 of 128 before that at
        // sixteen rows of four.
        //
        // Written as the arithmetic rather than as 171, so the number moves
        // with whichever of the two halves changes - but checked against the
        // CAP as well, because a registry that has quietly outgrown the mask is
        // not a compile error anywhere.
        check(paramreg::count() == 63 + project::numDrumParameters,
              "the registry is the synth's 63 plus the kit's "
                  + juce::String(project::numDrumParameters)
                  + ", and is " + juce::String(paramreg::count()));

        check(paramreg::count() <= paramreg::maxParameters,
              "and still fits the " + juce::String(paramreg::maxParameters)
                  + " the parameter mask can carry");

        std::cout << "  registry: " << paramreg::count() << " of " << paramreg::maxParameters
                  << ", " << (paramreg::maxParameters - paramreg::count()) << " spare"
                  << std::endl;

        bool idsAreRight = true;
        bool allRegistered = true;
        int seen = 0;

        for (int voice = 0; voice < project::numDrumVoices; ++voice)
        {
            for (int c = 0; c < project::numDrumControls; ++c)
            {
                const auto control = (DC) c;
                const auto id = paramreg::drumParameterId(voice, control);

                if (id < 0)
                {
                    allRegistered = false;
                    continue;
                }

                ++seen;

                // The registry row this id names must point back at the very
                // slot the panel would write to. That is the whole contract
                // between the two, and an off-by-one in either direction puts
                // one voice's dial on another voice's parameter.
                if (paramreg::all()[(size_t) id].drumControl
                    != project::drumControlIndex(voice, control))
                    idsAreRight = false;
            }
        }

        check(allRegistered && seen == project::numDrumStoredControls,
              "every one of the " + juce::String(project::numDrumStoredControls)
                  + " drum controls has an id, so MIDI learn can reach all of them");
        check(idsAreRight, "and each id points back at the slot the panel writes to");

        // The four that were unregistered until 0.44.0, checked by name rather
        // than by the loop above. The loop would still pass if the enum lost a
        // control, since it walks whatever is there; these are the ones the
        // whole change was about and they are worth naming.
        check(paramreg::drumParameterId(0, DC::tune) >= 0
                  && paramreg::drumParameterId(0, DC::attack) >= 0
                  && paramreg::drumParameterId(0, DC::pan) >= 0
                  && paramreg::drumParameterId(0, DC::send1) >= 0
                  && paramreg::drumParameterId(0, DC::send2) >= 0,
              "Tune, Attack, Pan and the two selectors are among them");

        // A selector is a choice wearing a dial, and the host is told so. A
        // continuous send selector would have a DAW drawing ramps through
        // positions between two units, which are values the engine rounds away.
        const auto send1 = paramreg::drumParameterId(0, DC::send1);
        check(paramreg::steps(send1) == project::numDrumSendDestinations,
              "a send selector has one step per unit in the rack");
        check(juce::String(paramreg::stepLabel(send1, 0)) == "Off"
                  && juce::String(paramreg::stepLabel(send1, 1)) == "Reverb 1",
              "and its positions are named the way the rack names them");
        check(paramreg::steps(paramreg::drumParameterId(0, DC::send1Amount)) == 0,
              "while the amount beside it is continuous");

        // The ranges the storage array actually holds, which are not all 0-1.
        // A descriptor claiming 0-1 for Tune would put the whole dial in the
        // top half of its travel the moment a host or a lane wrote to it.
        const auto& tune = paramreg::all()[(size_t) paramreg::drumParameterId(0, DC::tune)];
        check(tune.minValue == -12.0f && tune.maxValue == 12.0f,
              "Tune is registered in semitones, an octave either way");

        const auto& pan = paramreg::all()[(size_t) paramreg::drumParameterId(0, DC::pan)];
        check(pan.minValue == -1.0f && pan.maxValue == 1.0f, "and Pan runs left to right");

        std::cout << "  the host sees all nine controls on all twelve voices" << std::endl;
    }

    // ---- Which drum is on which slot travels with the project --------------
    //
    // The whole point of twelve slots rather than sixteen is that a slot's
    // sound is a choice. A choice that does not survive a save is not one.
    {
        PresetValues before;
        before.name = "Rearranged";

        // Deliberately not the defaults, and deliberately including a repeat
        // and a blank: two slots on the same drum is allowed, and the blank is
        // the case a bare `if (engine)` would get wrong.
        before.drumEngines[0] = 13;                 // Maraca on the Kick row
        before.drumEngines[1] = drums::noEngine;    // Snare row emptied
        before.drumEngines[2] = 13;                 // Maraca again
        before.drumEngines[11] = 12;                // Clave on the Aux row

        juce::MemoryBlock block;
        FxpPreset::writeToMemory(before, block);
        check(block.getSize() > 0, "a rearranged kit writes");

        PresetValues after;
        juce::String error;
        check(FxpPreset::readFromMemory(block.getData(), (int) block.getSize(), after, error),
              "and reads back: " + error);

        bool enginesMatch = true;
        for (size_t i = 0; i < before.drumEngines.size(); ++i)
            if (before.drumEngines[i] != after.drumEngines[i])
                enginesMatch = false;

        check(enginesMatch, "with every slot still on the drum it was put on");
        check(after.drumEngines[1] == drums::noEngine, "including the one left blank");
        check(after.drumEngines[0] == after.drumEngines[2],
              "and two slots may hold the same drum");

        std::cout << "  which drum sits on which slot survives the file" << std::endl;
    }

    // ---- The Master page's bus effects travel with the project -------------
    //
    // v25. Four promises: the bus survives a save; a project written before it
    // existed reads as a DRY bus, which is exactly what it sounded like; a
    // patch does not rewrite it while a project does; and a file lying about
    // how much bus it holds is refused rather than believed.
    {
        using B = project::BusFxControl;

        PresetValues before;
        before.name = "Bus";
        before.busFx[(size_t) B::reverbMix] = 0.37f;
        before.busFx[(size_t) B::reverbSize] = 0.81f;
        before.busFx[(size_t) B::delaySync] = 1.0f;
        before.busFx[(size_t) B::eqHigh] = -4.5f;
        before.busFx[(size_t) B::outputGain] = -3.0f;

        juce::MemoryBlock block;
        FxpPreset::writeToMemory(before, block);

        PresetValues after;
        juce::String error;
        check(FxpPreset::readFromMemory(block.getData(), (int) block.getSize(), after, error),
              "a project with a bus reads back: " + error);

        bool allMatch = true;
        for (size_t i = 0; i < before.busFx.size(); ++i)
            if (!nearly(before.busFx[i], after.busFx[i]))
                allMatch = false;

        check(allMatch, "every one of the " + juce::String(project::numBusFxControls)
                            + " bus settings survives the file");

        // A genuine v24 file: this one with the bus block taken off the tail
        // and the version put back. What every project saved before today is.
        auto v24 = withoutV25Bus(block);
        check(v24.getSize() == block.getSize() - (size_t) v25BusBytes,
              "the bus block is exactly the count and one float per control");
        writeBigInt(v24, 64, 24);

        PresetValues older;
        check(FxpPreset::readFromMemory(v24.getData(), (int) v24.getSize(), older, error),
              "a v24 project still reads: " + error);

        bool dry = true;
        for (int i = 0; i < project::numBusFxControls; ++i)
            if (!nearly(older.busFx[(size_t) i], project::defaultBusFxValue((B) i)))
                dry = false;

        check(dry, "and brings a dry, flat, unity bus - which is what it sounded like");
        check(older.drumEngines == before.drumEngines, "with everything before the bus intact");

        // The project/patch line, on the drum rack's flag and for its reason.
        SynthParameters params;
        params.busFx[(size_t) B::reverbMix].store(0.05f);

        FxpPreset::applyToSynthParameters(before, params, FxpPreset::ApplyScope::synthPreset);
        check(nearly(params.busFx[(size_t) B::reverbMix].load(), 0.05f),
              "loading a patch leaves the bus where it was");

        FxpPreset::applyToSynthParameters(before, params, FxpPreset::ApplyScope::project);
        check(nearly(params.busFx[(size_t) B::reverbMix].load(), 0.37f)
                  && nearly(params.busFx[(size_t) B::outputGain].load(), -3.0f),
              "loading a project restores it");

        // A count that could make the reader chew through a gigabyte.
        auto lying = block;
        writeBigInt(lying, (int) lying.getSize() - v25BusBytes, 1 << 30);

        PresetValues refused;
        check(!FxpPreset::readFromMemory(lying.getData(), (int) lying.getSize(), refused, error),
              "a bus block claiming a billion settings is refused");

        std::cout << "  the bus effects travel with a project, and not with a patch" << std::endl;
    }

    // ---- A synth preset is the Synth page's sound, and nothing else --------
    //
    // Two lists of the same fields have to agree: what Save Synth Preset
    // strips out (FxpPreset::synthPresetOnly) and what Load Synth Preset leaves
    // alone (ApplyScope::synthPreset). This is the third list, checking every
    // field on both - a field on one and not the other is either a synth
    // preset carrying a piece of somebody's project, or a load that reaches
    // into one.
    {
        using B = project::BusFxControl;
        using DC = project::DrumControl;

        // A project with every project-level field away from its default...
        PresetValues everything;
        everything.name = "Everything";
        everything.bpm = 97.0f;
        everything.midiClockSync = 1.0f;
        everything.masteringEnabled = 1.0f;
        everything.masterSatDrive = 0.4f;
        everything.masterSatMix = 0.6f;
        everything.masterCompThresholdDb = -18.0f;
        everything.masterCompRatio = 4.0f;
        everything.masterCompAttackMs = 3.0f;
        everything.masterCompReleaseMs = 250.0f;
        everything.masterCompMakeupDb = 5.0f;
        everything.masterCompMix = 0.7f;
        everything.masterLimitCeilingDb = -1.0f;
        everything.masterLimitReleaseMs = 120.0f;
        everything.masterFxGainDb = -2.0f;
        everything.busFx[(size_t) B::reverbMix] = 0.3f;
        everything.seqDivision = 9.0f;
        everything.seqLength = 32.0f;
        everything.seqRootNote = 60.0f;
        everything.seqRecordNotes = 0.0f;
        everything.seqQuantise = 0.0f;
        everything.sequencer.steps.resize(16);
        everything.sequencer.steps[0].active = 1;
        everything.drumControls[(size_t) project::drumControlIndex(0, DC::level)] = 0.1f;
        everything.drumFx[(size_t) project::DrumFxControl::reverbMix] = 0.9f;
        everything.drumEngines[0] = 13;
        everything.midiMap = "74:Cutoff";
        everything.keyboardChannel = 3;

        auto sample = std::make_shared<drums::Sample>();
        sample->name = "hit";
        sample->sourceRate = 44100.0;
        sample->data.assign(44100, 0.25f);
        everything.drumSamples[0] = sample;

        // ...and a sound of its own, including the parts that are easy to
        // forget are the synth's: the arpeggiator and Play in Key. Play in Key
        // is the synth's by decision, not by default - presets are built on
        // their key, scale and chord - so all three are pinned here, both ways.
        everything.filterCutoffHz = 1234.0f;
        everything.fxDistortion = 0.25f;
        everything.arpEnabled = 1.0f;
        everything.musicalKey = 7.0f;
        everything.musicalScale = 3.0f;
        everything.chordMode = 1.0f;

        const PresetValues fresh;

        // ---- Save: the project's part stripped, the sound kept ----
        const auto synth = FxpPreset::synthPresetOnly(everything);

        juce::StringArray kept;
        const auto stripped = [&kept](bool isFresh, const char* field)
        {
            if (!isFresh)
                kept.add(field);
        };

        stripped(synth.bpm == fresh.bpm, "bpm");
        stripped(synth.midiClockSync == fresh.midiClockSync, "midiClockSync");
        stripped(synth.masteringEnabled == fresh.masteringEnabled, "masteringEnabled");
        stripped(synth.masterSatDrive == fresh.masterSatDrive, "masterSatDrive");
        stripped(synth.masterSatMix == fresh.masterSatMix, "masterSatMix");
        stripped(synth.masterCompThresholdDb == fresh.masterCompThresholdDb, "masterCompThresholdDb");
        stripped(synth.masterCompRatio == fresh.masterCompRatio, "masterCompRatio");
        stripped(synth.masterCompAttackMs == fresh.masterCompAttackMs, "masterCompAttackMs");
        stripped(synth.masterCompReleaseMs == fresh.masterCompReleaseMs, "masterCompReleaseMs");
        stripped(synth.masterCompMakeupDb == fresh.masterCompMakeupDb, "masterCompMakeupDb");
        stripped(synth.masterCompMix == fresh.masterCompMix, "masterCompMix");
        stripped(synth.masterLimitCeilingDb == fresh.masterLimitCeilingDb, "masterLimitCeilingDb");
        stripped(synth.masterLimitReleaseMs == fresh.masterLimitReleaseMs, "masterLimitReleaseMs");
        stripped(synth.masterFxGainDb == fresh.masterFxGainDb, "masterFxGainDb");
        stripped(synth.busFx == fresh.busFx, "busFx");
        stripped(synth.seqDivision == fresh.seqDivision, "seqDivision");
        stripped(synth.seqLength == fresh.seqLength, "seqLength");
        stripped(synth.seqRootNote == fresh.seqRootNote, "seqRootNote");
        stripped(synth.seqRecordNotes == fresh.seqRecordNotes, "seqRecordNotes");
        stripped(synth.seqQuantise == fresh.seqQuantise, "seqQuantise");
        stripped(synth.sequencer.isEmpty(), "sequencer");
        stripped(synth.drumControls == fresh.drumControls, "drumControls");
        stripped(synth.drumFx == fresh.drumFx, "drumFx");
        stripped(synth.drumEngines == fresh.drumEngines, "drumEngines");
        stripped(synth.drumSamples[0] == nullptr, "drumSamples");
        stripped(synth.midiMap.isEmpty(), "midiMap");
        stripped(synth.keyboardChannel == fresh.keyboardChannel, "keyboardChannel");

        check(kept.isEmpty(), "a synth preset keeps nothing of the project"
                                  + (kept.isEmpty() ? juce::String() : " - it kept " + kept.joinIntoString(", ")));

        check(nearly(synth.filterCutoffHz, 1234.0f) && nearly(synth.fxDistortion, 0.25f)
                  && nearly(synth.arpEnabled, 1.0f) && synth.name == "Everything",
              "and keeps all of the sound, the arpeggiator included");
        check(nearly(synth.musicalKey, 7.0f) && nearly(synth.musicalScale, 3.0f) && nearly(synth.chordMode, 1.0f),
              "and keeps Play in Key - its key, scale and chord");

        // Which is what makes a preset a preset rather than a project in a
        // different box: kilobytes, not a second of drum audio.
        juce::MemoryBlock projectFile, presetFile;
        FxpPreset::writeToMemory(everything, projectFile);
        FxpPreset::writeToMemory(synth, presetFile);

        std::cout << "  the same session is " << (int) projectFile.getSize() << " bytes as a project and "
                  << (int) presetFile.getSize() << " as a synth preset" << std::endl;
        check(presetFile.getSize() * 10 < projectFile.getSize(),
              "a synth preset does not carry the kit's samples");

        // ---- Load: the project left alone, the sound taken ----
        SynthParameters params;
        params.bpm = 140.0f;
        params.masteringEnabled = 0.0f;
        params.masterCompRatio = 8.0f;
        params.masterFxGainDb = 6.0f;
        params.seqLength = 16.0f;
        params.busFx[(size_t) B::reverbMix].store(0.05f);
        params.drumFx[(size_t) project::DrumFxControl::reverbMix].store(0.2f);

        FxpPreset::applyToSynthParameters(everything, params, FxpPreset::ApplyScope::synthPreset);

        juce::StringArray touched;
        const auto untouched = [&touched](bool same, const char* field)
        {
            if (!same)
                touched.add(field);
        };

        untouched(nearly(params.bpm.load(), 140.0f), "bpm");
        untouched(nearly(params.masteringEnabled.load(), 0.0f), "masteringEnabled");
        untouched(nearly(params.masterCompRatio.load(), 8.0f), "masterCompRatio");
        untouched(nearly(params.masterFxGainDb.load(), 6.0f), "masterFxGainDb");
        untouched(nearly(params.seqLength.load(), 16.0f), "seqLength");
        untouched(nearly(params.busFx[(size_t) B::reverbMix].load(), 0.05f), "busFx");
        untouched(nearly(params.drumFx[(size_t) project::DrumFxControl::reverbMix].load(), 0.2f), "drumFx");

        check(touched.isEmpty(), "loading a synth preset leaves the project alone"
                                     + (touched.isEmpty() ? juce::String() : " - it moved " + touched.joinIntoString(", ")));

        check(nearly(params.filterCutoffHz.load(), 1234.0f) && nearly(params.arpEnabled.load(), 1.0f),
              "and takes all of the sound");
        check(nearly(params.musicalKey.load(), 7.0f) && nearly(params.musicalScale.load(), 3.0f)
                  && nearly(params.chordMode.load(), 1.0f),
              "and takes Play in Key with it - key, scale and chord");

        // And a project is everything - the Master page's mastering chain
        // included, which is the half of that page that used to go missing
        // from this promise the other way round, arriving with every patch.
        FxpPreset::applyToSynthParameters(everything, params, FxpPreset::ApplyScope::project);

        check(nearly(params.bpm.load(), 97.0f) && nearly(params.masteringEnabled.load(), 1.0f)
                  && nearly(params.masterCompRatio.load(), 4.0f) && nearly(params.masterFxGainDb.load(), -2.0f)
                  && nearly(params.seqLength.load(), 32.0f)
                  && nearly(params.busFx[(size_t) B::reverbMix].load(), 0.3f),
              "loading a project brings everything, the mastering chain included");

        std::cout << "  a synth preset is the sound alone; a project is everything" << std::endl;
    }

    // ---- A project older than the twelve-slot kit says what it lost --------
    //
    // The promise made when the kit came down from sixteen: rows that no
    // longer exist are dropped, and NAMED. Dropping somebody's pattern in
    // silence is the thing this is here to stop.
    {
        PresetValues before;
        before.name = "Sixteen rows";

        // A row that still exists, and two that do not. Row 14 is left EMPTY
        // on purpose: an empty row past the end must not be reported, or every
        // project from the old kit would cry wolf about four rows nobody
        // touched, and a warning that always fires is a warning nobody reads.
        const auto rowWith = [](int voice, bool withHits)
        {
            SequencerState::DrumVoiceData row;
            row.voice = voice;
            row.cells.assign((size_t) project::maxDrumCells, {});

            if (withHits)
                row.cells[0].active = 1;

            return row;
        };

        before.sequencer.drumVoices.push_back(rowWith(kick, true));
        before.sequencer.drumVoices.push_back(rowWith(12, true));    // Clave
        before.sequencer.drumVoices.push_back(rowWith(13, false));   // Maraca, untouched
        before.sequencer.drumVoices.push_back(rowWith(15, true));    // Perc 2

        juce::MemoryBlock block;
        FxpPreset::writeToMemory(before, block);
        check(block.getSize() > 0, "a sixteen-row pattern writes");

        PresetValues after;
        juce::String error;
        check(FxpPreset::readFromMemory(block.getData(), (int) block.getSize(), after, error),
              "and still loads rather than being refused: " + error);

        bool everyRowFits = true;
        for (const auto& row : after.sequencer.drumVoices)
            if (row.voice < 0 || row.voice >= project::numDrumVoices)
                everyRowFits = false;

        check(everyRowFits, "with every row it kept inside the twelve");
        check(after.sequencer.drumVoices.size() == 1, "which here is the one that was in range");

        check(after.droppedDrumRows.size() == 2,
              "and the two rows with hits on them are reported, the empty one is not");
        check(after.droppedDrumRows.contains(12) && after.droppedDrumRows.contains(15),
              "naming which ones");

        std::cout << "  an old project's out-of-range rows are dropped and named" << std::endl;
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL DRUM PATTERN TESTS PASSED" << std::endl;
    else
        std::cout << failures << " DRUM PATTERN TEST(S) FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
