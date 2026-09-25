// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <string>
#include <vector>

namespace wavelathe
{
// What a saved project is called on disk. A project is the whole of what you
// built - the synth, the sequencer pattern and the automation over it - which
// is a different thing from a preset, so it gets a name of its own.
//
// One place, so changing it changes the file chooser, the filter and the
// extension that gets added for you, all together.
namespace project
{
constexpr const char* fileExtension = "wlp";

// How finely a note length is measured, as ticks per sequencer step. Saved
// projects store lengths in these units, so the sequencer asserts that its own
// resolution still matches - change one and the other has to follow, or every
// saved length would silently mean something different.
constexpr int ticksPerStep = 16;

// How many drum voices a pattern can hold, and how long a row can be. Here
// rather than in StepSequencer because the preset reader needs them to reject a
// corrupt file, and the reader deliberately knows nothing about the sequencer -
// it reads data, and the sequencer decides what to do with it.
//
// The sequencer takes its own numDrumVoices from this, so there is one number
// and not two that have to be kept equal.
constexpr int numDrumVoices = 12;
constexpr int maxDrumSteps = 64;

// Each step splits into this many drum cells, so a hit can land between one
// beat and the next: two of them gives 32nds against the usual 1/16 rate.
//
// One named number rather than a 2 scattered through the grid, the sequencer
// and the file format. Four would give 64ths and is a one-line change here -
// at the cost of twice the cells again, and of columns too narrow to hit
// without zooming right in.
constexpr int drumSubdivisions = 2;
constexpr int maxDrumCells = maxDrumSteps * drumSubdivisions;
constexpr int ticksPerDrumCell = ticksPerStep / drumSubdivisions;

// How far a hit may be pushed off its cell, in ticks, in either direction.
//
// Signed on purpose. The melodic pattern's tickOffset only runs late, which is
// fine for placing a note inside its step but wrong for feel: a hat that sits
// fractionally EARLY is half of what makes a groove push rather than drag, and
// late-only cannot express it at all.
//
// Just under half a cell each way, so no two cells can ever claim the same
// instant. That "just under" is the whole of it: at exactly half a cell, cell N
// pushed fully early and cell N-1 pushed fully late land on the SAME tick, and
// a hit's position would no longer say which cell it came from. One tick of
// range is a cheap price for an unambiguous grid.
//
// A test asserted the property before this was written, failed, and is the
// reason the -1 is here rather than a comment claiming it was fine.
//
// At 120bpm this is about +/-23ms, which is the range this is actually for.
constexpr int maxDrumNudge = ticksPerDrumCell / 2 - 1;

// What the twelve rows are called.
//
// Here for the reason numDrumVoices is here: more than one part of the program
// needs them and none of those parts should be the owner. The grid draws them,
// the parameter registry builds forty-eight names out of them ("Kick Level"),
// and the drum kit keeps its own copy because it is a library that cannot see
// any of this - DrumPatternTest holds that copy equal to this one, name by
// name.
//
// A ROW's name, not a sound's. Since a slot chooses which drum it plays, these
// two can disagree: put a cowbell on the Rim row and the row is still called
// Rim. That is deliberate and it is the lesser of the two evils - the Sequencer
// page's grid and this page's strips have to agree about which row is which,
// and a name that moved when you changed a sound would break that agreement
// every time. The strip header tints and its tooltip says what is actually on
// it, which is where "what does this one play" is answered.
//
// The first eleven are an 808's instruments, in this kit's own order rather
// than the hardware's front-panel order - the order is what every saved
// pattern and every automation lane already means, and is not worth breaking
// to make the page read left to right like the machine.
//
// "Aux" is the twelfth: blank by default, and named for what it IS rather than
// for a sound it might be given, because it can be given any of them.
inline const char* drumVoiceName(int voice)
{
    static const char* const names[numDrumVoices] = {
        "Kick",  "Snare",  "Rim",     "Clap",
        "LoTom", "MidTom", "HiTom",   "Cowbell",
        "CHat",  "OHat",   "Crash",   "Aux"
    };

    if (voice < 0 || voice >= numDrumVoices)
        return "";

    return names[voice];
}

// ---- Which note strikes which slot -----------------------------------------
//
// General MIDI's drum map, folded onto these twelve slots. GM is what a pad
// controller sends when nobody has configured it, what a DAW's drum lane
// assumes, and what every other drum machine reads - so it is the DEFAULT,
// rather than one invented here.
//
// The default and not the last word, since 0.45.0. DrumNoteMap sits in front
// of this and answers first for any slot that has been taught its own note,
// and the kit's transpose shifts what is asked of this. Both live in MidiMap.h
// because they are about which MIDI reaches what; this stays here because the
// GM map is a fact about the standard rather than a setting.
//
// GM has more drums than this kit has rows, so several of its notes land on
// one slot: its three floor and low toms all reach LoTom, its two mid toms
// reach MidTom, and its four crashes reach Crash. That is the same folding a
// twelve-piece kit does to a GM file anyway, and it beats the alternative of
// notes that do nothing.
//
// Ride goes to the Aux slot, which is blank until somebody puts a drum on it.
// A note that lands on an empty slot makes no sound and that is correct - the
// slot is empty. Put a Ride on it, or a sample, and the note plays it.
//
// Anything not listed returns -1 and is ignored, rather than being folded onto
// some nearest slot: a kit that answers every one of the 128 notes with a
// drum would make a wrong-channel piano part sound like a falling drum rack.
constexpr int drumVoiceForNote(int note)
{
    switch (note)
    {
        case 35: case 36:                     return 0;   // Acoustic / Electric Bass Drum
        case 38: case 40:                     return 1;   // Acoustic / Electric Snare
        case 37:                              return 2;   // Side Stick -> Rim
        case 39:                              return 3;   // Hand Clap
        case 41: case 43: case 45:            return 4;   // the floor and low toms
        case 47: case 48:                     return 5;   // the two mid toms
        case 50:                              return 6;   // High Tom
        case 56:                              return 7;   // Cowbell
        case 42: case 44:                     return 8;   // Closed and Pedal Hat
        case 46:                              return 9;   // Open Hat
        case 49: case 52: case 55: case 57:   return 10;  // the crashes and splashes
        case 51: case 53: case 59:            return 11;  // the rides -> Aux
        default:                              return -1;
    }
}

// The one note to hit for a slot, out of the several the map above accepts.
//
// The map folds - three floor and low toms all reach LoTom - so "which note is
// this slot on" has no single answer from it. This is that answer: the note
// whose GM name is the slot's own name, which is the one to print on a strip
// and the one a player reaches for.
//
// It is the inverse of the function above and the two can drift apart, so
// MidiMapTest walks all twelve and checks the round trip rather than trusting
// that these numbers were copied correctly.
constexpr int drumPrimaryNoteFor(int voice)
{
    switch (voice)
    {
        case 0:  return 36;  // Bass Drum 1
        case 1:  return 38;  // Acoustic Snare
        case 2:  return 37;  // Side Stick
        case 3:  return 39;  // Hand Clap
        case 4:  return 45;  // Low Tom
        case 5:  return 47;  // Low-Mid Tom
        case 6:  return 50;  // High Tom
        case 7:  return 56;  // Cowbell
        case 8:  return 42;  // Closed Hi Hat
        case 9:  return 46;  // Open Hi Hat
        case 10: return 49;  // Crash Cymbal 1
        case 11: return 51;  // Ride Cymbal 1
        default: return -1;
    }
}

// How far the whole GM map may be shifted, in semitones.
//
// Three octaves each way, which is more than the reason for the control needs
// and costs nothing: GM's drums sit at 35-59, so +36 puts the kick at 72 and
// -36 puts it at 0, and both ends are still inside the 128 notes MIDI has.
//
// The reason for the control is an octave: a pad controller or a DAW drum lane
// that sits an octave above GM, which is common enough to be worth one number
// rather than twelve assignments. So the MENU offers octaves, and only the
// seven this range allows.
//
// Stored in semitones all the same, because that is the unit the number
// actually is - a settings file holding "12" is unambiguous where one holding
// "1" would have to be read against a convention. It also leaves a
// hand-edited odd value working rather than rejected, and DrumNoteMap is there
// for anything a shift cannot express.
constexpr int maxDrumTranspose = 36;
constexpr int maxDrumTransposeOctaves = maxDrumTranspose / 12;

// How hard a hit has to be to count as accented.
//
// The kit keeps a quarter of its range back for accent, so an un-accented hit
// at full velocity is at 0.75 and the top quarter belongs to accented ones.
// Without a threshold here a pad could never reach that quarter at all, and
// the loudest thing a player could hit would be three quarters of the loudest
// thing the kit can do.
//
// 0.9 rather than 1.0 because a pad reaching exactly 127 is a matter of luck.
constexpr float drumAccentVelocity = 0.9f;

// The nine controls every voice has, synthesised or sampled. ALL NINE are
// registered, so every one of them can be automated and MIDI learned.
//
// For two versions only four were: the kit had sixteen rows, sixteen fours is
// sixty-four, and that was exactly what raising the cap to 128 left over the
// synth's sixty-three. Level, Decay and the two send Amounts won on the
// argument that a selector picks WHICH effect a voice runs through and is set
// once while building a kit - which is a fair thing to say about automation
// and the wrong thing to say about a knob. Wanting a hand on Tune while the
// take runs is not the same wish as wanting a Tune curve drawn under it, and
// the four were chosen against the first wish using the second one's reasons.
//
// So the cap went to 192 instead and the distinction went away. Nine controls
// across twelve voices is 108, and 108 with the synth's 63 is 171. What that
// cost is written up in Docs/ParameterCap-128.md.
//
// The distinction leaving is why there is no drumParameterOffset here any
// more. A voice's controls sit at drumControlIndex in the storage array and at
// firstDrumParameterId() + that same index in the registry: one order, one
// function, and no gaps for a caller to have to know about.
enum class DrumControl
{
    level,
    tune,

