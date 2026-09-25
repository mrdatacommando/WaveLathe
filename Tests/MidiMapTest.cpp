// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <juce_core/juce_core.h>
#include "MidiMap.h"
#include "ParameterRegistry.h"
#include "MidiMonitor.h"
#include <cstdio>
#include <cmath>

using namespace wavelathe;

namespace
{
int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition)
        ++failures;
}

bool nearly(float a, float b) { return std::abs(a - b) < 0.001f; }

// A controller arriving from outside, which is the only way the map is ever
// driven in anger.
bool send(MidiMap& map, int cc, int value, SynthParameters& params)
{
    return map.handleController(cc, value, params);
}
} // namespace

int main()
{
    const int cutoff = paramreg::idForName("Cutoff");
    const int reso = paramreg::idForName("Reso");
    const int accent = paramreg::idForName("Accent");

    std::printf("Registry: %d parameters\n\n", paramreg::count());
    check(cutoff >= 0 && reso >= 0 && accent >= 0, "the parameters this suite uses all exist");

    // ---- An unmapped controller does nothing -------------------------------
    std::printf("\nAn unmapped controller:\n");
    {
        MidiMap map;
        SynthParameters params;
        float before = params.filterCutoffHz.load();

        check(!send(map, 74, 100, params), "reports that it did nothing");
        check(nearly(params.filterCutoffHz.load(), before), "and changes nothing");
        check(map.countAssignments() == 0, "an empty map holds no assignments");
    }

    // ---- Learning ----------------------------------------------------------
    std::printf("\nLearning:\n");
    {
        MidiMap map;
        SynthParameters params;

        map.armLearn(cutoff);
        check(map.isLearning(), "arming says so");

        check(send(map, 74, 127, params), "the next controller binds and applies");
        check(!map.isLearning(), "and disarms itself");
        check(map.parameterFor(74) == cutoff, "CC 74 now drives Cutoff");
        check(map.controllerFor(cutoff) == 74, "and Cutoff reports CC 74");

        // Applying on the bind is what tells you it took: the dial jumps to
        // where the hardware already is.
        check(params.filterCutoffHz.load() > 19000.0f, "full travel on the bind reaches the top");

        send(map, 74, 0, params);
        check(params.filterCutoffHz.load() < 25.0f, "and zero reaches the bottom");
    }

    // ---- Reassigning the same controller elsewhere -------------------------
    // The whole point: pick the same knob up again and point it at something
    // else, without having to unpick what it used to do.
    std::printf("\nReassigning one controller:\n");
    {
        MidiMap map;
        SynthParameters params;

        map.armLearn(cutoff);
        send(map, 74, 64, params);

        map.armLearn(reso);
        send(map, 74, 64, params);

        check(map.parameterFor(74) == reso, "CC 74 moved to Reso");
        check(map.controllerFor(cutoff) == MidiMap::none, "and Cutoff is no longer driven");
        check(map.countAssignments() == 1, "one controller is still one assignment");

        float held = params.filterCutoffHz.load();
        send(map, 74, 0, params);
        check(nearly(params.filterCutoffHz.load(), held), "the dial it left behind stops moving");
    }

    // ---- One parameter, two controllers ------------------------------------
    // Allowed, and the convention elsewhere: a fader and a knob can both reach
    // the same dial, even though one controller only ever reaches one dial.
    std::printf("\nTwo controllers on one parameter:\n");
    {
        MidiMap map;
        SynthParameters params;

        map.armLearn(accent);
        send(map, 20, 0, params);
        map.armLearn(accent);
        send(map, 21, 127, params);

        check(map.parameterFor(20) == accent && map.parameterFor(21) == accent,
              "both controllers drive it");
        check(map.countAssignments() == 2, "and both are counted");

        send(map, 20, 0, params);
        check(nearly(params.accentAmount.load(), 0.0f), "the first one still moves it");
        send(map, 21, 127, params);
        check(nearly(params.accentAmount.load(), 1.0f), "and so does the second");
    }

    // ---- Clearing ----------------------------------------------------------
    std::printf("\nClearing:\n");
    {
        MidiMap map;
        SynthParameters params;

        map.armLearn(cutoff);
        send(map, 74, 64, params);
        map.clearController(74);
        check(map.parameterFor(74) == MidiMap::none, "a cleared controller is unmapped");
        check(!send(map, 74, 127, params), "and stops doing anything");

        map.armLearn(accent);
        send(map, 20, 64, params);
        map.armLearn(accent);
        send(map, 21, 64, params);
        map.clearParameter(accent);
        check(map.countAssignments() == 0, "clearing a parameter clears every controller on it");

        map.armLearn(reso);
        send(map, 30, 64, params);
        map.clearAll();
        check(map.countAssignments() == 0, "clear all empties the map");
        check(!map.isLearning(), "and cancels anything armed");
    }

    // ---- Arming twice ------------------------------------------------------
    // Clicking the wrong dial and then the right one must not leave the wrong
    // one armed, or the next controller binds to something already given up on.
    std::printf("\nArming twice:\n");
    {
        MidiMap map;
        SynthParameters params;

        map.armLearn(cutoff);
        map.armLearn(reso);
        send(map, 74, 64, params);

        check(map.parameterFor(74) == reso, "the second choice wins");
        check(map.controllerFor(cutoff) == MidiMap::none, "the first is not bound at all");
    }

    // ---- A cancelled learn binds nothing ------------------------------------
    std::printf("\nCancelling:\n");
    {
        MidiMap map;
        SynthParameters params;

        map.armLearn(cutoff);
        map.cancelLearn();
        send(map, 74, 64, params);

        check(map.countAssignments() == 0, "a cancelled learn leaves the map empty");
    }

    // ---- Round trip through text -------------------------------------------
    std::printf("\nSaving and restoring:\n");
    {
        MidiMap map;
        SynthParameters params;

        map.armLearn(cutoff);
        send(map, 74, 64, params);
        map.armLearn(reso);
        send(map, 71, 64, params);
        map.armLearn(accent);
        send(map, 1, 64, params);

        auto text = map.toString();
        std::printf("  saved as: %s\n", text.toRawUTF8());

        MidiMap restored;
        restored.fromString(text);

        check(restored.parameterFor(74) == cutoff, "Cutoff comes back on CC 74");
        check(restored.parameterFor(71) == reso, "Reso on CC 71");
        check(restored.parameterFor(1) == accent, "and a wheel assignment survives too");
        check(restored.countAssignments() == 3, "with nothing else invented");

        // Written by name, so the text says what it means and a renumbered
        // registry could never repoint it at the wrong dial.
        check(text.contains("Cutoff"), "the text names parameters rather than numbering them");
    }

    // ---- Rubbish in --------------------------------------------------------
    std::printf("\nUnreadable text:\n");
    {
        MidiMap map;
        map.fromString("999:Reso,74:NotAParameter,nonsense,-3:Gain,12:,,");

        check(map.countAssignments() == 0, "nothing unrecognised or out of range gets in");

        MidiMap empty;
        empty.fromString("");
        check(empty.countAssignments() == 0, "empty text is an empty map");

        // Reading replaces rather than merges, or a map loaded over another
        // would be the two of them at once.
        MidiMap replaced;
        SynthParameters params;
        replaced.armLearn(cutoff);
        send(replaced, 74, 64, params);
        replaced.fromString("71:Reso");
        check(replaced.parameterFor(74) == MidiMap::none, "and reading a map replaces the old one");
        check(replaced.countAssignments() == 1, "leaving only what was read");
    }

    // ---- Out of range ------------------------------------------------------
    std::printf("\nOut of range:\n");
    {
        MidiMap map;
        SynthParameters params;

        map.assign(200, 0);
        map.assign(-1, 0);
        map.assign(5, paramreg::count() + 10);
        check(map.countAssignments() == 0, "nothing out of range is ever stored");

        check(!send(map, 200, 64, params), "a controller number that cannot exist does nothing");

        map.armLearn(paramreg::count() + 10);
        check(!map.isLearning(), "arming a parameter that does not exist arms nothing");
    }

    // ---- Every parameter is reachable --------------------------------------
    // A dial with an id that a controller cannot drive would be a hole in the
    // panel that only shows up when someone tries to learn it.
    std::printf("\nReach:\n");
    {
        MidiMap map;
        SynthParameters params;
        bool allReachable = true;

        for (int id = 0; id < paramreg::count(); ++id)
        {
            map.armLearn(id);

            // Modulo the ASSIGNABLE controllers, not all 128. This used to be
            // % numControllers, which was the same thing until the registry
            // passed 120 parameters: ids 120 upward then landed on CC 120-127,
            // which are Channel Mode messages and which the map refuses on
            // purpose. Seven drum controls "could not be learned" and the
            // product was right - the test was asking for a controller that
            // does not exist.
            int cc = id % MidiMap::firstChannelModeController;

            if (!send(map, cc, 127, params) || map.parameterFor(cc) != id)
            {
                std::printf("  %s could not be learned\n", paramreg::name(id));
                allReachable = false;
            }

            map.clearAll();
        }

        check(allReachable, "every parameter in the registry can be learned and driven");

        // Individually, which is what the loop proves. Not all at once: a
        // controller number is one of 120, and the registry passed that when
        // the drum mixer landed. Worth printing rather than asserting - it is
        // a fact about MIDI, not a fault in anything here - but worth printing,
        // because "I ran out of CCs" is otherwise a confusing thing to hit.
        std::printf("  %d parameters against %d assignable controllers%s\n",
                    paramreg::count(), MidiMap::firstChannelModeController,
                    paramreg::count() > MidiMap::firstChannelModeController
                        ? " - a single map cannot reach them all at once" : "");
    }


    // ---- Notes from one channel only ---------------------------------------
    // The other half of the problem: a controller whose knobs you want and
    // whose keys you do not. Narrowing the keyboard has to silence the keys
    // without disconnecting the knobs.
    std::printf("\nChannel filtering:\n");
    {
        auto build = []
        {
            juce::MidiBuffer buffer;
            buffer.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100), 0);
            buffer.addEvent(juce::MidiMessage::noteOn(2, 64, (juce::uint8) 100), 10);
            buffer.addEvent(juce::MidiMessage::controllerEvent(1, 74, 100), 20);
            buffer.addEvent(juce::MidiMessage::controllerEvent(2, 71, 100), 25);
            buffer.addEvent(juce::MidiMessage::noteOff(1, 60), 30);
            buffer.addEvent(juce::MidiMessage::noteOff(2, 64), 40);
            buffer.addEvent(juce::MidiMessage::pitchWheel(1, 4000), 45);
            return buffer;
        };

        auto count = [](const juce::MidiBuffer& buffer, bool wantNotes, int channel)
        {
            int found = 0;
            for (const auto metadata : buffer)
            {
                auto m = metadata.getMessage();
                if (m.isNoteOnOrOff() == wantNotes && (channel == 0 || m.getChannel() == channel))
                    ++found;
            }
            return found;
        };

        auto omni = build();
        filterNotesToChannel(omni, 0);
        check(count(omni, true, 0) == 4, "omni keeps every note");
        check(count(omni, false, 0) == 3, "and everything that is not a note");

        auto one = build();
        filterNotesToChannel(one, 1);
        check(count(one, true, 1) == 2, "channel 1 keeps its own notes");
        check(count(one, true, 2) == 0, "and drops the other channel's");

        // The point of the whole feature: knobs keep working. Named by the
        // exact message rather than by a count, because "how many survived"
        // says nothing about WHICH, and which is the whole question here.
        check(count(one, false, 0) == 3, "while every controller message passes");

        bool keptTheOtherChannelsKnob = false;
        for (const auto metadata : one)
        {
            auto m = metadata.getMessage();
            if (m.isController() && m.getChannel() == 2 && m.getControllerNumber() == 71)
                keptTheOtherChannelsKnob = true;
        }

        check(keptTheOtherChannelsKnob,
              "including the knob on the channel whose notes were dropped");

        auto two = build();
        filterNotesToChannel(two, 2);
        check(count(two, true, 2) == 2, "channel 2 keeps its own notes");
        check(count(two, true, 1) == 0, "and drops channel 1's");

        // A release kept without its note-on would tell the voices to let go of
        // something they were never holding.
        int noteOns = 0, noteOffs = 0;
        for (const auto metadata : two)
        {
            auto m = metadata.getMessage();
            if (m.isNoteOn())
                ++noteOns;
            if (m.isNoteOff())
                ++noteOffs;
        }
        check(noteOns == noteOffs, "note-ons and note-offs are dropped in pairs");

        // Timing has to survive, or a filtered buffer plays at the wrong moment.
        auto timed = build();
        filterNotesToChannel(timed, 1);
        bool positionsHeld = false;
        for (const auto metadata : timed)
            if (metadata.getMessage().isNoteOn() && metadata.samplePosition == 0)
                positionsHeld = true;
        check(positionsHeld, "and what survives keeps its position in the block");

        // Out of range must be a no-op, not an empty buffer.
        auto silly = build();
        filterNotesToChannel(silly, 99);
        check(count(silly, true, 0) == 4, "a channel that cannot exist changes nothing");
    }



    // ---- Channel Mode messages are not controls ----------------------------
    // Found in Live, and only findable there: nothing sends CC 123 from a knob,
    // but a HOST sends it every time the transport stops. The monitor caught it
    // arriving on the readout beside a real controller.
    std::printf("\nControllers 120-127, which are not controllers:\n");
    {
        MidiMap map;
        SynthParameters params;

        map.armLearn(cutoff);
        check(!send(map, 123, 0, params), "All Notes Off does nothing");
        check(map.controllerFor(cutoff) == MidiMap::none, "and binds to nothing");

        // The one that matters. The learn is taken with an exchange at the top
        // of handleController, so a reserved controller reaching that line would
        // consume the arming and leave the dial pointed at All Notes Off - or,
        // refused later, silently disarm a learn still waiting for the knob you
        // actually meant to move.
        check(map.isLearning(), "and leaves the learn armed, still waiting");

        check(send(map, 74, 100, params), "so the control you did mean still binds");
        check(map.controllerFor(cutoff) == 74, "to the right dial");
    }

    std::printf("\nThe whole reserved range, not just the one Live sent:\n");
    {
        MidiMap map;
        SynthParameters params;

        for (int cc = 120; cc <= 127; ++cc)
        {
            map.armLearn(reso);
            check(!send(map, cc, 64, params),
                  cc == 120 ? "120 (All Sound Off) is refused" : "and so is the rest of 121-127");
        }

        check(map.countAssignments() == 0, "none of the eight got into the map");

        // 119 is the last real one, and undefined rather than reserved - which
        // is exactly the kind of number Live turned out to send happily.
        map.armLearn(reso);
        check(send(map, 119, 64, params), "119 still works, being a control");
    }

    std::printf("\nAnd they cannot get in by the back door:\n");
    {
        MidiMap map;
        map.assign(123, 0);
        check(map.countAssignments() == 0, "assigning one directly does nothing");

        map.fromString("123:Cutoff,74:Reso");
        check(map.parameterFor(123) == MidiMap::none, "a saved map cannot carry one back");
        check(map.parameterFor(74) == paramreg::idForName("Reso"),
              "while everything beside it loads normally");
    }
    // ---- The monitor -------------------------------------------------------
    // The probe that stage 01 of the VST3 plan is built around. It has to be
    // right about a known case before it is evidence about an unknown one, and
    // "the host sent no CC" and "our monitor cannot see CC" look identical from
    // the readout - so the packing, the classification and the counting are all
    // checked here rather than trusted.
    std::printf("\nThe MIDI monitor, switched off:\n");
    {
        MidiMonitor monitor;
        check(!monitor.isEnabled(), "starts off, so nothing is watched until asked");

        monitor.capture(juce::MidiMessage::controllerEvent(1, 74, 100));
        check(monitor.getCounts().total() == 0, "and captures nothing while it is off");
        check(monitor.getLastEvent().kind == MidiMonitor::Kind::none, "with no last event either");
    }

    std::printf("\nA controller, which is what it is for:\n");
    {
        MidiMonitor monitor;
        monitor.setEnabled(true);
        monitor.capture(juce::MidiMessage::controllerEvent(1, 74, 63));

        auto event = monitor.getLastEvent();
        check(event.kind == MidiMonitor::Kind::controller, "arrives as a controller");
        check(event.number == 74, "with its number");
        check(event.value == 63, "and its value");
        check(event.channel == 1, "and its channel");
        check(monitor.getCounts().controllers == 1, "counted once");
        check(MidiMonitor::describe(event) == "CC 74 ch1 = 63", "and reads as the plan says it should");
    }

    std::printf("\nThe edges of every field, because a packed word loses them quietly:\n");
    {
        MidiMonitor monitor;
        monitor.setEnabled(true);

        monitor.capture(juce::MidiMessage::controllerEvent(16, 127, 127));
        auto high = monitor.getLastEvent();
        check(high.channel == 16, "channel 16 survives four bits");
        check(high.number == 127, "controller 127 survives seven");
        check(high.value == 127, "value 127 survives");

        monitor.capture(juce::MidiMessage::controllerEvent(1, 0, 0));
        auto low = monitor.getLastEvent();
        check(low.channel == 1 && low.number == 0 && low.value == 0, "and so does all-zero");

        // Fourteen bits exist for exactly this message and nothing else.
        monitor.capture(juce::MidiMessage::pitchWheel(1, 16383));
        auto bend = monitor.getLastEvent();
        check(bend.kind == MidiMonitor::Kind::pitchBend, "pitch bend arrives as pitch bend");
        check(bend.value == 16383, "at full travel, which is what needs the fourteenth bit");
    }

    std::printf("\nTelling one message from another:\n");
    {
        MidiMonitor monitor;
        monitor.setEnabled(true);

        monitor.capture(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 100));
        check(monitor.getLastEvent().kind == MidiMonitor::Kind::noteOn, "a note on is a note on");
        check(monitor.getLastEvent().value == 100, "carrying its velocity");

        monitor.capture(juce::MidiMessage::noteOff(1, 60));
        check(monitor.getLastEvent().kind == MidiMonitor::Kind::noteOff, "a note off is a note off");

        // The one that catches people out: a note-on at velocity zero IS a
        // release, and counting it as a keypress would report every note twice.
        monitor.capture(juce::MidiMessage::noteOn(1, 60, (juce::uint8) 0));
        check(monitor.getLastEvent().kind == MidiMonitor::Kind::noteOff,
              "and so is a note on at velocity zero");

        monitor.capture(juce::MidiMessage::channelPressureChange(1, 64));
        check(monitor.getLastEvent().kind == MidiMonitor::Kind::channelPressure, "pressure is pressure");

        monitor.capture(juce::MidiMessage::aftertouchChange(1, 60, 64));
        check(monitor.getLastEvent().kind == MidiMonitor::Kind::aftertouch, "and aftertouch is its own thing");

        check(monitor.getCounts().notes == 3, "three note messages counted");
        check(monitor.getCounts().controllers == 0, "and no controllers, which is the point");
    }

    std::printf("\nCounting, which is what a silent test actually reports:\n");
    {
        MidiMonitor monitor;
        monitor.setEnabled(true);

        for (int i = 0; i < 40; ++i)
            monitor.capture(juce::MidiMessage::controllerEvent(1, 74, i));

        check(monitor.getLastEvent().value == 39, "a sweep leaves only its newest value on the readout");

        for (int i = 0; i < 5; ++i)
            monitor.capture(juce::MidiMessage::noteOn(1, 60 + i, (juce::uint8) 100));

        auto counts = monitor.getCounts();
        check(counts.controllers == 40, "though the whole sweep is counted");
        check(counts.notes == 5, "beside the notes");
        check(monitor.getLastEvent().kind == MidiMonitor::Kind::noteOn,
              "and the newest is the newest whatever kind it is");

        // The two readings stage 03 is really after, in the two shapes they
        // come in.
        check(MidiMonitor::describe(counts) == "notes 5, CC 40, bend 0, pressure 0",
              "the summary names each kind");

        MidiMonitor::Counts nothing;
        check(MidiMonitor::describe(nothing) == "Nothing arrived at all.",
              "and says so plainly when none of it came");
    }

    std::printf("\nClock and the rest are counted, not printed:\n");
    {
        MidiMonitor monitor;
        monitor.setEnabled(true);
        monitor.capture(juce::MidiMessage::controllerEvent(1, 74, 10));
        monitor.capture(juce::MidiMessage::midiClock());
        monitor.capture(juce::MidiMessage::midiClock());

        check(monitor.getCounts().other == 2, "clock is counted");
        check(monitor.getLastEvent().kind == MidiMonitor::Kind::controller,
              "and does not push the controller off the readout");
    }

    std::printf("\nEvery message is new, even a repeated one:\n");
    {
        MidiMonitor monitor;
        monitor.setEnabled(true);

        monitor.capture(juce::MidiMessage::controllerEvent(1, 74, 64));
        uint32_t first = monitor.getLastEvent().sequence;

        monitor.capture(juce::MidiMessage::controllerEvent(1, 74, 64));
        uint32_t second = monitor.getLastEvent().sequence;

        check(second > first, "the same value twice still moves the sequence on");
        check(first > 0, "which starts above zero, so the first message is not mistaken for none");
    }

    std::printf("\nSwitching it on clears the last run:\n");
    {
        MidiMonitor monitor;
        monitor.setEnabled(true);
        monitor.capture(juce::MidiMessage::controllerEvent(1, 74, 64));
        monitor.setEnabled(false);

        check(monitor.getCounts().controllers == 1, "the count survives being switched off, to be read");

        monitor.setEnabled(true);
        check(monitor.getCounts().total() == 0, "and is gone when the next run starts");
        check(monitor.getLastEvent().kind == MidiMonitor::Kind::none, "along with the last event");
    }

    // ---- Splitting the pads from the keyboard ------------------------------
    //
    // Two instruments on two channels. What makes this worth testing rather
    // than reading is that every failure is SILENT: the wrong order leaves the
    // kit mute, a missed removal layers a bass note under every kick, and both
    // look like a settings problem rather than a bug.
    std::printf("\nSplitting the drum channel from the keyboard:\n");
    {
        const auto buildBuffer = []
        {
            juce::MidiBuffer buffer;
            buffer.addEvent(juce::MidiMessage::noteOn(10, 36, 1.0f), 0);      // a kick, on 10
            buffer.addEvent(juce::MidiMessage::noteOn(1, 60, 0.8f), 64);      // middle C, on 1
            buffer.addEvent(juce::MidiMessage::noteOff(10, 36), 128);         // the kick released
            buffer.addEvent(juce::MidiMessage::controllerEvent(10, 74, 100), 192);
            buffer.addEvent(juce::MidiMessage::noteOn(10, 42, 0.5f), 200);    // a closed hat
            return buffer;
        };

        auto buffer = buildBuffer();
        const auto struck = splitIncomingNotes(buffer, 10, 1);

        check(struck.size() == 2, "two note-ons on the drum channel come back as two hits");

        if (struck.size() == 2)
        {
            check(struck[0].note == 36 && struck[1].note == 42, "with the notes that were played");
            check(struck[0].sampleOffset == 0 && struck[1].sampleOffset == 200,
                  "and where in the block they landed, so a pad is not quantised to the block");
            check(std::abs(struck[1].velocity - 0.5f) < 0.02f, "and how hard they were hit");
        }

        // What is left is the keyboard's.
        int notesLeft = 0;
        int controllersLeft = 0;
        bool anyOnTen = false;

        for (const auto metadata : buffer)
        {
            const auto message = metadata.getMessage();

            if (message.isNoteOnOrOff())
            {
                ++notesLeft;
                if (message.getChannel() == 10)
                    anyOnTen = true;
            }
            else if (message.isController())
            {
                ++controllersLeft;
            }
        }

        check(notesLeft == 1, "the keyboard is left with only its own note");
        check(!anyOnTen, "and nothing from the drum channel, note-offs included");
        check(controllersLeft == 1,
              "while a controller on the drum channel passes through - a knob is a knob");
    }

    {
        // Both set to the same channel is a SPLIT, not a layer: the kit takes
        // the notes and the keyboard is left with none. The alternative -
        // playing both - would put a bass note under every kick.
        juce::MidiBuffer buffer;
        buffer.addEvent(juce::MidiMessage::noteOn(10, 36, 1.0f), 0);

        const auto struck = splitIncomingNotes(buffer, 10, 10);

        check(struck.size() == 1, "on one shared channel the kit gets the note");
        check(buffer.isEmpty(), "and the keyboard gets nothing, rather than both sounding");
    }

    {
        // Omni keyboard: hears everything EXCEPT what the kit has taken.
        juce::MidiBuffer buffer;
        buffer.addEvent(juce::MidiMessage::noteOn(10, 36, 1.0f), 0);
        buffer.addEvent(juce::MidiMessage::noteOn(3, 60, 1.0f), 0);

        const auto struck = splitIncomingNotes(buffer, 10, 0);

        check(struck.size() == 1, "with an omni keyboard the kit still takes its channel");
        check(buffer.getNumEvents() == 1, "and the keyboard hears every other one");
    }

    {
        // Off means off: the buffer is untouched apart from the keyboard's own
        // filter, which is exactly how this behaved before the kit could be
        // played at all.
        juce::MidiBuffer buffer;
        buffer.addEvent(juce::MidiMessage::noteOn(10, 36, 1.0f), 0);
        buffer.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0);

        const auto struck = splitIncomingNotes(buffer, 0, 0);

        check(struck.empty(), "with the drum channel off nothing is taken for the kit");
        check(buffer.getNumEvents() == 2, "and an omni keyboard still hears the lot");
    }

    // ---- The General MIDI note map -----------------------------------------
    std::printf("\nThe General MIDI drum map:\n");
    {
        using namespace wavelathe::project;

        check(drumVoiceForNote(36) == 0 && drumVoiceForNote(35) == 0,
              "both GM bass drums reach the Kick");
        check(drumVoiceForNote(38) == 1 && drumVoiceForNote(40) == 1,
              "both snares reach the Snare");
        check(drumVoiceForNote(42) == 8 && drumVoiceForNote(44) == 8,
              "closed and pedal hats reach the closed hat");
        check(drumVoiceForNote(46) == 9, "and the open hat is its own");

        // GM has more drums than this kit has rows, so several notes fold onto
        // one slot. Checked rather than assumed, because a fold that landed on
        // the WRONG slot would play a tom where a crash was written.
        check(drumVoiceForNote(41) == 4 && drumVoiceForNote(43) == 4 && drumVoiceForNote(45) == 4,
              "the three low toms fold onto LoTom");
        check(drumVoiceForNote(49) == 10 && drumVoiceForNote(57) == 10,
              "and the crashes onto Crash");

        // Every note must land on a slot this kit actually has, or a pad would
        // reach past the end of the parameter array.
        bool allInRange = true;
        int mapped = 0;

        for (int note = 0; note < 128; ++note)
        {
            const auto voice = drumVoiceForNote(note);

            if (voice == -1)
                continue;

            ++mapped;

            if (voice < 0 || voice >= numDrumVoices)
                allInRange = false;
        }

        check(allInRange, "every mapped note lands on a slot that exists");
        check(drumVoiceForNote(60) == -1,
              "and middle C is not a drum - an unmapped note is ignored, not folded");
        std::printf("  %d of the 128 notes strike a drum\n", mapped);

        // drumPrimaryNoteFor is the inverse of the map above and is a second
        // hand-written table, so the two can disagree. They agree here or the
        // strip prints a note that does not play it.
        bool inverseHolds = true;
        bool allDistinct = true;

        for (int voice = 0; voice < numDrumVoices; ++voice)
        {
            const auto primary = drumPrimaryNoteFor(voice);

            if (primary < 0 || drumVoiceForNote(primary) != voice)
                inverseHolds = false;

            for (int other = voice + 1; other < numDrumVoices; ++other)
                if (drumPrimaryNoteFor(other) == primary)
                    allDistinct = false;
        }

        check(inverseHolds, "every slot's primary note plays that slot and no other");
        check(allDistinct, "and no two slots claim the same primary note");
    }

    // ---- Where the kit listens ---------------------------------------------
    //
    // The transpose and the per-slot learn together. Both answer "the pad I
    // have is not the note GM says", and they answer it at different grains -
    // one moves the whole map, the other moves one slot - so the interesting
    // checks are the ones where they meet.
    std::printf("\nTranspose and learned pads:\n");
    {
        using namespace wavelathe::project;
        using wavelathe::DrumNoteMap;
        using wavelathe::drumTriggerNote;

        DrumNoteMap map;

        check(map.countAssignments() == 0, "a fresh map has taught nothing");
        check(map.voiceForNote(36, 0) == 0, "so GM still plays: 36 is the Kick");
        check(drumTriggerNote(map, 0, 0) == 36, "and the Kick prints as 36");

        // An octave up, which is the thing this was built for.
        check(map.voiceForNote(48, 12) == 0, "shifted +12, the Kick answers to 48");
        check(map.voiceForNote(36, 12) == -1,
              "and 36 is nothing any more - shifted back it is 24, which GM does not use");
        check(drumTriggerNote(map, 0, 12) == 48, "the Kick prints as 48");

        // The fold survives a shift. Three low toms onto LoTom, an octave up.
        check(map.voiceForNote(41 + 12, 12) == 4 && map.voiceForNote(45 + 12, 12) == 4,
              "the low toms still fold onto LoTom when the map is shifted");

        // Down as well as up, and the ends clamp rather than wrapping. A note
        // that shifts below zero is nothing, not note 127.
        check(map.voiceForNote(24, -12) == 0, "shifted -12, the Kick answers to 24");
        check(map.voiceForNote(0, 36) == -1, "and a note that shifts past the bottom is nothing");

        // ---- Learning ------------------------------------------------------
        map.assign(0, 60);

        check(map.countAssignments() == 1, "teaching a slot counts as one assignment");
        check(map.voiceForNote(60, 0) == 0, "the Kick answers to the note it was taught");
        check(drumTriggerNote(map, 0, 0) == 60, "and prints it");

        // The taught slot comes OFF the GM map. Without this the Kick would
        // answer both 60 and 36, and "which pad is this drum on" would have two
        // answers for every slot anybody had touched.
        check(map.voiceForNote(36, 0) == -1, "and no longer answers to GM's 36");
        check(map.voiceForNote(38, 0) == 1, "while every untaught slot is still on GM");

        // A learned note is absolute: it was learned by playing a pad, and
        // shifting it afterwards would move the slot off that pad.
        check(map.voiceForNote(60, 12) == 0, "a taught note ignores the transpose");
        check(drumTriggerNote(map, 0, 12) == 60, "and prints the same whatever the shift");
        check(drumTriggerNote(map, 1, 12) == 50,
              "while an untaught slot beside it still follows the shift");

        // One note reaches one drum. Teaching 60 to the Snare takes it off the
        // Kick rather than leaving both answering.
        map.assign(1, 60);

        check(map.voiceForNote(60, 0) == 1, "teaching a note to a second slot moves it");
        check(map.noteFor(0) == DrumNoteMap::none, "and takes it off the first");
        check(map.countAssignments() == 1, "so the count does not grow");
        check(map.voiceForNote(36, 0) == 0, "the first slot is back on GM");

        map.clearVoice(1);
        check(map.countAssignments() == 0 && map.voiceForNote(38, 0) == 1,
              "and clearing a slot puts it back on GM too");

        // ---- The armed learn -----------------------------------------------
        check(!map.isLearning(), "nothing is armed to begin with");
        check(!map.captureLearn(64), "so a note is not captured");

        map.armLearn(3);
        check(map.isLearning() && map.getLearnTarget() == 3, "arming names the slot");
        check(map.captureLearn(64), "the next note is taken");
        check(!map.isLearning(), "which disarms it");
        check(map.voiceForNote(64, 0) == 3, "and the Clap answers to 64");
        check(!map.captureLearn(65), "a second note is not taken - one arm, one note");

        map.armLearn(5);
        map.cancelLearn();
        check(!map.isLearning() && !map.captureLearn(66), "a cancelled learn takes nothing");
        check(map.voiceForNote(66, 0) == -1, "and 66 is still nobody's");

        // ---- The settings line ---------------------------------------------
        map.clearAll();
        map.assign(0, 60);
        map.assign(11, 24);

        const auto text = map.toString();

        DrumNoteMap restored;
        restored.fromString(text);

        std::printf("  the settings line reads \"%s\"\n", text.toRawUTF8());

        check(restored.noteFor(0) == 60 && restored.noteFor(11) == 24,
              "a map survives the settings line");
        check(restored.countAssignments() == 2, "with nothing else in it");

        // By slot NAME, so a file written before the rows were reordered still
        // puts the Cowbell's pad on the Cowbell.
        check(text.contains("Kick:60") && text.contains("Aux:24"),
              "written by name rather than by row number");

        DrumNoteMap junk;
        junk.fromString("Kick:60,Nonesuch:70,Snare:999,,Rim");

        check(junk.noteFor(0) == 60, "a readable pair is kept");
        check(junk.countAssignments() == 1,
              "and an unknown slot, an impossible note and a malformed pair are dropped");

        // A hand-edited file can say two slots share a note, which neither
        // assign nor captureLearn can produce. Resolved on the way in, so the
        // file and the running map cannot mean different things.
        DrumNoteMap clashing;
        clashing.fromString("Kick:60,Snare:60");

        check(clashing.countAssignments() == 1, "two slots taught one note is resolved to one");
        check(clashing.noteFor(1) == 60 && clashing.noteFor(0) == DrumNoteMap::none,
              "and it is the later slot that keeps it");
    }

    std::printf("\n%s\n", failures == 0 ? "ALL MIDI MAP TESTS PASSED" : "SOME MIDI MAP TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
