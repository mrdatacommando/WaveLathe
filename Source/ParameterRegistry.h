// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "SynthParameters.h"
#include <juce_core/juce_core.h>
#include <vector>

namespace wavelathe
{
// A numbered list of the parameters the sequencer can record and play back.
// Automation needs to name a parameter in a way that survives being stored and
// read back on another thread, so each dial worth automating gets a stable id
// here rather than the UI holding pointers into the parameter block.
//
// IDS ARE APPENDED, NEVER INSERTED. A saved automation lane stores the id of
// the parameter it drives, so putting a new entry in the middle of this list
// would silently repoint every lane saved before it at the wrong dial.
//
// That rule was broken once, deliberately, in 0.44.0: registering the drum
// kit's other five controls per voice reordered every drum id, because the
// alternative was a second block of drum ids appended after the first and a
// voice's nine controls split across two ranges for good. Every automation
// lane and MIDI assignment saved against a drum dial before 0.44.0 points at
// the wrong dial now. The synth's sixty-three did not move.
namespace paramreg
{
struct Descriptor
{
    const char* name;
    std::atomic<float> SynthParameters::* member;
    float minValue;
    float maxValue;
    bool logarithmic; // frequencies move in octaves, not in hertz

    // Which panel section this belongs to, so a list of sixty parameters can
    // be shown the way the panel is laid out rather than as one long column.
    const char* group;

    // How many discrete positions this has, or 0 when it is continuous.
    //
    // Four parameters here are really choices wearing a dial: the engine rounds
    // them at the point of use, so a value of 1.5 was never anything but 2. A
    // host told nothing draws a ramp through numbers that can never sound, and
    // an automation curve across a filter type reads as a sweep when it is a
    // series of switches.
    int steps = 0;

    // What each position is called, or nullptr when the number says it plainly.
    // Kept beside the parameter rather than in the editor, so the panel and the
    // host cannot end up disagreeing about what step 3 is called - which they
    // would, the first time one of them gained a type the other did not.
    const char* const* stepLabels = nullptr;

    // Where the value lives, when it is not a named member of SynthParameters.
    //
    // The drum mixer's values are held in one flat array rather than as
    // a hundred and eight fields, for the reason written beside that array. So
    // a descriptor has two possible homes: `member` for the sixty-three dials
    // that are named fields, and this index into SynthParameters::drumControls
    // for the rest. Exactly one is set.
    //
    // It indexes the STORAGE, and since 0.44.0 every stored control has a
    // descriptor, so these indices run 0 to 107 with no gaps in them.
    //
    // Last in the struct on purpose. The sixty-three rows below are aggregate
    // initialised positionally, and a field added anywhere earlier would have
    // silently become one of their existing values.
    int drumControl = -1;
};

// The ceiling on registry ids, and so on automation lanes, host parameters and
// every per-parameter array in the project.
//
// It was 64 because a set of touched parameters had to fit in one uint64_t that
// the audio thread reads without a lock. That is now ParameterMask, which is as
// many words as this needs, so the number is free to move - see
// Docs/ParameterCap-128.md. Raising it does not create parameters: count() is
// still the size of the registry, and ids above it simply do not exist yet.
//
// 192 rather than 171 exactly, which is what the synth and the whole kit come
// to. The cap is a ceiling and not a count, and picking the current total for
// it would mean moving it again for the next dial. Three words of mask, and a
// lane per id on the heap - see the doc for what those actually cost.
constexpr int maxParameters = 192;

const std::vector<Descriptor>& all();
int count();

// The atomic a registry id names, wherever it lives.
//
// The one place that knows a parameter can be a named member OR a slot in the
// drum array. Everything else - the host parameters, the automation lanes, the
// editor putting a value back on a knob - asks for the value and is spared
// knowing which kind it got. Out of range returns a scratch atomic that
// nothing reads, so a stale id cannot write into the parameter block.
std::atomic<float>& valueOf(SynthParameters& params, int id);
const std::atomic<float>& valueOf(const SynthParameters& params, int id);

// Where the drum mixer's block of ids begins. Contiguous and voice-major, so
// a voice's nine controls are id + 0 to id + 8 - which is what lets the mixer
// strip and the copy into the kit both be loops.
int firstDrumParameterId();

// The id a voice's control has, or -1 when the voice does not exist.
//
// Now just the base plus project::drumControlIndex, which a call site could do
// itself. It stays a function because callers should not have to know that the
// two index spaces line up - if a tenth control ever arrives unregistered, this
// is where that is dealt with, and every caller is already asking.
int drumParameterId(int voice, project::DrumControl control);

// Whether an id is one of the kit's controls rather than one of the synth's.
//
// Asked of the descriptor rather than worked out from firstDrumParameterId,
// because the descriptor is what says where the value LIVES - and "is this a
// drum dial" has to agree with that, not with an assumption about where the
// block happens to start. False for anything out of range.
bool isDrumParameter(int id);

const char* name(int id);
const char* group(int id);

// Automation stores everything 0-1, so a lane is meaningful whatever the
// parameter's real range happens to be.
float readNormalised(const SynthParameters& params, int id);
void writeNormalised(SynthParameters& params, int id, float normalised);

// Matching a dial to its id, so the editor can record what was just moved.
int idForName(const juce::String& name);

// How many discrete positions a parameter has, or 0 when it is continuous.
int steps(int id);

// What one position is called, or nullptr when it has no name of its own.
// `step` counts from zero at minValue.
const char* stepLabel(int id, int step);
} // namespace paramreg
} // namespace wavelathe