    // The envelope. Attack is zero everywhere by default, because a strike is
    // instant and this is here to take an edge off one rather than to change
    // what the kit is. How far it reaches is the voice's own business - a
    // crash can swell for half a second, a rim shot has ten milliseconds of
    // anything to shape. See VoiceSpec::maxAttackSeconds.
    attack,
    decay,

    pan,

    // Which unit this slot runs the voice through, and how much of it there
    // is. Kept adjacent so a strip reads "what, and how much" in that order.
    send1,
    send1Amount,
    send2,
    send2Amount
};

constexpr int numDrumControls = 9;

inline const char* drumControlName(DrumControl control)
{
    switch (control)
    {
        case DrumControl::level:       return "Level";
        case DrumControl::tune:        return "Tune";
        case DrumControl::attack:      return "Attack";
        case DrumControl::decay:       return "Decay";
        case DrumControl::pan:         return "Pan";
        case DrumControl::send1:       return "FX 1";
        case DrumControl::send1Amount: return "Amt 1";
        case DrumControl::send2:       return "FX 2";
        case DrumControl::send2Amount: return "Amt 2";
    }

    return "";
}

// Where a voice's control sits in the flat array SynthParameters keeps them in,
// and equally where it sits inside its voice's block of registry ids. Voice-
// major, so one voice's nine controls are adjacent - which is the order a mixer
// strip reads them in, and the order the ids run in.
//
// One function for both because the two orders are now the same one. While
// four of the nine were registered they could not be: the registry had four
// slots per voice and the storage had nine, so the ids ran level-decay-amt-amt
// against storage that ran level-tune-attack-decay-pan-... and something had to
// hold the mapping between them. Nothing does now.
constexpr int drumControlIndex(int voice, DrumControl control)
{
    return voice * numDrumControls + (int) control;
}

constexpr int numDrumStoredControls = numDrumVoices * numDrumControls;

// The same number again, under the name the registry side calls it by. Both
// names are kept because the two readings are genuinely different questions -
// "how much storage does the kit need" and "how many registry ids does the kit
// take" - and they were different numbers until every control was registered.
constexpr int numDrumParameters = numDrumStoredControls;

// ---- Which unit a slot runs the voice through ------------------------------
//
// A selector, not an amount: position 0 is off and the other four name the
// units in the rack down the side of the Drums page. The amount is the dial
// beside it.
//
// The two slots are a CHAIN, not two parallel sends: a voice goes through slot
// 1's unit and then through slot 2's. So each voice needs its own copy of
// whatever unit it is running through - the rack's twelve dials are shared
// settings, not shared processors. That costs real memory and it is the price
// of the chain; a parallel send bus would be one reverb for the whole kit, and
// then "through slot 1, then slot 2" could not mean anything.
enum class DrumSend { off, reverb, distortion, eq, delay };
constexpr int numDrumSendDestinations = 5;

inline const char* drumSendName(int destination)
{
    static const char* const names[numDrumSendDestinations] = {
        "Off", "Reverb 1", "Dist", "EQ", "Delay"
    };

    if (destination < 0 || destination >= numDrumSendDestinations)
        return "Off";

    return names[destination];
}

// ---- What the four units are set to -----------------------------------------
//
// Three controls each, and they are NOT registered: the budget is full at 127
// of 128 and these are set while building a kit rather than automated across a
// track. Saved with the project, exactly as the mastering chain's twelve are
// saved with a preset.
//
// Shared settings over private processors. Every voice running through the
// reverb runs through ITS OWN reverb, set to these numbers - which is the only
// arrangement a per-voice chain allows, and which does mean two drums in
// "Reverb 1" are in two identical rooms rather than the same one.
//
// Flat and spelled out rather than a unit-times-control grid, because the
// three controls mean different things in each unit and a generic "control 2"
// would read as a mistake in three of the four.
enum class DrumFxControl
{
    reverbSize,
    reverbDamp,
    reverbMix,

