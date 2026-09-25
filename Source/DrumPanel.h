// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"
#include "UiComponents.h"
#include <array>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace wavelathe
{
// The Drums page: twelve channel strips and the rack they send to.
//
// Columns rather than rows, which is the shape every drum machine's mixer has
// and the one that uses the width. The grid on the Sequencer page is twelve
// ROWS of the same voices in the same order, which is a mismatch worth naming:
// a grid is read along time, and time runs left to right, so its voices have
// to stack. A mixer has no time in it at all.
//
// Self-contained the way SequencerPanel and MasteringPanel are - it owns its
// dials and its layout - with ONE exception it cannot avoid. All nine of each
// strip's dials are registered, unlike the rack's twelve, so the host can
// automate them and a controller can be learned onto them. That machinery
// lives in the editor and is keyed by knob pointer, so the panel hands out its
// knob-to-id pairs rather than keeping them to itself.
class DrumPanel : public juce::Component,
                  private juce::Timer
{
public:
    DrumPanel(WaveLatheProcessor& processor, ui::LcdDisplay& lcd);
    ~DrumPanel() override;

    void paint(juce::Graphics&) override;

    // Dimming an unused voice goes OVER the children, not behind them: paint()
    // draws under its child components in JUCE, so a wash drawn there would sit
    // beneath the very knobs it is meant to be dimming and do nothing visible.
    void paintOverChildren(juce::Graphics&) override;

    // The note under each strip name, drawn over the children - see the note
    // in the .cpp for why paint() is the wrong place for it.
    void paintTriggerNotes(juce::Graphics&);

    void resized() override;

    // Cancels an armed note learn when the page is left.
    //
    // An arm with nothing to disarm it is a trap: the strip would sit saying
    // "hit a pad" on a page nobody is looking at, and the next drum note
    // played - days later, in the middle of something else - would be eaten
    // and quietly reassign a slot.
    void visibilityChanged() override;

    // Pulls every dial back into step with the parameters behind them, for
    // when a project has been loaded underneath the panel.
    void refresh();

    // Which slot was last touched - a dial moved, a name clicked, a sample
    // loaded, a drum chosen.
    //
    // The editor points the Waveform and Envelope displays at it, so what they
    // draw is the drum being worked on rather than the patch. A callback and
    // not a getter, because the displays only need to know when it CHANGES and
    // polling for that would mean re-rendering a drum thirty times a second to
    // find out it is the same one.
    std::function<void(int)> onVoiceTouched;

    // The slot last worked on, or the Kick before anything has been. Read when
    // the page is opened, so the displays land on something rather than on
    // nothing.
    int lastTouched() const { return lastTouchedVoice >= 0 ? lastTouchedVoice : 0; }

    // Every dial the host can see and the registry id it drives, for the
    // editor to fold into the list that MIDI learn and the host both read.
    //
    // That is all nine dials on all twelve strips since 0.44.0. The rack's own
    // twelve are still absent, and still for the reason they always were: they
    // are one set of settings the whole kit shares rather than anything a
    // single voice owns, and they are saved with the project as the mastering
    // chain's twelve are.
    std::vector<std::pair<ui::ParameterKnob*, int>> automatableKnobs() const;

private:
    void timerCallback() override;
    void buildStrips();
    void buildRack();

    // The strip header: left-click hears the voice, right-click is where a
    // sample is loaded onto it or taken off.
    //
    // A subclass rather than a plain TextButton because onClick cannot tell
    // the two apart, and rather than a second control because there is no
    // room for one - a strip is a twelfth of the page wide and every pixel
    // of its height is already a dial.
    struct VoiceButton : juce::TextButton
    {
        using juce::TextButton::TextButton;

        std::function<void()> onRightClick;

        void mouseDown(const juce::MouseEvent& event) override
        {
            if (event.mods.isPopupMenu() && onRightClick != nullptr)
            {
                onRightClick();
                return;
            }

            juce::TextButton::mouseDown(event);
        }
    };

    // The lamp beside a Level dial: white when the voice sounds, red when it
    // goes past full scale.
    //
    // Its own component rather than something the panel paints, because the
    // panel is the parent of a hundred and fifty dials and repainting all of
    // them thirty times a second to light one dot would be absurd. A child
    // repaints its own few pixels.
    struct VoiceLed : juce::Component,
                      public juce::SettableTooltipClient
    {
        // Takes the peak since the last look and works out what to show.
        // Returns nothing; it repaints itself only when what it would draw has
        // actually changed, so a silent voice costs nothing at all.
        void showPeak(float peak);

        void paint(juce::Graphics&) override;

        float brightness = 0.0f;

        // Clipping LATCHES. A single sample over full scale is one frame at a
        // display rate, which nobody would see - and "that one went over" is
        // exactly the thing you need to be told about after it happened.
        //
        // When it last happened, not when the light should go out: an elapsed
        // time compares safely across the millisecond counter's wrap, and a
        // stored deadline does not. everClipped is what keeps a voice that has
        // never clipped from reading time zero as a clip at start-up.
        juce::uint32 clippedAtMs = 0;
        bool everClipped = false;

        // What was last drawn, so the repaint can be skipped when it would
        // draw the same thing. Quantised, because brightness decays through
        // values far finer than a lamp can show.
        int drawnBrightness = -1;
        bool drawnClipped = false;
    };

    // One voice: its name, which is also the button that auditions it, its
    // nine dials and the lamp beside the first of them.
    struct Strip
    {
        int voice = 0;
        std::unique_ptr<VoiceButton> audition;
        std::unique_ptr<VoiceLed> led;
        std::array<std::unique_ptr<ui::ParameterKnob>, project::numDrumControls> knobs;

        // The registry id each dial drives, or -1 for the two the host cannot
        // see. Parallel to knobs, so one index reaches both.
        std::array<int, project::numDrumControls> ids{};

        // Where the trigger note is printed, under the header. A rectangle
        // the panel paints into rather than a component, because a label that
        // holds four characters and never takes a click would be a component
        // for the sake of one drawText.
        juce::Rectangle<int> noteArea;
    };

    // One unit of the rack: a heading and three dials.
    struct FxUnit
    {
        std::unique_ptr<ui::FlowSection> section;
        std::array<std::unique_ptr<ui::ParameterKnob>, 3> knobs;
        std::array<project::DrumFxControl, 3> controls{};
    };

    juce::Rectangle<int> stripArea(int voice) const;

    // Says a slot has been worked on. Cheap to call repeatedly: it only tells
    // anybody when the answer changes.
    void touchVoice(int voice);

    // The last slot touched, so touchVoice can stay quiet when it has not
    // moved. -1 until something is.
    int lastTouchedVoice = -1;

    // The slot menu: load or clear a sample, and pick which synthesised drum
    // the slot is. Loading opens the SampleBrowser, which previews onto this
    // voice as you move through a folder and puts the slot back if you close
    // it any way but Load.
    void showSlotMenu(int voice);
    void handleSlotMenu(int voice, int result);
    void chooseSampleFor(int voice);

    // Menu ids. The engines take a RANGE at the top, one id per drum plus one
    // for None, so adding a drum to the table does not need a number chosen
    // here as well.
    static constexpr int loadSampleItem = 1;
    static constexpr int clearSampleItem = 2;
    static constexpr int learnNoteItem = 3;
    static constexpr int resetNoteItem = 4;
    static constexpr int firstEngineItem = 100;
    static void settingsFolderFor(const juce::File& file);

    // Puts each strip header back in step with what is loaded: tinted and
    // tooltipped when it holds a recording, plain when it is the circuit.
    void refreshHeaders();

    // Writes the note map out to the application settings. Called from the
    // menu and from the timer that notices an audio-thread bind.
    void saveNoteMap();

    WaveLatheProcessor& processorRef;
    ui::LcdDisplay& lcd;

    ui::FlowSection mixerSection{"Drum Kit", ui::colours::accentWarm};

    std::array<Strip, project::numDrumVoices> strips;
    std::array<FxUnit, drums::numFxUnits> rack;

    // Which voices have something written on them, so a row nobody is using
    // can be dimmed. Polled rather than pushed: the pattern is edited on
    // another page and there is no signal from it to here.
    std::array<bool, project::numDrumVoices> voiceUsed{};

    // The timer runs at the lamps' rate now, which is far faster than the
    // pattern needs asking about. This counts ticks so that stays cheap.
    int tick = 0;

    // The note map's version as of the last repaint. A learn binds on the
    // AUDIO thread, so there is nothing to be told by - the panel watches a
    // counter the same way the editor watches the CC map's.
    uint32_t lastNoteMapVersion = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrumPanel)
};
} // namespace wavelathe
