// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MidiMonitor.h"

namespace wavelathe
{
namespace
{
constexpr int valueBits = 14;
constexpr int numberBits = 7;
constexpr int channelBits = 4;
constexpr int kindBits = 3;

constexpr int valueShift = 0;
constexpr int numberShift = valueShift + valueBits;   // 14
constexpr int channelShift = numberShift + numberBits; // 21
constexpr int kindShift = channelShift + channelBits;  // 25
constexpr int sequenceShift = 32;

constexpr uint64_t mask(int bits) { return (uint64_t{1} << bits) - 1; }
} // namespace

uint64_t MidiMonitor::pack(Kind kind, int channel, int number, int value, uint32_t sequence)
{
    // Clamped rather than asserted: this runs on the audio thread for whatever
    // a host chooses to send, and a malformed message should cost a wrong digit
    // on a diagnostic readout, not a corrupted word that reads as some other
    // kind of message entirely.
    const uint64_t k = (uint64_t) juce::jlimit(0, (int) mask(kindBits), (int) kind);
    const uint64_t c = (uint64_t) juce::jlimit(0, (int) mask(channelBits), channel - 1);
    const uint64_t n = (uint64_t) juce::jlimit(0, (int) mask(numberBits), number);
    const uint64_t v = (uint64_t) juce::jlimit(0, (int) mask(valueBits), value);

    return (k << kindShift) | (c << channelShift) | (n << numberShift) | (v << valueShift)
           | ((uint64_t) sequence << sequenceShift);
}

MidiMonitor::Event MidiMonitor::unpack(uint64_t word)
{
    Event event;
    event.kind = (Kind) ((word >> kindShift) & mask(kindBits));
    event.channel = (int) ((word >> channelShift) & mask(channelBits)) + 1;
    event.number = (int) ((word >> numberShift) & mask(numberBits));
    event.value = (int) ((word >> valueShift) & mask(valueBits));
    event.sequence = (uint32_t) (word >> sequenceShift);
    return event;
}

void MidiMonitor::setEnabled(bool shouldBeEnabled)
{
    if (shouldBeEnabled)
    {
        // Cleared on the way in rather than on the way out, so the summary
        // printed when it is switched off describes the run that just happened
        // and is still readable afterwards.
        lastEvent.store(0);
        sequenceCounter.store(0);
        noteCount.store(0);
        controllerCount.store(0);
        pitchBendCount.store(0);
        pressureCount.store(0);
        otherCount.store(0);
    }

    enabled.store(shouldBeEnabled);
}

void MidiMonitor::capture(const juce::MidiMessage& message)
{
    if (!enabled.load(std::memory_order_relaxed))
        return;

    Kind kind = Kind::none;
    int number = 0;
    int value = 0;

    // Controllers are tested first because they are what this exists to watch,
    // and because in a VST3 they are the one kind that may simply never come.
    if (message.isController())
    {
        kind = Kind::controller;
        number = message.getControllerNumber();
        value = message.getControllerValue();
        controllerCount.fetch_add(1, std::memory_order_relaxed);
    }
    else if (message.isNoteOn())
    {
        kind = Kind::noteOn;
        number = message.getNoteNumber();
        value = message.getVelocity();
        noteCount.fetch_add(1, std::memory_order_relaxed);
    }
    else if (message.isNoteOff())
    {
        // Which takes a note-on at velocity zero with it, since that is what
        // such a message means and counting it as a note-on would have every
        // release reported as a silent keypress.
        kind = Kind::noteOff;
        number = message.getNoteNumber();
        value = message.getVelocity();
        noteCount.fetch_add(1, std::memory_order_relaxed);
    }
    else if (message.isPitchWheel())
    {
        kind = Kind::pitchBend;
        value = message.getPitchWheelValue();
        pitchBendCount.fetch_add(1, std::memory_order_relaxed);
    }
    else if (message.isChannelPressure())
    {
        kind = Kind::channelPressure;
        value = message.getChannelPressureValue();
        pressureCount.fetch_add(1, std::memory_order_relaxed);
    }
    else if (message.isAftertouch())
    {
        kind = Kind::aftertouch;
        number = message.getNoteNumber();
        value = message.getAfterTouchValue();
        pressureCount.fetch_add(1, std::memory_order_relaxed);
    }
    else if (message.isProgramChange())
    {
        kind = Kind::programChange;
        value = message.getProgramChangeNumber();
        otherCount.fetch_add(1, std::memory_order_relaxed);
    }
    else
    {
        // Clock, transport, sysex and the rest. Counted so that "nothing at
        // all arrives" stays distinguishable from "plenty arrives and none of
        // it is what we wanted", but not given a line of their own - MIDI clock
        // alone would be twenty-four messages a beat.
        otherCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    const uint32_t sequence = sequenceCounter.fetch_add(1, std::memory_order_relaxed) + 1;
    lastEvent.store(pack(kind, message.getChannel(), number, value, sequence),
                    std::memory_order_relaxed);
}

MidiMonitor::Event MidiMonitor::getLastEvent() const
{
    return unpack(lastEvent.load(std::memory_order_relaxed));
}

MidiMonitor::Counts MidiMonitor::getCounts() const
{
    Counts counts;
    counts.notes = noteCount.load(std::memory_order_relaxed);
    counts.controllers = controllerCount.load(std::memory_order_relaxed);
    counts.pitchBends = pitchBendCount.load(std::memory_order_relaxed);
    counts.pressure = pressureCount.load(std::memory_order_relaxed);
    counts.other = otherCount.load(std::memory_order_relaxed);
    return counts;
}

juce::String MidiMonitor::describe(const Event& event)
{
    const juce::String channel = " ch" + juce::String(event.channel);

    switch (event.kind)
    {
        case Kind::controller:
            return "CC " + juce::String(event.number) + channel + " = " + juce::String(event.value);

        case Kind::noteOn:
            return "Note " + juce::MidiMessage::getMidiNoteName(event.number, true, true, 3)
                   + channel + " on, vel " + juce::String(event.value);

        case Kind::noteOff:
            return "Note " + juce::MidiMessage::getMidiNoteName(event.number, true, true, 3)
                   + channel + " off";

        case Kind::pitchBend:
            // Reported as it arrived rather than as a fraction: this is here to
            // say what the host delivered, and 8192 is a number you can look
            // for in a spec.
            return "Pitch bend" + channel + " = " + juce::String(event.value) + " of 16383";

        case Kind::channelPressure:
            return "Pressure" + channel + " = " + juce::String(event.value);

        case Kind::aftertouch:
            return "Aftertouch " + juce::MidiMessage::getMidiNoteName(event.number, true, true, 3)
                   + channel + " = " + juce::String(event.value);

        case Kind::programChange:
            return "Program change" + channel + " = " + juce::String(event.value);

        case Kind::none:
        default:
            return "Nothing yet";
    }
}

juce::String MidiMonitor::describe(const Counts& counts)
{
    if (counts.total() == 0)
        return "Nothing arrived at all.";

    juce::String text;
    text << "notes " << (int) counts.notes
         << ", CC " << (int) counts.controllers
         << ", bend " << (int) counts.pitchBends
         << ", pressure " << (int) counts.pressure;

    if (counts.other > 0)
        text << ", other " << (int) counts.other;

    return text;
}
} // namespace wavelathe