    driveAmount,
    driveTone,
    driveMix,

    eqLow,
    eqHigh,
    eqLevel,

    delayTime,
    delayFeedback,
    delayMix,

    count
};

// The slowest echo the Delay unit may be set to, in quarter-note beats.
//
// A quarter note, and the reason is memory rather than taste. The chain is
// per voice, so every voice needs its own delay line; twelve of them are
// sized for one second, and one second is exactly a quarter note at 60bpm.
// Everything shorter fits at any tempo above that.
//
// Here rather than as a table index, because the table is TempoSync's and a
// number like "5" written in two places would be two places to get wrong the
// next time a division is added to it. Both the dial's range and the clamp in
// the processor ask tempo::nearestDivisionForBeats for this.
constexpr double drumDelaySlowestBeats = 1.0;

constexpr int numDrumFxControls = (int) DrumFxControl::count;

inline const char* drumFxControlName(DrumFxControl control)
{
    switch (control)
    {
        case DrumFxControl::reverbSize:    return "Size";
        case DrumFxControl::reverbDamp:    return "Damp";
        case DrumFxControl::reverbMix:     return "Level";
        case DrumFxControl::driveAmount:   return "Drive";
        case DrumFxControl::driveTone:     return "Tone";
        case DrumFxControl::driveMix:      return "Level";
        case DrumFxControl::eqLow:         return "Low";
        case DrumFxControl::eqHigh:        return "High";
        case DrumFxControl::eqLevel:       return "Level";
        case DrumFxControl::delayTime:     return "Time";
        case DrumFxControl::delayFeedback: return "Fbk";
        case DrumFxControl::delayMix:      return "Level";
        case DrumFxControl::count:         break;
    }

    return "";
}

// What an untouched rack is. One list, read by the preset reader for a project
// written before the rack existed and by a fresh synth on startup, so the two
// cannot drift.
inline float defaultDrumFxValue(DrumFxControl control)
{
    switch (control)
    {
        case DrumFxControl::reverbSize:    return 0.5f;
        case DrumFxControl::reverbDamp:    return 0.5f;
        case DrumFxControl::reverbMix:     return 0.35f;
        case DrumFxControl::driveAmount:   return 0.4f;
        case DrumFxControl::driveTone:     return 0.5f;
        case DrumFxControl::driveMix:      return 0.5f;
        case DrumFxControl::eqLow:         return 0.0f;
        case DrumFxControl::eqHigh:        return 0.0f;
        case DrumFxControl::eqLevel:       return 0.5f;

        // 1/8, which is the division a drum echo is set to more often than
        // every other value put together.
        case DrumFxControl::delayTime:     return 8.0f;
        case DrumFxControl::delayFeedback: return 0.35f;
        case DrumFxControl::delayMix:      return 0.35f;
        case DrumFxControl::count:         break;
    }

    return 0.0f;
}

// ---- The mix bus: effects over everything, before mastering ----------------
//
// A second copy of the Synth page's effects - the same seven stages, the same
// DSP class - sitting on the Master page and running on the WHOLE mix: the
// synth after its own effects, plus the drums after their sends. Then the
// mastering chain. So a reverb here puts the kit and the bassline in one room,
// which neither of their own effects can do: the synth's belong to the synth,
// and each drum's sends are its own private copies of a unit.
//
// Not registered, on purpose. A DAW has its own bus processing and would only
// be offered seventeen more rows to scroll past; these are a sketchpad for the
// mix inside WaveLathe. Saved with the PROJECT and not with a patch, for the
// reason the drum rack gives: loading "Bright Lead" is a decision about the
// synth's voice, and it has no business flattening the room the drums are in.
//
// Flat and spelled out, like DrumFxControl, and in chain order - the order the
// page lays them out in and the order the v25 block writes them in. APPEND
// ONLY, for the same reason the registry is: the file stores these by
// position.
enum class BusFxControl
{
    distortion,

