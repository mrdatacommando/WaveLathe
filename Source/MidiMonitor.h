// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <cstdint>

namespace wavelathe
{
// What MIDI actually reached the plugin, printed to the readout.
//
// Built for the VST3 port, where the question stops being rhetorical: VST3 has
// no MIDI controller message at all, and a host that never delivers one looks
// from inside processBlock exactly like a controller nobody turned. Without
// this, "the dial didn't move" is a shrug; with it, it is a number.
//
// The audio thread writes and the editor reads on its own tick. One 64-bit
// store per message rather than a queue, because the question being asked is
// "what is arriving", not "everything that ever arrived" - a knob sweep sends
// dozens of messages a second and only the newest is worth a line on a
// three-line display. The counters are what keeps the ones in between from
// being lost: they are the difference between "CC does not arrive here" and
// "nothing arrives here", which are different failures with different fixes.
//
// Deliberately not saved anywhere. A monitor still switched on from last week
// would fill the readout with traffic while you were trying to read something
// else, and a diagnostic you did not ask for is noise.
class MidiMonitor
{
public:
    enum class Kind
    {
        none = 0,
        controller,
        noteOn,
        noteOff,
        pitchBend,
        channelPressure,
        aftertouch,
        programChange
    };

    // The most recent message, unpacked.
    struct Event
    {
        Kind kind = Kind::none;
        int channel = 1;       // 1..16
        int number = 0;        // controller or note number; unused by bend and pressure
        int value = 0;         // 0..127, or 0..16383 for pitch bend
        // Bumped for every message, so turning a knob back to a value it
        // already held still reads as something having happened.
        uint32_t sequence = 0;
    };

    struct Counts
    {
        uint32_t notes = 0;
        uint32_t controllers = 0;
        uint32_t pitchBends = 0;
        uint32_t pressure = 0;
        uint32_t other = 0;

        uint32_t total() const { return notes + controllers + pitchBends + pressure + other; }
    };

    // Declared because the non-copyable macro below declares a copy constructor,
    // and any user-declared constructor takes the implicit default one away.
    MidiMonitor() = default;

    // ---- Message thread ----------------------------------------------------
    // Switching on clears what came before, so a run starts from nothing and
    // the counts at the end describe that run rather than the session.
    void setEnabled(bool shouldBeEnabled);
    bool isEnabled() const { return enabled.load(std::memory_order_relaxed); }

    Event getLastEvent() const;
    Counts getCounts() const;

    // ---- Audio thread ------------------------------------------------------
    // Atomics only, no allocation: the text is built by whoever reads it.
    // Guarded internally as well as at the call site, so the class is correct
    // on its own and the common case still costs one load for a whole block
    // rather than one per message.
    void capture(const juce::MidiMessage& message);

    // ---- Formatting, for either thread -------------------------------------
    // "CC 74 ch1 = 63" - one line of the readout.
    static juce::String describe(const Event& event);

    // "notes 12, CC 340, bend 0, pressure 0" - what arrived at all, which is
    // the question a silent test is really asking.
    static juce::String describe(const Counts& counts);

private:
    // Everything about one message in a single word, so the reader takes it
    // whole. Two atomics would need a seqlock to stop the editor pairing a new
    // controller number with an old value and printing a message that never
    // happened.
    //
    //   bits  0..13  value      (14, because pitch bend is 0..16383)
    //   bits 14..20  number     (7)
    //   bits 21..24  channel    (4, stored as channel - 1)
    //   bits 25..27  kind       (3)
    //   bits 32..63  sequence   (32)
    static uint64_t pack(Kind kind, int channel, int number, int value, uint32_t sequence);
    static Event unpack(uint64_t word);

    std::atomic<bool> enabled{false};
    std::atomic<uint64_t> lastEvent{0};
    std::atomic<uint32_t> sequenceCounter{0};

    std::atomic<uint32_t> noteCount{0};
    std::atomic<uint32_t> controllerCount{0};
    std::atomic<uint32_t> pitchBendCount{0};
    std::atomic<uint32_t> pressureCount{0};
    std::atomic<uint32_t> otherCount{0};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiMonitor)
};
} // namespace wavelathe