    chorusRate,
    chorusDepth,
    chorusMix,

    phaserRate,
    phaserFeedback,
    phaserMix,

    delayTime,
    delayFeedback,
    delayMix,
    delaySync,

    reverbSize,
    reverbMix,

    eqLow,
    eqMid,
    eqHigh,

    // In decibels, unlike the Synth page's Gain beside its EQ - which is a
    // voice level applied per note, not a stage on the finished sound. This
    // one is the level going into the mastering chain, and a bus level wants
    // a centre that means "unchanged".
    outputGain,

    count
};

constexpr int numBusFxControls = (int) BusFxControl::count;

// What an untouched bus is: every effect dry, the EQ flat, unity gain - so a
// project saved before the bus existed, and every new one, sounds exactly as
// it would with no bus at all. The rates, times and sizes match the Synth
// page's defaults so the two sets of dials rest in the same places, but with
// every mix at zero none of them is heard until somebody turns one up.
inline float defaultBusFxValue(BusFxControl control)
{
    switch (control)
    {
        case BusFxControl::distortion:     return 0.0f;
        case BusFxControl::chorusRate:     return 0.6f;
        case BusFxControl::chorusDepth:    return 0.35f;
        case BusFxControl::chorusMix:      return 0.0f;
        case BusFxControl::phaserRate:     return 0.4f;
        case BusFxControl::phaserFeedback: return 0.4f;
        case BusFxControl::phaserMix:      return 0.0f;
        case BusFxControl::delayTime:      return 320.0f;
        case BusFxControl::delayFeedback:  return 0.35f;
        case BusFxControl::delayMix:       return 0.0f;
        case BusFxControl::delaySync:      return 0.0f;
        case BusFxControl::reverbSize:     return 0.5f;
        case BusFxControl::reverbMix:      return 0.0f;
        case BusFxControl::eqLow:          return 0.0f;
        case BusFxControl::eqMid:          return 0.0f;
        case BusFxControl::eqHigh:         return 0.0f;
        case BusFxControl::outputGain:     return 0.0f;
        case BusFxControl::count:          break;
    }

    return 0.0f;
}

// What the display and the undo history call each one. Prefixed, because a
// bare "Mix" would be one of five and "Undid Mix" says nothing.
inline const char* busFxControlName(BusFxControl control)
{
    switch (control)
    {
        case BusFxControl::distortion:     return "Bus Distortion";
        case BusFxControl::chorusRate:     return "Bus Chorus Rate";
        case BusFxControl::chorusDepth:    return "Bus Chorus Depth";
        case BusFxControl::chorusMix:      return "Bus Chorus Mix";
        case BusFxControl::phaserRate:     return "Bus Phaser Rate";
        case BusFxControl::phaserFeedback: return "Bus Phaser Feedback";
        case BusFxControl::phaserMix:      return "Bus Phaser Mix";
        case BusFxControl::delayTime:      return "Bus Delay Time";
        case BusFxControl::delayFeedback:  return "Bus Delay Feedback";
        case BusFxControl::delayMix:       return "Bus Delay Mix";
        case BusFxControl::delaySync:      return "Bus Delay Sync";
        case BusFxControl::reverbSize:     return "Bus Reverb Size";
        case BusFxControl::reverbMix:      return "Bus Reverb Mix";
        case BusFxControl::eqLow:          return "Bus EQ Low";
        case BusFxControl::eqMid:          return "Bus EQ Mid";
        case BusFxControl::eqHigh:         return "Bus EQ High";
        case BusFxControl::outputGain:     return "Bus Gain";
        case BusFxControl::count:          break;
    }

    return "";
}
// What the file choosers filter on, BUILT from fileExtension above rather than
// written out again.
//
// It was written out again, as "*.wfo", and the two drifted apart at the rename
// from WaveForge to WaveLathe: saving forced the new .wlp on the filename while
// both choosers still filtered for the old .wfo. Every project saved by the
// renamed build was written correctly and then hidden from Load Project, which
// from the outside is indistinguishable from saving not working at all.
//
// .wfo is still accepted, deliberately. There are projects on disk from before
// the rename and a tidy-up that stopped opening them would be the same bug
// again, pointed the other way.
inline const char* wildcard()
{
    static const std::string value = std::string("*.") + fileExtension + ";*.wfo";
    return value.c_str();
}
} // namespace project

// The sequencer's contents as plain data.
//
// Saving a project means saving what you built, not only how the synth is set
// up - a pattern and the automation drawn over it are most of the work. This
// carries them in a form the preset format can write without needing to know
// anything about how the sequencer runs them, and without the sequencer
// needing to know about files.
struct SequencerState
{
    struct StepData
    {
        int active = 0;
        int degree = 0;
        int octave = 0;
        int length = 1;
        int tickOffset = 0;
        int locked = 0;
        float velocity = 0.85f;
        float gate = 0.9f;
        float probability = 1.0f;
        int slide = 0;
        int accent = 0;
    };

    struct LaneData
    {
        int parameterId = -1;
        std::vector<float> values;
    };

    // One cell of a drum voice's row. Deliberately a smaller vocabulary than
    // StepData: a drum hit is on or off, and the only things that vary are how
    // hard, how often, whether it is accented and how tightly it sits. No
    // degree, no octave, no length, no slide - a kick has no pitch to slide to,
    // and a drum voice rings for as long as its own decay says.
    struct DrumCellData
    {
        int active = 0;
        float velocity = 0.85f;
        float probability = 1.0f;
        int accent = 0;

        // Ticks off the cell's own position, either way. Zero is dead on it.
        int nudge = 0;
    };

    // Saved per voice rather than as a whole block, so a pattern using two
    // voices costs two rows rather than fourteen empty ones. The same reason
    // lanes are sparse.
    struct DrumVoiceData
    {
        int voice = -1;
        std::vector<DrumCellData> cells;
    };

    std::vector<StepData> steps;
    std::vector<LaneData> lanes;
    std::vector<DrumVoiceData> drumVoices;

    // A project saved before the sequencer was part of the format, or one with
    // nothing in it, leaves whatever is loaded alone rather than wiping it.
    //
    // Drums count. A project that is only a beat - no melodic steps, no
    // automation - is not an empty project, and treating it as one would load
    // it as whatever happened to be in the sequencer already.
    bool isEmpty() const { return steps.empty() && lanes.empty() && drumVoices.empty(); }
};
} // namespace wavelathe
