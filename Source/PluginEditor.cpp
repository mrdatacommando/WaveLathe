// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginEditor.h"

#if JucePlugin_Build_Standalone
 #include <juce_audio_plugin_client/Standalone/juce_StandaloneFilterWindow.h>
#endif
#include "SampleMatcher.h"
#include "PresetOptimizer.h"
#include "OfflineRenderer.h"
#include "Version.h"
#include "MidiMapView.h"

namespace wavelathe
{
namespace
{
// The wrapper builds its own menu from translatable strings, so the words it
// uses can be corrected without touching it. Worth doing even though the menu
// is hidden: if a future version of the wrapper puts it back, it will at least
// say the same thing this one does.
// What the Options menu can do. Declared here rather than inside the function
// that builds the menu, so the code that acts on a choice names the same thing
// the menu offered. It was once a local enum answered by a switch of bare
// numbers, and adding three entries in the middle silently renumbered six
// others - Audio settings ran the recovery, Undo opened a save dialog.
enum MenuId
{
    browsePresets = 1,
    savePreset,
    loadPreset,
    initPatch,
    sampleWavetable,
    builtInWaves,
    quickMatch,
    deepMatch,
    exportAb,
    midiClock,
    resetFolders,
    fullScreen,
    undoAction,
    redoAction,
    recoverProject,
    saveProject,
    loadProject,
    audioSettings,
    midiLearn,
    midiAssignments,
    midiMonitor,

    firstFolder = 100,
    firstKey = 200,
    firstScale = 300,
    firstChord = 400,
    firstKeyboardChannel = 500,

    // Seventeen ids apiece - omni or none, then sixteen channels - so the two
    // ranges are 500-516 and 600-616 and cannot meet.
    firstDrumChannel = 600,

    // Seven ids, one per octave of shift, at 700-706. Above the drum channel's
    // range on purpose: these are open-ended `>=` tests in the handler and the
    // new range has to be tested FIRST, so it has to be the highest.
    firstDrumTranspose = 700
};

void useProjectWording()
{
    static bool done = false;
    if (done)
        return;
    done = true;

    juce::String mappings;
    mappings << "language: English\n"
             << "countries: \n\n"
             << "\"Save current state...\" = \"Save Project...\"\n"
             << "\"Load a saved state...\" = \"Load Project...\"\n"
             << "\"Save current state\" = \"Save Project\"\n"
             << "\"Load a saved state\" = \"Load Project\"\n"
             << "\"Reset to default state\" = \"New Project\"\n";

    juce::LocalisedStrings::setCurrentMappings(new juce::LocalisedStrings(mappings, false));
}

// Readouts under a dial are read at a glance, not measured off - so they are
// kept to about four characters. Precision past that tells you nothing you
// would act on, and a long number sets the dials at different widths.
juce::String formatSeconds(double v)
{
    if (v < 1.0)
        return juce::String(juce::roundToInt(v * 1000.0)) + "m";

    return ui::toFixed(v, v < 10.0 ? 1 : 0) + "s";
}
juce::String formatHz(double v)
{
    if (v >= 1000.0)
        return ui::toFixed(v / 1000.0, v < 10000.0 ? 1 : 0) + "k";

    return juce::String(juce::roundToInt(v));
}
juce::String formatPercent(double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; }
// A dial has 128 positions, so the exact centre of a bipolar range often is not
// one of them. Anything within half a step of zero is reported as zero rather
// than as the 0.8% the dial technically sits at.
juce::String formatCents(double v) { return juce::String(std::abs(v) < 1.0 ? 0 : juce::roundToInt(v)) + "c"; }
juce::String formatVoices(double v) { return juce::String((int) std::round(v)); }
juce::String formatLfoHz(double v) { return juce::String(v, v < 10.0 ? 2 : 1) + "Hz"; }
// Bipolar depths read with a sign, since direction matters for a sweep.
juce::String formatBipolar(double v)
{
    if (std::abs(v) < 0.012)
        return "0%";

    return (v > 0.0 ? "+" : "") + juce::String(juce::roundToInt(v * 100.0)) + "%";
}
} // namespace

WaveLatheEditor::WaveLatheEditor(WaveLatheProcessor& processor)
    : AudioProcessorEditor(&processor), processorRef(processor), waveformDisplay(processor),
      outputScope(processor.getOutputTap(), processor.getParameters()),
      keyboardComponent(processor.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel(&lookAndFeel);

    // Explicitly, because setLookAndFeel above reaches this editor's CHILDREN
    // and the tooltip window is not one - built with a null parent it lives on
    // the desktop, so it was quietly using JUCE's default and drawing its text
    // bold and centred no matter what the look and feel here said. The popup
    // menus below need the same treatment for the same reason.
    tooltips.setLookAndFeel(&lookAndFeel);

    addAndMakeVisible(content);

    content.addAndMakeVisible(oscSection);
    content.addAndMakeVisible(osc2Section);
    content.addAndMakeVisible(subNoiseSection);
    content.addAndMakeVisible(filterSection);
    content.addAndMakeVisible(driveSection);
    content.addAndMakeVisible(ampSection);
    content.addAndMakeVisible(lfoSection);
    content.addAndMakeVisible(distortionSection);
    content.addAndMakeVisible(delaySection);
    content.addAndMakeVisible(reverbSection);
    content.addAndMakeVisible(chorusSection);
    content.addAndMakeVisible(phaserSection);
    content.addAndMakeVisible(eqSection);
    content.addAndMakeVisible(modEnvSection);
    content.addAndMakeVisible(lfo2Section);
    content.addAndMakeVisible(expressionSection);
    content.addAndMakeVisible(outSection);

    content.addAndMakeVisible(stereoLfoButton);
    stereoLfoButton.onClick = [this]
    {
        bool on = stereoLfoButton.getToggleState();
        processorRef.getParameters().stereoLfo = on ? 1.0f : 0.0f;
        lcd.print(on ? "Stereo LFO on - movement sweeps L/R." : "Stereo LFO off - movement is mono.");
        waveformDisplay.markDirty();
    };

    buildKnobs();

    content.addAndMakeVisible(waveformDisplay);
    content.addAndMakeVisible(outputScope);
    content.addAndMakeVisible(lcd);
    lcd.print("WAVELATHE READY");
    lcd.print("Play the keyboard, or load a synth preset or a project.");

    auto styleButton = [this](juce::TextButton& b, std::function<void()> action)
    {
        b.setColour(juce::TextButton::buttonColourId, ui::colours::panel);
        b.onClick = std::move(action);
        content.addAndMakeVisible(b);
    };

    // Every action now lives in one menu in the header, rather than a row of
    // buttons competing with the dials for space.
    styleButton(optionsButton, [this] { showOptionsMenu(); });
    // Tabs rather than a button that renames itself: both pages are named, and
    // the one you are on is marked.
    content.addAndMakeVisible(pageTabs);
    pageTabs.onTabSelected = [this](int index) { setPage(index); };

    // Parented to the header for now. The window does not exist yet when the
    // editor is built, so the move into its title bar happens on the first tick
    // that finds one - the same way the wrapper's own button is dealt with.
    content.addAndMakeVisible(fullScreenButton);
    fullScreenButton.onClick = [this] { toggleFullScreen(); };
    fullScreenButton.setTooltip("Full screen: gives the synth the whole display, with no title bar "
                               "or taskbar over it. Press it again, or Escape, or F11, to come "
                               "back out.");

    // The sequencer takes over the same panel area the synth controls use, so
    // both get the full width rather than being squeezed side by side.
    sequencerPanel = std::make_unique<SequencerPanel>(processorRef, lcd);
    content.addChildComponent(*sequencerPanel);

    // The Master page shares that same area. It is handed a way to speak to the
    // display rather than a reference to it, so the panel never has to know
    // what an LcdDisplay is - the same boundary the mastering library keeps
    // from the synth, one level up.
    // The Drums page, between the two. It is handed the display directly
    // rather than a callback, unlike the Master page: its dials ARE registered
    // parameters, so it already knows about the processor and the registry, and
    // a boundary that only half exists is worth less than none.
    drumPanel = std::make_unique<DrumPanel>(processorRef, lcd);

    // The two displays at the top of the window show whatever is being worked
    // on. On the Drums page that is a drum, not the patch.
    drumPanel->onVoiceTouched = [this](int voice) { waveformDisplay.showDrumSlot(voice); };

    content.addChildComponent(*drumPanel);

    masteringPanel = std::make_unique<MasteringPanel>(processorRef);
    masteringPanel->onMessage = [this](const juce::String& text) { lcd.print(text); };

    // Numbered after the bus's seven, so the page reads as one chain of eleven
    // in the order the signal takes. The effect plugin keeps its own 1 to 4.
    masteringPanel->setFirstStage(BusEffectsPanel::firstStage + BusEffectsPanel::numStages);

    // The Master page's displays draw the patch THROUGH this chain, so moving
    // one of its dials changes the picture.
    masteringPanel->onChanged = [this] { waveformDisplay.markDirty(); };
    content.addChildComponent(*masteringPanel);

    busEffectsPanel = std::make_unique<BusEffectsPanel>(processorRef);
    busEffectsPanel->onTransient = [this](const juce::String& text) { lcd.printTransient(text); };
    busEffectsPanel->onMessage = [this](const juce::String& text) { lcd.print(text); };
    busEffectsPanel->onChanged = [this] { waveformDisplay.markDirty(); outputScope.repaint(); };
    content.addChildComponent(*busEffectsPanel);

    registerAutomatableKnobs();

    // Added last of the panel children and kept on top, so it covers every dial
    // rather than whichever ones happened to be added after it. Hidden until
    // learn mode is switched on.
    content.addChildComponent(learnLayer);
    learnLayer.setAlwaysOnTop(true);
    lastMapVersion = processorRef.getMidiMap().getVersion();
    rebuildMappedParameters();

    gatherSynthPageComponents();

    // --- Arpeggiator, beside the keyboard ---
    content.addAndMakeVisible(arpSection);
    arpSection.setStyle(ui::SectionStyle::modulator);

    content.addAndMakeVisible(arpEnabledButton);
    arpEnabledButton.onClick = [this]
    {
        bool on = arpEnabledButton.getToggleState();
        processorRef.getParameters().arpEnabled = on ? 1.0f : 0.0f;
        lcd.print(on ? "Arpeggiator on - hold notes to run a pattern." : "Arpeggiator off.");
    };

    // Too many to cycle through one click at a time, so both pickers open a
    // grouped list instead. Mode chooses the note order, Pattern the rhythm.
    styleButton(arpModeButton, [this] { showArpModeMenu(); });
    styleButton(arpPatternButton, [this] { showArpPatternMenu(); });
    updateArpModeButton();
    updateArpPatternButton();

    // --- Tempo, and which controls follow it ---

    auto addSyncToggle = [this](juce::ToggleButton& button, std::atomic<float>& target, const juce::String& what)
    {
        content.addAndMakeVisible(button);
        button.setToggleState(target.load() > 0.5f, juce::dontSendNotification);
        button.onClick = [this, &button, &target, what]
        {
            bool on = button.getToggleState();
            target = on ? 1.0f : 0.0f;
            lcd.print(what + (on ? " synced to tempo." : " running free."));

            // The rate dial changes units with it, so what it reads is always
            // the grid the feature is actually on.
            if (&target == &processorRef.getParameters().arpSync)
                updateArpRateScale();
        };
    };

    auto& params = processorRef.getParameters();
    // Mono narrows everything to one note; legato then lets an overlapping note
    // take that one over rather than starting again.
    content.addAndMakeVisible(monoButton);
    monoButton.onClick = [this]
    {
        bool on = monoButton.getToggleState();
        processorRef.getParameters().monoMode = on ? 1.0f : 0.0f;
        lcd.print(on ? "Mono: one note at a time, newest key wins."
                     : "Polyphonic.");
    };

    content.addAndMakeVisible(legatoButton);
    legatoButton.onClick = [this]
    {
        bool on = legatoButton.getToggleState();
        processorRef.getParameters().legatoMode = on ? 1.0f : 0.0f;
        lcd.print(on ? "Legato: an overlapping note slides in without re-attacking."
                     : "Every note re-attacks.");
    };

    addSyncToggle(arpSyncButton, params.arpSync, "Arpeggiator");
    addSyncToggle(delaySyncButton, params.delaySync, "Delay");
    addSyncToggle(lfo1SyncButton, params.lfo1Sync, "LFO 1");
    addSyncToggle(lfo2SyncButton, params.lfo2Sync, "LFO 2");

    keyboardComponent.setAvailableRange(0, 127);
    keyboardComponent.setLowestVisibleKey(lowestVisibleKey);

    // The scroll arrows are given no width rather than switched off. They are
    // drawn OVER the first and last key, so at five octaves they were covering
    // half of the bottom C and half of the top B - and Oct - and Oct + sit an
    // inch away doing the same job with a label on them.
    //
    // Not setScrollButtonsVisible(false): that also pins the keyboard to the
    // bottom of its available range, so Oct - and Oct + would stop moving it.
    keyboardComponent.setScrollButtonWidth(0);
    keyboardComponent.setColour(juce::MidiKeyboardComponent::shadowColourId, juce::Colours::transparentBlack);

    // Its built-in key mapping only works while it holds keyboard focus, which
    // any click elsewhere takes away. We handle the computer keyboard at the
    // editor instead, so clear its mapping rather than have two of them.
    keyboardComponent.clearKeyMappings();
    keyboardComponent.setWantsKeyboardFocus(false);
    content.addAndMakeVisible(keyboardComponent);

    // Key events bubble up from whatever has focus, so listening here catches
    // them wherever the last click happened to land.
    addKeyListener(this);
    setWantsKeyboardFocus(true);
    computerKeyNotes.assign((size_t) numComputerKeys, -1);

    content.addAndMakeVisible(octaveDownButton);
    octaveDownButton.onClick = [this] { shiftOctave(-12); };

    content.addAndMakeVisible(octaveUpButton);
    octaveUpButton.onClick = [this] { shiftOctave(12); };

    content.addAndMakeVisible(holdNoteButton);
    holdNoteButton.onClick = [this] { setHoldEnabled(holdNoteButton.getToggleState()); };

    content.addAndMakeVisible(playStopButton);
    playStopButton.onClick = [this]
    {
        auto& p = processorRef.getParameters();
        bool nowPlaying = p.seqPlaying.load() <= 0.5f;
        p.seqPlaying = nowPlaying ? 1.0f : 0.0f;

        // Stopping disarms as well: leaving REC lit over a stopped transport
        // is how you record over something by accident.
        if (!nowPlaying)
            p.seqRecord = 0.0f;

        refreshTransportButtons();
        lcd.print(nowPlaying ? "Sequencer running." : "Sequencer stopped.");

        if (sequencerPanel != nullptr)
            sequencerPanel->refreshFromProcessor();
    };

    content.addAndMakeVisible(recordButton);
    recordButton.onClick = [this]
    {
        auto& p = processorRef.getParameters();
        bool arming = p.seqRecord.load() <= 0.5f;
        p.seqRecord = arming ? 1.0f : 0.0f;

        // Arming with the transport stopped is never what you meant.
        if (arming && p.seqPlaying.load() <= 0.5f)
            p.seqPlaying = 1.0f;

        refreshTransportButtons();
        lcd.print(arming ? "Recording: move a dial or play a note to capture it."
                         : "Recording off.");

        if (sequencerPanel != nullptr)
            sequencerPanel->refreshFromProcessor();
    };

    processorRef.getKeyboardState().addListener(this);
    useProjectWording();

    // A restored session has already put its values into the parameters by the
    // time the panel is built, so the panel is brought into line with them -
    // otherwise the switches would all read as off over a patch that is on.
    holdNoteButton.setToggleState(processorRef.getParameters().holdNotes.load() > 0.5f,
                                  juce::dontSendNotification);
    refreshKnobsFromParameters();

    // The dials have just been filled from the parameters, so whatever program
    // changes happened before this panel existed are already on them. Starting
    // level means the first tick does not announce a preset nobody just chose.
    lastProgramChange = processorRef.getProgramChangeCount();

    startTimerHz(60); // also the fallback that reconciles held computer keys

    setResizable(true, true);
    setResizeLimits(1000, 700, 4000, 3000);

    // Open filling the screen: the panel scales to the window rather than
    // scrolling, so everything stays visible at once.
    //
    // The main display is only a starting guess. There is no window yet, so
    // there is no telling which screen this will be shown on - fitToOpeningDisplay
    // settles that on the first tick, once there is a window that has been put
    // somewhere.
    auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay();
    auto area = display != nullptr ? display->userBounds.toNearestInt()
                                   : juce::Rectangle<int>(0, 0, 1440, 900);
    setSize(area.getWidth(), juce::jmax(700, area.getHeight() - 48));
}

WaveLatheEditor::~WaveLatheEditor()
{
    stopTimer();
    removeKeyListener(this);
    releaseComputerKeyboardNotes();
    processorRef.getKeyboardState().removeListener(this);
    stopOptimizerThread();
    tooltips.setLookAndFeel(nullptr);
    setLookAndFeel(nullptr);
}

void WaveLatheEditor::buildKnobs()
{
    auto& p = processorRef.getParameters();

    // Each dial reports its real value and a human-readable string; the LCD
    // shows the real units while the dial face shows the 0-127 MIDI step.
    auto makeKnob = [this](std::unique_ptr<ui::ParameterKnob>& target, const juce::String& name, ui::IconType icon,
                            juce::NormalisableRange<double> range, double initial,
                            std::function<juce::String(double)> formatter, std::function<void(double)> apply)
    {
        target = std::make_unique<ui::ParameterKnob>(name, icon, range, initial, std::move(formatter));
        target->onValueChanged = [this, apply, &target](double realValue, juce::String text)
        {
            apply(realValue);
            lcd.printTransient(target->getName().toUpperCase() + ": " + text + "   [" +
                               juce::String(target->getMidiStep()) + "/127]");
            waveformDisplay.markDirty();

            // And tell the host, which is how Live's Configure discovers a
            // parameter at all: its manual is explicit that adjusting a control
            // in the plugin's own window while mapping is what creates the entry
            // in Live's panel. A published parameter that never reports is
            // invisible to Configure, to automation recording, and to the
            // temporary entries Live puts in its envelope choosers.
            if (auto* parameter = hostParameterFor(target.get()))
                parameter->reportToHost();
        };

        // Turning a dial is undoable like everything else - it is the change
        // people make most, so leaving it out made undo look broken.
        target->onGestureStart = [this, name, &target]
        {
            processorRef.recordUndoPoint(name);

            if (auto* parameter = hostParameterFor(target.get()))
                parameter->beginChangeGesture();
        };

        // Always paired with the above, including on the paths that have no
        // mouse-up of their own. A host left holding an open gesture keeps its
        // own automation off that parameter until something closes it.
        target->onGestureEnd = [this, &target]
        {
            if (auto* parameter = hostParameterFor(target.get()))
                parameter->endChangeGesture();
        };

        content.addAndMakeVisible(*target);
    };

    juce::NormalisableRange<double> unit{0.0, 1.0};
    juce::NormalisableRange<double> timeRange{0.001, 5.0, 0.0, 0.3};
    juce::NormalisableRange<double> shortTimeRange{0.001, 3.0, 0.0, 0.3};

    makeKnob(waveKnob, "Wave", ui::IconType::wave, unit, p.wavePosition.load(), formatPercent,
             [&p](double v) { p.wavePosition = (float) v; });
    makeKnob(voicesKnob, "Voices", ui::IconType::voices, {1.0, 7.0}, p.unisonVoices.load(), formatVoices,
             [&p](double v) { p.unisonVoices = (float) std::round(v); });
    makeKnob(detuneKnob, "Detune", ui::IconType::detune, {0.0, 50.0}, p.unisonDetuneCents.load(), formatCents,
             [&p](double v) { p.unisonDetuneCents = (float) v; });
    makeKnob(widthKnob, "Width", ui::IconType::width, unit, p.unisonWidth.load(), formatPercent,
             [&p](double v) { p.unisonWidth = (float) v; });

    // Glide is a time, but the useful part of the range is all at the short
    // end, so the dial is skewed to put it where your hand is.
    makeKnob(glideKnob, "Glide", ui::IconType::pitch, {0.0, 2000.0, 0.0, 0.3}, p.glideTimeMs.load(),
             [](double v) { return v < 1.0 ? juce::String("off") : juce::String((int) v) + "m"; },
             [&p](double v) { p.glideTimeMs = (float) v; });

    makeKnob(osc1LevelKnob, "Level", ui::IconType::gain, unit, p.osc1Level.load(), formatPercent,
             [&p](double v) { p.osc1Level = (float) v; });

    makeKnob(osc2LevelKnob, "Level", ui::IconType::gain, unit, p.osc2Level.load(), formatPercent,
             [&p](double v) { p.osc2Level = (float) v; });
    makeKnob(osc2WaveKnob, "Wave", ui::IconType::wave, unit, p.osc2WavePosition.load(), formatPercent,
             [&p](double v) { p.osc2WavePosition = (float) v; });
    makeKnob(osc2SemiKnob, "Semi", ui::IconType::pitch, {-24.0, 24.0}, p.osc2Semitones.load(),
             [](double v) { return juce::String((int) std::round(v)) + " st"; },
             [&p](double v) { p.osc2Semitones = (float) std::round(v); });
    makeKnob(osc2FineKnob, "Fine", ui::IconType::detune, {-100.0, 100.0}, p.osc2Fine.load(),
             [](double v) { return juce::String(juce::roundToInt(v)) + "c"; },
             [&p](double v) { p.osc2Fine = (float) v; });

    makeKnob(subLevelKnob, "Sub", ui::IconType::subOsc, unit, p.subLevel.load(), formatPercent,
             [&p](double v) { p.subLevel = (float) v; });
    makeKnob(subWaveKnob, "Sub Wave", ui::IconType::wave, unit, p.subWave.load(),
             [](double v) { return v < 0.5 ? "sine " + ui::toFixed(v * 200.0, 0) + "%" : "square "
                                                 + ui::toFixed((v - 0.5) * 200.0, 0) + "%"; },
             [&p](double v) { p.subWave = (float) v; });
    makeKnob(subOctaveKnob, "Sub Oct", ui::IconType::pitch, {1.0, 2.0}, p.subOctave.load(),
             [](double v) { return "-" + juce::String((int) std::round(v)) + " oct"; },
             [&p](double v) { p.subOctave = (float) std::round(v); });
    makeKnob(noiseLevelKnob, "Noise", ui::IconType::noise, unit, p.noiseLevel.load(), formatPercent,
             [&p](double v) { p.noiseLevel = (float) v; });
    makeKnob(noiseColourKnob, "Colour", ui::IconType::noise, unit, p.noiseColour.load(),
             [](double v) { return v < 0.5 ? "white" : "pink"; },
             [&p](double v) { p.noiseColour = (float) v; });

    // The names come from the registry rather than a second copy here, so the
    // dial and the host's automation lane cannot end up disagreeing about what
    // step 3 is called.
    const int filterTypeId = paramreg::idForName("Filter Type");
    makeKnob(filterTypeKnob, "Type", ui::IconType::filterType, {0.0, 4.0}, p.filterType.load(),
             [filterTypeId](double v)
             {
                 const char* label = paramreg::stepLabel(filterTypeId, juce::jlimit(0, 4, (int) std::round(v)));
                 return label != nullptr ? juce::String(label) : juce::String((int) std::round(v));
             },
             [&p](double v) { p.filterType = (float) std::round(v); });

    makeKnob(cutoffKnob, "Cutoff", ui::IconType::cutoff, {20.0, 20000.0, 0.0, 0.3}, p.filterCutoffHz.load(), formatHz,
             [&p](double v) { p.filterCutoffHz = (float) v; });
    makeKnob(resonanceKnob, "Reso", ui::IconType::resonance, {0.1, 1.0}, p.filterResonance.load(), formatPercent,
             [&p](double v) { p.filterResonance = (float) v; });

    makeKnob(driveKnob, "Drive", ui::IconType::drive, unit, p.driveAmount.load(), formatPercent,
             [&p](double v) { p.driveAmount = (float) v; });

    makeKnob(attackKnob, "Attack", ui::IconType::attack, shortTimeRange, p.attack.load(), formatSeconds,
             [&p](double v) { p.attack = (float) v; });
    makeKnob(decayKnob, "Decay", ui::IconType::decay, shortTimeRange, p.decay.load(), formatSeconds,
             [&p](double v) { p.decay = (float) v; });
    makeKnob(sustainKnob, "Sustain", ui::IconType::sustain, unit, p.sustain.load(), formatPercent,
             [&p](double v) { p.sustain = (float) v; });
    makeKnob(releaseKnob, "Release", ui::IconType::release, timeRange, p.release.load(), formatSeconds,
             [&p](double v) { p.release = (float) v; });

    makeKnob(lfoRateKnob, "Rate", ui::IconType::lfoRate, {0.05, 20.0, 0.0, 0.3}, p.lfoRateHz.load(), formatLfoHz,
             [&p](double v) { p.lfoRateHz = (float) v; });
    makeKnob(lfoToFilterKnob, "To Filter", ui::IconType::lfoToFilter, unit, p.lfoDepth.load(), formatPercent,
             [&p](double v) { p.lfoDepth = (float) v; });
    makeKnob(lfoToAmpKnob, "To Amp", ui::IconType::lfoToAmp, unit, p.lfoAmpDepth.load(), formatPercent,
             [&p](double v) { p.lfoAmpDepth = (float) v; });
    makeKnob(lfoToWaveKnob, "To Wave", ui::IconType::lfoToWave, unit, p.lfoToWave.load(), formatPercent,
             [&p](double v) { p.lfoToWave = (float) v; });

    // Mod envelope: a second envelope free to be routed, rather than
    // hardwired to level like the amp envelope.
    juce::NormalisableRange<double> bipolar{-1.0, 1.0};
    makeKnob(modEnvAttackKnob, "Attack", ui::IconType::attack, shortTimeRange, p.modEnvAttack.load(), formatSeconds,
             [&p](double v) { p.modEnvAttack = (float) v; });
    makeKnob(modEnvDecayKnob, "Decay", ui::IconType::decay, shortTimeRange, p.modEnvDecay.load(), formatSeconds,
             [&p](double v) { p.modEnvDecay = (float) v; });
    makeKnob(modEnvSustainKnob, "Sustain", ui::IconType::sustain, unit, p.modEnvSustain.load(), formatPercent,
             [&p](double v) { p.modEnvSustain = (float) v; });
    makeKnob(modEnvReleaseKnob, "Release", ui::IconType::release, timeRange, p.modEnvRelease.load(), formatSeconds,
             [&p](double v) { p.modEnvRelease = (float) v; });
    makeKnob(modEnvToWaveKnob, "To Wave", ui::IconType::modEnvToWave, bipolar, p.modEnvToWave.load(), formatBipolar,
             [&p](double v) { p.modEnvToWave = (float) v; });
    makeKnob(modEnvToCutoffKnob, "To Cutoff", ui::IconType::modEnvToCutoff, bipolar, p.modEnvToCutoff.load(),
             formatBipolar, [&p](double v) { p.modEnvToCutoff = (float) v; });

    makeKnob(lfo2RateKnob, "Rate", ui::IconType::lfoRate, {0.05, 20.0, 0.0, 0.3}, p.lfo2RateHz.load(), formatLfoHz,
             [&p](double v) { p.lfo2RateHz = (float) v; });
    makeKnob(lfo2ToWaveKnob, "To Wave", ui::IconType::lfoToWave, unit, p.lfo2ToWave.load(), formatPercent,
             [&p](double v) { p.lfo2ToWave = (float) v; });
    makeKnob(lfo2ToCutoffKnob, "To Cutoff", ui::IconType::lfoToFilter, unit, p.lfo2ToCutoff.load(), formatPercent,
             [&p](double v) { p.lfo2ToCutoff = (float) v; });

    makeKnob(gainKnob, "Gain", ui::IconType::gain, unit, p.masterGain.load(), formatPercent,
             [&p](double v) { p.masterGain = (float) v; });

    // Master effects, applied to the mixed output after the voices.
    makeKnob(fxDistortionKnob, "Amount", ui::IconType::distortion, unit, p.fxDistortion.load(), formatPercent,
             [&p](double v) { p.fxDistortion = (float) v; });
    makeKnob(delayTimeKnob, "Time", ui::IconType::delayTime, {10.0, 1500.0, 0.0, 0.4}, p.delayTimeMs.load(),
             [](double v) { return juce::String(juce::roundToInt(v)) + "m"; },
             [&p](double v) { p.delayTimeMs = (float) v; });
    makeKnob(delayFeedbackKnob, "Feedback", ui::IconType::delayFeedback, {0.0, 0.95}, p.delayFeedback.load(),
             formatPercent, [&p](double v) { p.delayFeedback = (float) v; });
    makeKnob(delayMixKnob, "Mix", ui::IconType::delayMix, unit, p.delayMix.load(), formatPercent,
             [&p](double v) { p.delayMix = (float) v; });
    makeKnob(reverbSizeKnob, "Size", ui::IconType::reverbSize, unit, p.reverbSize.load(), formatPercent,
             [&p](double v) { p.reverbSize = (float) v; });
    makeKnob(reverbMixKnob, "Mix", ui::IconType::reverbMix, unit, p.reverbMix.load(), formatPercent,
             [&p](double v) { p.reverbMix = (float) v; });

    // Chorus and phaser: both are mix-based, so at zero they cost nothing and
    // a patch that ignores them is untouched.
    makeKnob(chorusRateKnob, "Rate", ui::IconType::lfoRate, {0.05, 8.0, 0.0, 0.4}, p.chorusRate.load(),
             [](double v) { return juce::String(v, 2) + "Hz"; },
             [&p](double v) { p.chorusRate = (float) v; });
    makeKnob(chorusDepthKnob, "Depth", ui::IconType::lfoToAmp, unit, p.chorusDepth.load(), formatPercent,
             [&p](double v) { p.chorusDepth = (float) v; });
    makeKnob(chorusMixKnob, "Mix", ui::IconType::delayMix, unit, p.chorusMix.load(), formatPercent,
             [&p](double v) { p.chorusMix = (float) v; });

    makeKnob(phaserRateKnob, "Rate", ui::IconType::lfoRate, {0.05, 8.0, 0.0, 0.4}, p.phaserRate.load(),
             [](double v) { return juce::String(v, 2) + "Hz"; },
             [&p](double v) { p.phaserRate = (float) v; });
    makeKnob(phaserFeedbackKnob, "Feedback", ui::IconType::delayFeedback, {0.0, 0.9},
             p.phaserFeedback.load(), formatPercent,
             [&p](double v) { p.phaserFeedback = (float) v; });
    makeKnob(phaserMixKnob, "Mix", ui::IconType::delayMix, unit, p.phaserMix.load(), formatPercent,
             [&p](double v) { p.phaserMix = (float) v; });

    // EQ: decibels, marked so that flat reads as flat rather than as a
    // number you have to interpret.
    auto formatDecibels = [](double v)
    {
        if (std::abs(v) < 0.15)
            return juce::String("flat");
        return (v > 0.0 ? juce::String("+") : juce::String()) + ui::toFixed(v, std::abs(v) < 10.0 ? 1 : 0);
    };

    makeKnob(eqLowKnob, "Low", ui::IconType::cutoff, {-12.0, 12.0}, p.eqLowGain.load(), formatDecibels,
             [&p](double v) { p.eqLowGain = (float) v; });
    makeKnob(eqMidKnob, "Mid", ui::IconType::resonance, {-12.0, 12.0}, p.eqMidGain.load(), formatDecibels,
             [&p](double v) { p.eqMidGain = (float) v; });
    makeKnob(eqHighKnob, "High", ui::IconType::modEnvToCutoff, {-12.0, 12.0}, p.eqHighGain.load(),
             formatDecibels, [&p](double v) { p.eqHighGain = (float) v; });

    // Expression: playing as a modulation source. Velocity is fixed at the
    // strike, the wheel and pressure keep moving while the note sounds.
    makeKnob(velToCutoffKnob, "Vel>Cut", ui::IconType::lfoToFilter, unit, p.velocityToCutoff.load(),
             formatPercent, [&p](double v) { p.velocityToCutoff = (float) v; });
    makeKnob(velToWaveKnob, "Vel>Wave", ui::IconType::lfoToWave, unit, p.velocityToWave.load(),
             formatPercent, [&p](double v) { p.velocityToWave = (float) v; });
    makeKnob(velToAmpKnob, "Vel>Amp", ui::IconType::lfoToAmp, unit, p.velocityToAmp.load(),
             formatPercent, [&p](double v) { p.velocityToAmp = (float) v; });
    makeKnob(velToResonanceKnob, "Vel>Res", ui::IconType::resonance, unit, p.velocityToResonance.load(),
             formatPercent, [&p](double v) { p.velocityToResonance = (float) v; });
    makeKnob(accentKnob, "Accent", ui::IconType::lfoToAmp, unit, p.accentAmount.load(),
             formatPercent, [&p](double v) { p.accentAmount = (float) v; });
    makeKnob(wheelToCutoffKnob, "Whl>Cut", ui::IconType::lfoToFilter, unit, p.wheelToCutoff.load(),
             formatPercent, [&p](double v) { p.wheelToCutoff = (float) v; });
    makeKnob(wheelToWaveKnob, "Whl>Wave", ui::IconType::lfoToWave, unit, p.wheelToWave.load(),
             formatPercent, [&p](double v) { p.wheelToWave = (float) v; });
    makeKnob(pressureToCutoffKnob, "Press>Cut", ui::IconType::lfoToFilter, unit,
             p.pressureToCutoff.load(), formatPercent,
             [&p](double v) { p.pressureToCutoff = (float) v; });

    // Arpeggiator controls. Rate is built as a division dial and re-scaled by
    // updateArpRateScale, which is also what switches it back to plain speed
    // when sync is off.
    makeKnob(arpRateKnob, "Rate", ui::IconType::lfoRate,
             {0.0, (double) tempo::getNumDivisions() - 1}, p.arpDivision.load(),
             [](double v) { return tempo::divisionName((int) std::round(v)); },
             [this, &p](double v)
             {
                 if (p.arpSync.load() > 0.5f)
                     p.arpDivision = (float) std::round(v);
                 else
                     p.arpRateHz = (float) v;
             });

    // What the rate is measured against. Whole multiples of the sequencer's
    // rate keep the two on one grid, which is what lets the arpeggiator be
    // recorded onto the sequencer's timeline and play back in the same places.
    makeKnob(arpLinkKnob, "Link", ui::IconType::lfoRate,
             {0.0, (double) tempo::getNumRateLinks() - 1}, p.arpRateLink.load(),
             [&p](double v)
             {
                 // Just the multiplier: the Rate dial beside this one now shows
                 // its own value at all times, so what the link works out to is
                 // already on screen and does not need repeating here.
                 int link = juce::jlimit(0, tempo::getNumRateLinks() - 1, (int) std::round(v));
                 return tempo::rateLinkName(link).fromLastOccurrenceOf(" ", false, false).isEmpty()
                            ? tempo::rateLinkName(link)
                            : tempo::rateLinkName(link).replace("Seq ", "");
             },
             [this, &p](double v)
             {
                 p.arpRateLink = (float) std::round(v);
                 updateArpRateScale();
             });
    makeKnob(arpOctavesKnob, "Octaves", ui::IconType::pitch, {1.0, 3.0}, p.arpOctaves.load(),
             [](double v) { return juce::String((int) std::round(v)); },
             [&p](double v) { p.arpOctaves = (float) std::round(v); });
    makeKnob(arpGateKnob, "Gate", ui::IconType::sustain, {0.05, 1.0}, p.arpGate.load(), formatPercent,
             [&p](double v) { p.arpGate = (float) v; });
    // Swing is what makes the syncopated rhythms actually groove, so it sits
    // with the pattern rather than being left to the host.
    makeKnob(arpSwingKnob, "Swing", ui::IconType::lfoToAmp, {0.0, 0.75}, p.arpSwing.load(),
             [](double v) { return juce::String(juce::roundToInt(v * 100.0)) + "%"; },
             [&p](double v) { p.arpSwing = (float) v; });
    // The readout answers from whatever is actually driving the tempo, not from
    // where the dial happens to sit. A dial carries 128 positions across 20 to
    // 300 BPM, which is a step of better than two - fine for choosing a tempo,
    // useless for reporting one, and a host at 120.00 would land on 119. So the
    // position follows the host approximately, as a dial must, and the number
    // under it is exact and says where it came from.
    makeKnob(bpmKnob, "BPM", ui::IconType::lfoRate, {20.0, 300.0}, p.bpm.load(),
             [this](double v)
             {
                 if (processorRef.isTempoExternal())
                     return juce::String(processorRef.getEffectiveBpm(), 1) + " ext";

                 return juce::String((int) std::round(v));
             },
             [&p](double v) { p.bpm = (float) v; });
}

void WaveLatheEditor::updateArpRateScale()
{
    if (arpRateKnob == nullptr || arpLinkKnob == nullptr)
        return;

    auto& p = processorRef.getParameters();
    bool synced = p.arpSync.load() > 0.5f;
    int link = juce::jlimit(0, tempo::getNumRateLinks() - 1, (int) std::round(p.arpRateLink.load()));
    int sequencerDivision = juce::jlimit(0, tempo::getNumDivisions() - 1,
                                          (int) std::round(p.seqDivision.load()));

    // Rebuilding walks the dial's whole travel to find its notches, so it is
    // only worth doing when one of the three things it depends on has moved.
    int signature = (synced ? 1 : 0) + link * 2 + sequencerDivision * 64;
    if (signature == arpRateScaleBuiltFor)
        return;
    arpRateScaleBuiltFor = signature;

    auto divisionScale = juce::NormalisableRange<double>{0.0, (double) tempo::getNumDivisions() - 1};
    auto divisionText = [](double v) { return tempo::divisionName((int) std::round(v)); };

    if (!synced)
    {
        // Running free: the dial is a speed, and the sequencer's grid has
        // nothing to say about it.
        arpRateKnob->setScale({0.5, 30.0, 0.0, 0.4},
                              [](double v) { return juce::String(v, 1) + "/s"; },
                              p.arpRateHz.load());
    }
    else if (tempo::isFreeRateLink(link))
    {
        arpRateKnob->setScale(divisionScale, divisionText, p.arpDivision.load());
    }
    else
    {
        // Following the sequencer. The dial shows the division the link works
        // out to, so you can see what the arpeggiator is actually playing, but
        // the Link dial is what sets it - there is only one rate here, which is
        // the whole point of linking them.
        double beats = tempo::linkedBeats(
            tempo::divisionBeats(sequencerDivision), link);
        arpRateKnob->setScale(divisionScale, divisionText, (double) tempo::nearestDivisionForBeats(beats));
    }

    bool dialLive = !synced || tempo::isFreeRateLink(link);
    arpRateKnob->setEnabled(dialLive);
    arpRateKnob->setAlpha(dialLive ? 1.0f : 0.5f);
}


void WaveLatheEditor::followTempo()
{
    const bool external = processorRef.isTempoExternal();
    const double bpm = processorRef.getEffectiveBpm();

    // While the dial is the thing driving, it is also the truth, and writing to
    // it every tick would fight the hand turning it.
    if (! external && ! lastTempoExternal)
        return;

    if (external)
        bpmKnob->setRealValue(bpm);
    else
        bpmKnob->setRealValue(processorRef.getParameters().bpm.load()); // handed back

    // The number can change while the dial does not: a host moving from 120.0
    // to 120.4 is the same step and a different readout, and setRealValue draws
    // nothing when the step has not moved - deliberately, because sixty-three
    // dials repainting every frame for nothing is what that guard is there for.
    if (external != lastTempoExternal || std::abs(bpm - lastShownBpm) > 0.05)
        bpmKnob->repaint();

    lastTempoExternal = external;
    lastShownBpm = bpm;
}

void WaveLatheEditor::followProgramChange()
{
    const auto seen = processorRef.getProgramChangeCount();

    if (seen == lastProgramChange)
        return;

    lastProgramChange = seen;

    // The dials, and nothing else. The processor has already told the host,
    // from its own async update - coming through adoptLoadedPatch here would
    // tell it a second time and put sixty-three more automation points into an
    // armed take for nothing.
    refreshKnobsFromParameters();
    waveformDisplay.markDirty();
    lcd.print(processorRef.getProgramName(processorRef.getCurrentProgram()));
}

void WaveLatheEditor::adoptLoadedPatch()
{
    refreshKnobsFromParameters();

    // Not folded into refreshKnobsFromParameters, which is also how the editor
    // catches up when its window is opened - and telling a host that sixty-three
    // values "changed" every time someone looks at the panel would write sixty-
    // three automation points into an armed take for nothing.
    processorRef.notifyHostOfAllParameters();
}
void WaveLatheEditor::refreshKnobsFromParameters()
{
    auto& p = processorRef.getParameters();
    waveKnob->setRealValue(p.wavePosition.load());
    voicesKnob->setRealValue(p.unisonVoices.load());
    detuneKnob->setRealValue(p.unisonDetuneCents.load());
    widthKnob->setRealValue(p.unisonWidth.load());
    cutoffKnob->setRealValue(p.filterCutoffHz.load());
    resonanceKnob->setRealValue(p.filterResonance.load());
    filterTypeKnob->setRealValue(p.filterType.load());
    osc1LevelKnob->setRealValue(p.osc1Level.load());
    osc2LevelKnob->setRealValue(p.osc2Level.load());
    osc2WaveKnob->setRealValue(p.osc2WavePosition.load());
    osc2SemiKnob->setRealValue(p.osc2Semitones.load());
    osc2FineKnob->setRealValue(p.osc2Fine.load());
    subLevelKnob->setRealValue(p.subLevel.load());
    subWaveKnob->setRealValue(p.subWave.load());
    subOctaveKnob->setRealValue(p.subOctave.load());
    noiseLevelKnob->setRealValue(p.noiseLevel.load());
    noiseColourKnob->setRealValue(p.noiseColour.load());
    driveKnob->setRealValue(p.driveAmount.load());
    attackKnob->setRealValue(p.attack.load());
    decayKnob->setRealValue(p.decay.load());
    sustainKnob->setRealValue(p.sustain.load());
    releaseKnob->setRealValue(p.release.load());
    lfoRateKnob->setRealValue(p.lfoRateHz.load());
    lfoToFilterKnob->setRealValue(p.lfoDepth.load());
    lfoToAmpKnob->setRealValue(p.lfoAmpDepth.load());
    gainKnob->setRealValue(p.masterGain.load());
    lfoToWaveKnob->setRealValue(p.lfoToWave.load());
    modEnvAttackKnob->setRealValue(p.modEnvAttack.load());
    modEnvDecayKnob->setRealValue(p.modEnvDecay.load());
    modEnvSustainKnob->setRealValue(p.modEnvSustain.load());
    modEnvReleaseKnob->setRealValue(p.modEnvRelease.load());
    modEnvToWaveKnob->setRealValue(p.modEnvToWave.load());
    modEnvToCutoffKnob->setRealValue(p.modEnvToCutoff.load());
    lfo2RateKnob->setRealValue(p.lfo2RateHz.load());
    lfo2ToWaveKnob->setRealValue(p.lfo2ToWave.load());
    lfo2ToCutoffKnob->setRealValue(p.lfo2ToCutoff.load());
    fxDistortionKnob->setRealValue(p.fxDistortion.load());
    delayTimeKnob->setRealValue(p.delayTimeMs.load());
    delayFeedbackKnob->setRealValue(p.delayFeedback.load());
    delayMixKnob->setRealValue(p.delayMix.load());
    reverbSizeKnob->setRealValue(p.reverbSize.load());
    reverbMixKnob->setRealValue(p.reverbMix.load());
    chorusRateKnob->setRealValue(p.chorusRate.load());
    chorusDepthKnob->setRealValue(p.chorusDepth.load());
    chorusMixKnob->setRealValue(p.chorusMix.load());
    phaserRateKnob->setRealValue(p.phaserRate.load());
    phaserFeedbackKnob->setRealValue(p.phaserFeedback.load());
    phaserMixKnob->setRealValue(p.phaserMix.load());
    eqLowKnob->setRealValue(p.eqLowGain.load());
    eqMidKnob->setRealValue(p.eqMidGain.load());
    eqHighKnob->setRealValue(p.eqHighGain.load());
    glideKnob->setRealValue(p.glideTimeMs.load());
    velToCutoffKnob->setRealValue(p.velocityToCutoff.load());
    velToWaveKnob->setRealValue(p.velocityToWave.load());
    velToAmpKnob->setRealValue(p.velocityToAmp.load());
    velToResonanceKnob->setRealValue(p.velocityToResonance.load());
    accentKnob->setRealValue(p.accentAmount.load());
    wheelToCutoffKnob->setRealValue(p.wheelToCutoff.load());
    wheelToWaveKnob->setRealValue(p.wheelToWave.load());
    pressureToCutoffKnob->setRealValue(p.pressureToCutoff.load());
    monoButton.setToggleState(p.monoMode.load() > 0.5f, juce::dontSendNotification);
    legatoButton.setToggleState(p.legatoMode.load() > 0.5f, juce::dontSendNotification);
    if (arpLinkKnob != nullptr) arpLinkKnob->setRealValue(p.arpRateLink.load());

    // Forced, because a preset can change the rate without changing which
    // scale the dial is drawn on.
    arpRateScaleBuiltFor = -1;
    updateArpRateScale();
    arpOctavesKnob->setRealValue(p.arpOctaves.load());
    arpGateKnob->setRealValue(p.arpGate.load());
    arpSwingKnob->setRealValue(p.arpSwing.load());
    arpEnabledButton.setToggleState(p.arpEnabled.load() > 0.5f, juce::dontSendNotification);
    updateArpModeButton();
    updateArpPatternButton();
    bpmKnob->setRealValue(p.bpm.load());
    arpSyncButton.setToggleState(p.arpSync.load() > 0.5f, juce::dontSendNotification);
    delaySyncButton.setToggleState(p.delaySync.load() > 0.5f, juce::dontSendNotification);
    lfo1SyncButton.setToggleState(p.lfo1Sync.load() > 0.5f, juce::dontSendNotification);
    lfo2SyncButton.setToggleState(p.lfo2Sync.load() > 0.5f, juce::dontSendNotification);
    stereoLfoButton.setToggleState(p.stereoLfo.load() > 0.5f, juce::dontSendNotification);

    // The Master page owns its own dials, so it has to be told as well. Missing
    // this would be invisible until somebody loaded a patch while looking at
    // the Synth page and then switched over to find the previous patch's
    // settings still showing.
    if (drumPanel != nullptr)
        drumPanel->refresh();

    if (masteringPanel != nullptr)
        masteringPanel->refresh();

    if (busEffectsPanel != nullptr)
        busEffectsPanel->refresh();

    waveformDisplay.markDirty();
}

void WaveLatheEditor::savePresetClicked()
{
    auto defaultDir = folderFor(AppSettings::Folder::presets);
    defaultDir.createDirectory();

    fileChooser = std::make_unique<juce::FileChooser>("Save Synth Preset", defaultDir, "*.fxp");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
                return;
            if (file.getFileExtension().isEmpty())
                file = file.withFileExtension("fxp");

            rememberFolder(AppSettings::Folder::presets, file);

            // The Synth page's sound and nothing else. This saved the whole
            // project until 0.48.0 - pattern, kit, samples and Master page -
            // from before projects had a menu item of their own; that is what
            // Save Project is for now, and a preset that carried a beat was a
            // preset that could not be shared without sharing the beat.
            auto values = FxpPreset::synthPresetOnly(
                processorRef.captureProject(file.getFileNameWithoutExtension()));
            const bool embeddedWavetable = !values.wavetableData.empty();

            juce::String error;
            if (FxpPreset::save(file, values, error))
                lcd.print("Saved synth preset " + file.getFileName()
                          + (embeddedWavetable ? " + wavetable" : ""));
            else
                lcd.print("Save failed: " + error);
        });
}

void WaveLatheEditor::loadPresetClicked()
{
    auto defaultDir = folderFor(AppSettings::Folder::presets);

    fileChooser = std::make_unique<juce::FileChooser>("Load Synth Preset", defaultDir, "*.fxp");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
                return;

            rememberFolder(AppSettings::Folder::presets, file);

            PresetValues values;
            juce::String error;
            if (!FxpPreset::load(file, values, error))
            {
                lcd.print("Load failed: " + error);
                return;
            }

            // The sound and nothing else, whatever the file holds. A preset
            // saved by an older build carried the whole project; its pattern,
            // kit and Master page are in there and are deliberately left
            // there - Load Project is how a whole session comes back.
            processorRef.recordUndoPoint("Load Synth Preset");
            auto note = loadWavetableFromPreset(values);
            applyPresetValues(values, "Loaded synth preset " + file.getFileName() + note,
                              FxpPreset::ApplyScope::synthPreset);
        });
}

void WaveLatheEditor::refreshTransportButtons()
{
    const auto& p = processorRef.getParameters();
    bool playing = p.seqPlaying.load() > 0.5f;

    // One button doing both jobs, showing what it will do next - the shape you
    // see is the action, not the state.
    playStopButton.setShape(playing ? ui::TransportButton::Shape::stop
                                    : ui::TransportButton::Shape::play);
    playStopButton.setLit(playing);
    recordButton.setLit(p.seqRecord.load() > 0.5f);
}

void WaveLatheEditor::checkUndoShortcuts()
{
    auto modifiers = juce::ModifierKeys::getCurrentModifiers();

    bool undoDown = juce::KeyPress::isKeyCurrentlyDown('Z');
    bool redoDown = juce::KeyPress::isKeyCurrentlyDown('Y');

    bool allowed = !keyboardIsElsewhere() && isShowing() && juce::Process::isForegroundProcess()
                   && modifiers.isCommandDown();

    // Only on the way down, or holding the keys would run the whole history
    // backwards in a second.
    if (allowed && undoDown && !undoChordHeld)
        performUndo(modifiers.isShiftDown());
    else if (allowed && redoDown && !redoChordHeld)
        performUndo(true);

    // Tracked whether or not the shortcut was allowed, so a key already held
    // when the modifier goes down does not count as a fresh press.
    undoChordHeld = undoDown;
    redoChordHeld = redoDown;
}

void WaveLatheEditor::performUndo(bool redoInstead)
{
    PresetValues values;
    bool moved = redoInstead ? processorRef.redo(values) : processorRef.undo(values);

    if (!moved)
    {
        lcd.print(redoInstead ? "Nothing to redo." : "Nothing to undo.");
        return;
    }

    applyRestoredProject(values, (redoInstead ? juce::String("Redid ") : juce::String("Undid "))
                                     + values.name.toLowerCase());
}

// Putting a whole project back on screen: the parameters, the pattern, the
// lanes and everything drawn from them.
void WaveLatheEditor::applyRestoredProject(const PresetValues& values, const juce::String& message)
{
    FxpPreset::applyToSynthParameters(values, processorRef.getParameters());
    processorRef.getSequencer().restoreState(values.sequencer);

    // The sample slots and which drum each one is, both of which this used to
    // leave alone - so undoing a sample load put the parameters back, said so,
    // and left the sample on the pad. adoptLoadedPatch below is what makes the
    // strip headers follow.
    processorRef.restoreDrumSamples(values);

    for (int voice = 0; voice < project::numDrumVoices; ++voice)
        processorRef.setDrumEngine(voice, values.drumEngines[(size_t) voice]);

    // Applied without asking, unlike opening a project from disk. This path is
    // undo, redo and crash recovery - it is putting back a state that was
    // already yours a moment ago, not adopting one from somewhere else, so a
    // question here would be asking whether you meant your own last move.
    if (values.midiMap.isNotEmpty())
        adoptMidiMap(values.midiMap);

    if (values.keyboardChannel >= 0)
        adoptKeyboardChannel(values.keyboardChannel);

    if (sequencerPanel != nullptr)
        sequencerPanel->refreshFromProcessor();

    adoptLoadedPatch();
    waveformDisplay.markDirty();
    lcd.print(message);
}

void WaveLatheEditor::offerRecoveredProject()
{
    PresetValues values;
    juce::String error;

    if (!processorRef.loadRecoveredProject(values, error))
    {
        lcd.print("Nothing to recover: " + error);
        processorRef.discardRecovery();
        return;
    }

    // Recovering is itself undoable, so choosing it by mistake costs nothing.
    processorRef.recordUndoPoint("Recover autosave");
    applyRestoredProject(values, "Recovered the project from the last autosave.");
    processorRef.discardRecovery();
}

void WaveLatheEditor::showAudioSettings()
{
#if JucePlugin_Build_Standalone
    if (auto* holder = juce::StandalonePluginHolder::getInstance())
    {
        holder->showAudioSettingsDialog();
        return;
    }
#endif

    lcd.print("Audio and MIDI devices are the host's to choose when WaveLathe runs as a plugin.");
}

void WaveLatheEditor::hideWrapperOptionsButton()
{
    auto* top = getTopLevelComponent();
    if (top == nullptr || top == this)
        return;

    // The wrapper's button is a direct child of the window itself, which is
    // what separates it from the one in the panel - they share a name.
    for (auto* child : top->getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child))
            if (button->getButtonText() == "Options" && button->isVisible())
                button->setVisible(false);
}

void WaveLatheEditor::fitToOpeningDisplay()
{
    // Null when this editor IS the top level - hosted in a plugin window, or
    // not shown yet - and then the size is the host's business, not ours.
    auto* window = ownWindow();
    if (window == nullptr)
        return;

    // Which screen the window actually landed on, rather than which one is the
    // main one. The editor sizes itself in its constructor, before any window
    // exists to have been put anywhere, so it could only ask the main display -
    // and on a second monitor that arrived as 1922 pixels of window on a 1680
    // pixel screen, with the rest hanging over the edge onto the other one.
    //
    // getDisplayForRect picks the screen holding the largest part of the
    // window, which is the right answer for one straddling two of them.
    const auto& displays = juce::Desktop::getInstance().getDisplays();
    const auto* display = displays.getDisplayForRect(window->getScreenBounds());

    if (display == nullptr)
        display = displays.getPrimaryDisplay();

    if (display == nullptr)
        return;

    // The user area, not the whole screen: covering the taskbar's strip is not
    // filling the screen, it is hiding behind it.
    //
    // This also settles the position, since a window the size of the work area
    // has only one place to be - which is why nothing here tries to preserve
    // the saved one.
    window->setBoundsConstrained(display->userBounds.toNearestInt());
}

juce::DocumentWindow* WaveLatheEditor::ownWindow() const
{
    return dynamic_cast<juce::DocumentWindow*>(getTopLevelComponent());
}

void WaveLatheEditor::placeFullScreenButton()
{
    auto* window = ownWindow();

    // Hosted in a plugin: the window belongs to the host, so there is nothing
    // here for full screen to act ON and no button offering to. A control drawn
    // on the synth panel that reaches into the host's window is exactly the
    // confusion this button was moved into the title bar to avoid - and in Live
    // it did not merely fail, it asked for kiosk mode on Live's own window.
    //
    // The header keeps the space it reserved either way. That region is empty
    // between the tabs and the wordmark, so nothing moves.
    if (window == nullptr)
    {
        fullScreenButton.setVisible(false);
        return;
    }

    const auto titleBar = window->getTitleBarArea();

    // No title bar to sit in: full screen has just taken it away. The header
    // takes the button back, because the way out of full screen cannot be on
    // the bar that full screen removed.
    if (titleBar.isEmpty())
    {
        if (fullScreenButton.getParentComponent() != &content)
        {
            content.addAndMakeVisible(fullScreenButton);
            fullScreenButton.toFront(false);
        }

        fullScreenButton.setVisible(true);
        fullScreenButton.setBounds(headerFullScreenBounds);
        return;
    }

    if (fullScreenButton.getParentComponent() != window)
        window->addAndMakeVisible(fullScreenButton);

    fullScreenButton.setVisible(true);

    // Immediately left of whichever buttons this window actually has, found by
    // asking them rather than by counting them: the standalone wrapper decides
    // that, and a hard-coded two would put this one through the middle of a
    // third the first time it changed its mind.
    int leftmost = titleBar.getRight();

    for (auto* button : {window->getMinimiseButton(), window->getMaximiseButton(),
                         window->getCloseButton()})
        if (button != nullptr && button->isVisible())
            leftmost = juce::jmin(leftmost, button->getX());

    // The same cell size they use, so the row reads as one group of buttons
    // rather than as two things that ended up next to each other.
    const int cell = (int) (titleBar.getHeight() * 1.2);
    fullScreenButton.setBounds(leftmost - cell, titleBar.getY(), cell, titleBar.getHeight());
}

void WaveLatheEditor::saveProjectClicked()
{
    auto defaultDir = folderFor(AppSettings::Folder::projects);
    defaultDir.createDirectory();

    fileChooser = std::make_unique<juce::FileChooser>("Save WaveLathe Project", defaultDir,
                                                      project::wildcard());
    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::warnAboutOverwriting,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
                return;

            // Typing a bare name still gets the extension, so a project is
            // always something the chooser will offer you again next time.
            if (!file.getFileExtension().equalsIgnoreCase(juce::String(".") + project::fileExtension))
                file = file.withFileExtension(project::fileExtension);

            rememberFolder(AppSettings::Folder::projects, file);

            auto values = processorRef.captureProject(file.getFileNameWithoutExtension());

            juce::String error;
            if (!FxpPreset::save(file, values, error))
            {
                lcd.print("Save failed: " + error);
                return;
            }

            lcd.print("Saved project " + file.getFileName());
            lcd.print(describeSavedProject(values));
        });
}

juce::String WaveLatheEditor::describeSavedProject(const PresetValues& values)
{
    // Every page's settings always go in - that is what a project is - so the
    // line is about the work on top of them. The beat counts: a project that
    // was only drums used to be reported as an empty sequencer.
    int notes = 0;
    for (const auto& step : values.sequencer.steps)
        if (step.active != 0)
            ++notes;

    int drumHits = 0;
    for (const auto& voice : values.sequencer.drumVoices)
        for (const auto& cell : voice.cells)
            if (cell.active != 0)
                ++drumHits;

    int samples = 0;
    for (const auto& sample : values.drumSamples)
        if (sample != nullptr)
            ++samples;

    const auto count = [](int n, const juce::String& thing)
    { return juce::String(n) + " " + thing + (n == 1 ? "" : "s"); };

    juce::StringArray carried;
    if (notes > 0)
        carried.add(count(notes, "note"));
    if (drumHits > 0)
        carried.add(count(drumHits, "drum hit"));
    if (!values.sequencer.lanes.empty())
        carried.add(count((int) values.sequencer.lanes.size(), "automation lane"));
    if (samples > 0)
        carried.add(count(samples, "drum sample"));
    if (!values.wavetableData.empty())
        carried.add("the sampled wavetable");

    if (carried.isEmpty())
        return "Every page's settings - the pattern is empty.";

    juce::String list;
    for (int i = 0; i < carried.size(); ++i)
        list << (i == 0 ? "" : i == carried.size() - 1 ? " and " : ", ") << carried[i];

    return "Every page's settings, with " + list + ".";
}

void WaveLatheEditor::loadProjectClicked()
{
    auto defaultDir = folderFor(AppSettings::Folder::projects);

    fileChooser = std::make_unique<juce::FileChooser>("Load WaveLathe Project", defaultDir,
                                                      project::wildcard());
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
                return;

            rememberFolder(AppSettings::Folder::projects, file);

            PresetValues values;
            juce::String error;
            if (!FxpPreset::load(file, values, error))
            {
                lcd.print("Load failed: " + error);
                return;
            }

            processorRef.recordUndoPoint("Load Project");
            auto note = loadWavetableFromPreset(values);
            applyPresetValues(values, "Loaded project " + file.getFileName() + note,
                              FxpPreset::ApplyScope::project);

            if (values.sequencer.isEmpty())
                lcd.print("Saved before projects carried the sequencer - the pattern is untouched.");

            // Asked rather than applied, unlike the pattern above. The pattern
            // belongs to the project; the assignments belong to the desk the
            // project is being opened ON, which may not be the desk it was
            // saved at - someone else's controller layout is not an improvement
            // on your own.
            offerMidiSetupFromProject(values);
        });
}

juce::String WaveLatheEditor::loadWavetableFromPreset(const PresetValues& values)
{
    if (values.wavetableData.empty())
    {
        processorRef.revertToBuiltInWavetables();
        return {};
    }

    juce::String tableError;
    if (processorRef.loadWavetableFromRaw(values.wavetableData, values.name, tableError))
        return " + wavetable";

    return " (wavetable unreadable)";
}

void WaveLatheEditor::matchSampleClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>("Quick Match Audio Sample", folderFor(AppSettings::Folder::quickMatch),
                                                       "*.wav;*.aif;*.aiff;*.flac");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
                return;

            rememberFolder(AppSettings::Folder::quickMatch, file);

            PresetValues estimate;
            juce::String error;
            if (!SampleMatch::matchFile(file, file.getFileNameWithoutExtension(), -1, estimate, error))
            {
                lcd.print("Match failed: " + error);
                return;
            }

            // The estimate reads a handful of features off the recording and
            // turns them into settings. It is quick, and it is usually beaten:
            // measured against a known reference, every one of the shipped
            // presets came closer to it than the estimate did. So rather than
            // trusting the estimate outright, both it and the whole bank are
            // played at the sample's pitch and actually compared. Forty-odd
            // renders is a fraction of a second, which still leaves this the
            // quick option next to Deep Match's full search.
            PresetValues chosen = estimate;
            juce::String verdict;

            SampleMatch::MatchContext context;
            if (SampleMatch::analyzeFile(file, -1, context, error) && context.chosenIndex >= 0)
            {
                const auto& report = context.reports[(size_t) context.chosenIndex];
                int midiNote = SampleMatch::midiNoteForFrequency(report.fundamentalHz);

                if (midiNote > 0)
                {
                    // The same window Deep Match measures, tail included, so the
                    // release is part of what is being judged.
                    const auto& seg = context.segments[(size_t) context.chosenIndex];
                    float holdRatio = 0.85f;
                    int length = SampleMatch::windowWithTail(context, context.chosenIndex, 0.8, holdRatio);

                    auto ranked = SoundMatch::auditionFactoryPresets(
                        processorRef.getActiveWavetables(), context.mono.getReadPointer(0) + seg.startSample,
                        length, context.sampleRate, midiNote, &estimate, holdRatio);

                    if (!ranked.empty() && ranked.front().bankIndex >= 0)
                    {
                        const auto& winner = FactoryPresets::all()[(size_t) ranked.front().bankIndex];
                        chosen = winner.values;

                        // The name the user asked for, not the preset's - what
                        // they loaded was their sample, matched.
                        chosen.name = file.getFileNameWithoutExtension();
                        verdict = "Nearest: " + winner.name;
                    }
                    else
                    {
                        verdict = "Nearest: the estimate, which beat every preset";
                    }
                }
            }

            processorRef.recordUndoPoint("Quick Match");

            // A synth preset, as the match is - Deep Match's below too. They
            // were applied as whole projects, and what they apply is either
            // the estimate or a factory patch, both of which carry an
            // untouched kit, a dry bus and mastering switched off. So matching
            // a sound put the drum mixer, the rack, the bus and the mastering
            // chain back to their defaults underneath whatever you had built.
            FxpPreset::applyToSynthParameters(chosen, processorRef.getParameters(),
                                              FxpPreset::ApplyScope::synthPreset);
            adoptLoadedPatch();

            lcd.print("Quick match: " + file.getFileName());
            lcd.print(verdict.isNotEmpty()
                          ? verdict + ". Deep Match fits closer."
                          : "Cutoff " + formatHz(chosen.filterCutoffHz) + ", "
                                + formatVoices(chosen.unisonVoices) + " voices. Deep Match fits closer.");
        });
}

void WaveLatheEditor::sampleWavetableClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>("Sample Wavetable From Audio", folderFor(AppSettings::Folder::sampleWavetable),
                                                       "*.wav;*.aif;*.aiff;*.flac");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
                return;

            rememberFolder(AppSettings::Folder::sampleWavetable, file);

            SampleMatch::MatchContext context;
            juce::String error;
            if (!SampleMatch::analyzeFile(file, -1, context, error))
            {
                lcd.print("Wavetable sampling failed: " + error);
                return;
            }

            const auto& seg = context.segments[(size_t) context.chosenIndex];
            const auto& report = context.reports[(size_t) context.chosenIndex];

            if (!processorRef.loadSampledWavetable(context.mono.getReadPointer(0) + seg.startSample,
                                                    seg.endSample - seg.startSample, context.sampleRate,
                                                    report.fundamentalHz, file.getFileNameWithoutExtension(), error))
            {
                lcd.print("Wavetable sampling failed: " + error);
                return;
            }

            waveformDisplay.markDirty();
            lcd.print("Sampled wavetable: " + file.getFileName() + " ("
                      + SampleMatch::noteName(report.fundamentalHz) + ")");
            lcd.print("Wave dial morphs through the note.");
        });
}

void WaveLatheEditor::exportAbClicked()
{
    fileChooser = std::make_unique<juce::FileChooser>("Choose the reference to compare against", folderFor(AppSettings::Folder::exportAb),
                                                       "*.wav;*.aif;*.aiff;*.flac");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
                return;

            rememberFolder(AppSettings::Folder::exportAb, file);

            SampleMatch::MatchContext context;
            juce::String error;
            if (!SampleMatch::analyzeFile(file, -1, context, error))
            {
                lcd.print("A/B failed: " + error);
                return;
            }

            const auto& seg = context.segments[(size_t) context.chosenIndex];
            const auto& report = context.reports[(size_t) context.chosenIndex];
            int midiNote = SampleMatch::midiNoteForFrequency(report.fundamentalHz);
            if (midiNote <= 0)
            {
                lcd.print("A/B failed: no clear pitch in " + file.getFileName());
                return;
            }

            int segLength = seg.endSample - seg.startSample;
            double durationSeconds = segLength / context.sampleRate;

            OfflineRenderer renderer(processorRef.getActiveWavetables(), context.sampleRate);
            auto values = FxpPreset::fromSynthParameters(processorRef.getParameters(), "current");
            juce::AudioBuffer<float> rendered;
            renderer.render(values, midiNote, durationSeconds, rendered);

            auto outFile = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                               .getChildFile("WaveLathe Presets")
                               .getChildFile("ab_" + file.getFileNameWithoutExtension() + ".wav");

            juce::String writeError;
            if (SampleMatch::writeAbComparison(outFile, context.mono.getReadPointer(0) + seg.startSample, segLength,
                                                rendered.getReadPointer(0), rendered.getNumSamples(),
                                                context.sampleRate, writeError))
            {
                lcd.print("A/B written: " + outFile.getFileName());
                lcd.print("Reference first, then the match - listen and judge.");
            }
            else
            {
                lcd.print("A/B failed: " + writeError);
            }
        });
}

void WaveLatheEditor::deepMatchClicked()
{
    if (optimizerRunning.load())
    {
        stopOptimizerThread();
        lcd.print("Deep match cancelled.");
        return;
    }

    fileChooser = std::make_unique<juce::FileChooser>("Deep Match Audio Sample", folderFor(AppSettings::Folder::deepMatch),
                                                       "*.wav;*.aif;*.aiff;*.flac");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
        [this](const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File{})
                return;
            rememberFolder(AppSettings::Folder::deepMatch, file);
            startDeepMatch(file);
        });
}

void WaveLatheEditor::startDeepMatch(const juce::File& file)
{
    stopOptimizerThread();

    SampleMatch::MatchContext context;
    juce::String error;
    if (!SampleMatch::analyzeFile(file, -1, context, error))
    {
        lcd.print("Deep match failed: " + error);
        return;
    }

    const auto& chosenReport = context.reports[(size_t) context.chosenIndex];
    int midiNote = SampleMatch::midiNoteForFrequency(chosenReport.fundamentalHz);
    if (midiNote <= 0)
    {
        lcd.print("Deep match failed: no clear pitch in " + file.getFileName());
        return;
    }

    PresetValues seed;
    if (!SampleMatch::matchFile(file, file.getFileNameWithoutExtension(), -1, seed, error))
    {
        lcd.print("Deep match failed: " + error);
        return;
    }

    optimizerCancel = std::make_shared<std::atomic<bool>>(false);
    optimizerRunning = true;
    optimizerGeneration = 0;

    juce::Component::SafePointer<WaveLatheEditor> safeThis(this);
    auto cancelFlag = optimizerCancel;
    auto fileName = file.getFileName();
    const auto& tables = processorRef.getActiveWavetables();
    double sampleRate = context.sampleRate;

    // ---- Which notes to fit ------------------------------------------------
    // Fitted to one note, the search can put the filter in the wrong place and
    // bend the tone controls to cancel it: the result measures well on that one
    // note and is wrong across the rest of the keyboard. Shown several pitches,
    // it cannot, because a filter cuts the harmonic series somewhere different
    // at every fundamental. Measured against a known patch, three pitches beat
    // one at a pitch neither had seen - and beat one pitch given three times
    // the search budget, which recovered the filter worse than the short run
    // because it simply overfitted harder.
    //
    // Widely spaced pitches say the most, so they are taken in order of
    // distance from the note already chosen, and only notes with a clear pitch
    // and no competing tone are used.
    std::vector<int> chosenSegments{context.chosenIndex};
    {
        std::vector<std::pair<int, int>> byDistance; // (semitones away, index)
        for (int i = 0; i < (int) context.reports.size(); ++i)
        {
            if (i == context.chosenIndex)
                continue;

            int note = SampleMatch::midiNoteForFrequency(context.reports[(size_t) i].fundamentalHz);
            if (note <= 0 || std::abs(note - midiNote) < 2)
                continue; // no pitch, or too close to say anything new

            byDistance.emplace_back(std::abs(note - midiNote), i);
        }

        std::sort(byDistance.begin(), byDistance.end(),
                  [](const auto& a, const auto& b) { return a.first > b.first; });

        for (const auto& entry : byDistance)
        {
            if ((int) chosenSegments.size() >= 3)
                break;
            chosenSegments.push_back(entry.second);
        }
    }

    // Each note carries its own window and its own tail, so every release is
    // measured rather than guessed.
    struct HeldNote
    {
        std::shared_ptr<juce::AudioBuffer<float>> audio;
        int midiNote = 60;
        float holdRatio = 0.85f;
    };

    auto heldNotes = std::make_shared<std::vector<HeldNote>>();
    for (int index : chosenSegments)
    {
        const auto& seg = context.segments[(size_t) index];

        HeldNote held;
        held.midiNote = SampleMatch::midiNoteForFrequency(context.reports[(size_t) index].fundamentalHz);
        int length = SampleMatch::windowWithTail(context, index, 0.8, held.holdRatio);
        held.audio = std::make_shared<juce::AudioBuffer<float>>(1, length);
        held.audio->copyFrom(0, 0, context.mono, 0, seg.startSample, length);
        heldNotes->push_back(std::move(held));
    }

    // Every note is rendered for every candidate, so the budget is shared out
    // between them rather than multiplied by them - at equal cost the extra
    // pitches were worth far more than the extra generations would have been.
    int generations = juce::jmax(60, 200 / (int) heldNotes->size());
    optimizerTotalGenerations = generations;

    juce::String noteList;
    for (size_t i = 0; i < heldNotes->size(); ++i)
        noteList += (i > 0 ? ", " : "")
                    + SampleMatch::noteName(context.reports[(size_t) chosenSegments[i]].fundamentalHz);

    lcd.print("Deep matching " + file.getFileName() + " (" + noteList + ")");
    lcd.print("");

    optimizerThread = std::make_unique<std::thread>(
        [this, safeThis, cancelFlag, heldNotes, sampleRate, seed, fileName, generations, &tables]()
        {
            std::vector<SoundMatch::ReferenceNote> notes;
            for (const auto& held : *heldNotes)
            {
                SoundMatch::ReferenceNote note;
                note.mono = held.audio->getReadPointer(0);
                note.numSamples = held.audio->getNumSamples();
                note.midiNoteNumber = held.midiNote;
                note.holdRatio = held.holdRatio;
                notes.push_back(note);
            }

            SoundMatch::OptimizerSettings settings;
            settings.generations = generations;
            settings.cancelFlag = cancelFlag;

            auto result = SoundMatch::optimize(
                tables, notes, sampleRate, seed, settings,
                [this](int gen, int total, float best)
                {
                    optimizerGeneration = gen;
                    optimizerTotalGenerations = total;
                    optimizerBestDistance = best;
                });

            if (cancelFlag->load())
                return;

            juce::MessageManager::callAsync(
                [safeThis, result, fileName]()
                {
                    if (safeThis == nullptr)
                        return;

                    FxpPreset::applyToSynthParameters(result.best, safeThis->processorRef.getParameters(),
                                                      FxpPreset::ApplyScope::synthPreset);
                    safeThis->adoptLoadedPatch();
                    safeThis->optimizerRunning = false;

                    double improvement = result.startingDistance > 0.0f
                                             ? 100.0 * (result.startingDistance - result.bestDistance)
                                                   / result.startingDistance
                                             : 0.0;
                    safeThis->lcd.printTransient("Matched " + fileName + " - " + juce::String(improvement, 1)
                                                 + "% closer (nearest: " + result.closestPresetName + ", "
                                                 + juce::String(result.evaluations) + " renders)");
                });
        });
}

void WaveLatheEditor::stopOptimizerThread()
{
    if (optimizerCancel != nullptr)
        optimizerCancel->store(true);

    if (optimizerThread != nullptr && optimizerThread->joinable())
        optimizerThread->join();

    optimizerThread.reset();
    optimizerCancel.reset();
    optimizerRunning = false;
}

void WaveLatheEditor::shiftOctave(int semitones)
{
    // Stopped at C4, because five octaves from there ends on B8 and the next
    // step up would run the top of the keyboard past note 127 and draw blank
    // board where the keys ran out. Nothing is lost: what is above B8 is eight
    // kilohertz of squeak, and a controller can still send it.
    //
    // How far it ACTUALLY moved, not how far it was asked to. Everything below
    // follows the keys, so clamping each of them separately would leave the
    // letter keys playing one octave and the keyboard showing another as soon
    // as one of them hit its end first.
    const int moved = juce::jlimit(0, 60, lowestVisibleKey + semitones) - lowestVisibleKey;
    if (moved == 0)
        return;

    lowestVisibleKey += moved;
    keyboardComponent.setLowestVisibleKey(lowestVisibleKey);

    // The computer keyboard moves with the buttons too, so the letter keys
    // always play the octave the on-screen keyboard is showing.
    computerKeyBaseNote = juce::jlimit(0, 115, computerKeyBaseNote + moved);
    updateComputerKeyboardNotes(true);

    // Carry the note being played along with the view, so stepping octaves
    // auditions the same patch higher or lower instead of leaving the sounding
    // note behind in a range that is no longer on screen.
    int shifted = juce::jlimit(0, 127, lastClickedNote.load() + moved);
    lastClickedNote.store(shifted);


    lcd.printTransient("Octave: A-L keys play from "
                       + juce::MidiMessage::getMidiNoteName(computerKeyBaseNote, true, true, 3));
}

bool WaveLatheEditor::isFullScreen() const
{
    auto* window = ownWindow();
    return window != nullptr && juce::Desktop::getInstance().getKioskModeComponent() == window;
}

void WaveLatheEditor::setFullScreen(bool shouldBeFullScreen)
{
    // Nothing of ours to make full screen. Guarded here as well as at the
    // button and the menu, because Escape and F11 reach this directly - and
    // this is the call that did the damage: the old guard only refused when
    // there was no window AT ALL, so in a host it sailed past and handed the
    // host's own window to kiosk mode.
    auto* window = ownWindow();
    if (window == nullptr)
        return;

    // Kiosk mode rather than the window's own maximise. Maximising is already
    // on the title bar and leaves that bar and the taskbar sitting over the
    // synth; this is the state the title bar cannot give you - the whole
    // display, nothing else on it. JUCE remembers the window's bounds and puts
    // them back on the way out.
    if (shouldBeFullScreen == isFullScreen())
        return;

    juce::Desktop::getInstance().setKioskModeComponent(shouldBeFullScreen ? window : nullptr, false);

    fullScreenButton.setExpanded(shouldBeFullScreen);

    // Straight away rather than on the next tick, so the button does not spend
    // a frame sitting where the title bar used to be.
    placeFullScreenButton();

    // Said out loud, because the way back out is not where Windows usually
    // puts it - the title bar it would be on is exactly what has just gone.
    lcd.print(shouldBeFullScreen ? "Full screen. Escape or F11 to come back."
                                 : "Full screen off.");
}

void WaveLatheEditor::toggleFullScreen()
{
    setFullScreen(!isFullScreen());
}

void WaveLatheEditor::updateArpModeButton()
{
    int mode = juce::jlimit(0, Arpeggiator::numModes - 1,
                            (int) std::round(processorRef.getParameters().arpMode.load()));
    arpModeButton.setButtonText(Arpeggiator::modeName(mode));
}

void WaveLatheEditor::showArpModeMenu()
{
    // Headings mirror the grouping in the Mode enum, so twenty-four patterns
    // stay findable instead of becoming one undifferentiated list.
    struct ModeGroup
    {
        const char* title;
        Arpeggiator::Mode firstMode;
    };

    static const ModeGroup groups[] = {
        {"Direction", Arpeggiator::Mode::up},
        {"Repeats & runs", Arpeggiator::Mode::upTwice},
        {"Shapes", Arpeggiator::Mode::converge},
        {"Pedal note", Arpeggiator::Mode::pinkyUp},
        {"As played", Arpeggiator::Mode::playOrder},
        {"Random", Arpeggiator::Mode::randomOnce}
    };

    int current = juce::jlimit(0, Arpeggiator::numModes - 1,
                               (int) std::round(processorRef.getParameters().arpMode.load()));

    juce::PopupMenu menu;
    menu.setLookAndFeel(&lookAndFeel);

    int nextGroup = 0;
    for (int mode = 0; mode < Arpeggiator::numModes; ++mode)
    {
        if (nextGroup < (int) juce::numElementsInArray(groups)
            && (int) groups[nextGroup].firstMode == mode)
            menu.addSectionHeader(groups[nextGroup++].title);

        menu.addItem(mode + 1, Arpeggiator::modeName(mode), true, mode == current);
    }

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(arpModeButton)
                           .withMinimumWidth(170),
                       [this](int result)
                       {
                           if (result <= 0)
                               return;

                           int chosen = result - 1;
                           processorRef.getParameters().arpMode = (float) chosen;
                           updateArpModeButton();
                           lcd.print(juce::String("Arp order: ") + Arpeggiator::modeName(chosen));
                           lcd.print(Arpeggiator::modeDescription(chosen));
                       });
}

void WaveLatheEditor::registerAutomatableKnobs()
{
    automatedKnobs.clear();

    // Paired by registry name rather than by the dial's label, because labels
    // repeat across sections - there are three dials called "Rate".
    auto add = [this](const std::unique_ptr<ui::ParameterKnob>& knob, const char* registryName)
    {
        int id = paramreg::idForName(registryName);
        if (knob != nullptr && id >= 0)
            automatedKnobs.push_back({knob.get(), id});
    };

    add(waveKnob, "Wave");
    add(voicesKnob, "Voices");
    add(detuneKnob, "Detune");
    add(widthKnob, "Width");
    add(osc1LevelKnob, "Osc1 Level");

    add(osc2LevelKnob, "Osc2 Level");
    add(osc2WaveKnob, "Osc2 Wave");
    add(osc2SemiKnob, "Osc2 Semi");
    add(osc2FineKnob, "Osc2 Fine");

    add(subLevelKnob, "Sub");
    add(subWaveKnob, "Sub Wave");
    add(noiseLevelKnob, "Noise");
    add(noiseColourKnob, "Colour");

    add(cutoffKnob, "Cutoff");
    add(resonanceKnob, "Reso");
    add(driveKnob, "Drive");

    add(attackKnob, "Attack");
    add(decayKnob, "Decay");
    add(sustainKnob, "Sustain");
    add(releaseKnob, "Release");

    add(lfoRateKnob, "LFO Rate");
    add(lfoToFilterKnob, "To Filter");
    add(lfoToAmpKnob, "To Amp");
    add(lfoToWaveKnob, "To Wave");

    add(lfo2RateKnob, "LFO2 Rate");
    add(lfo2ToWaveKnob, "LFO2 Wave");
    add(lfo2ToCutoffKnob, "LFO2 Cutoff");

    add(fxDistortionKnob, "Distortion");
    add(delayMixKnob, "Delay Mix");
    add(delayFeedbackKnob, "Delay Fbk");
    add(reverbMixKnob, "Reverb Mix");
    add(gainKnob, "Gain");

    // The dials that had no id until the registry grew past thirty-two. They
    // are automatable now for the same reason they are learnable: there was
    // never a reason for them not to be, only a bit of room.
    add(glideKnob, "Glide");
    add(subOctaveKnob, "Sub Oct");
    add(filterTypeKnob, "Filter Type");

    add(modEnvAttackKnob, "Mod Atk");
    add(modEnvDecayKnob, "Mod Dec");
    add(modEnvSustainKnob, "Mod Sus");
    add(modEnvReleaseKnob, "Mod Rel");
    add(modEnvToWaveKnob, "Mod>Wave");
    add(modEnvToCutoffKnob, "Mod>Cutoff");

    add(delayTimeKnob, "Delay Time");
    add(reverbSizeKnob, "Reverb Size");

    add(chorusRateKnob, "Chorus Rate");
    add(chorusDepthKnob, "Chorus Dep");
    add(chorusMixKnob, "Chorus Mix");

    add(phaserRateKnob, "Phaser Rate");
    add(phaserFeedbackKnob, "Phaser Fbk");
    add(phaserMixKnob, "Phaser Mix");

    add(eqLowKnob, "EQ Low");
    add(eqMidKnob, "EQ Mid");
    add(eqHighKnob, "EQ High");

    add(velToCutoffKnob, "Vel>Cut");
    add(velToWaveKnob, "Vel>Wave");
    add(velToAmpKnob, "Vel>Amp");
    add(velToResonanceKnob, "Vel>Res");
    add(accentKnob, "Accent");

    add(wheelToCutoffKnob, "Whl>Cut");
    add(wheelToWaveKnob, "Whl>Wave");
    add(pressureToCutoffKnob, "Press>Cut");

    add(arpOctavesKnob, "Arp Octaves");
    add(arpGateKnob, "Arp Gate");
    add(arpSwingKnob, "Arp Swing");

    // The Drums page's hundred and eight, folded in from the panel that owns
    // them. Nine controls on each of twelve strips since 0.44.0, where it was
    // four on each of sixteen when this was written.
    //
    // It has to be this way round. MIDI learn and the host both find a dial by
    // walking this list, so a panel that kept its knobs to itself would have a
    // hundred and eight registered parameters that no controller could be
    // learned onto and that the learn overlay would draw nothing over -
    // automatable from the host and untouchable from here.
    if (drumPanel != nullptr)
        for (const auto& pair : drumPanel->automatableKnobs())
            automatedKnobs.push_back(pair);
}

void WaveLatheEditor::followAutomation()
{
    auto& sequencer = processorRef.getSequencer();
    const auto& params = processorRef.getParameters();

    for (const auto& pair : automatedKnobs)
    {
        auto* knob = pair.first;
        int id = pair.second;

        // Hand on the dial wins: the sequencer records it and leaves it alone.
        sequencer.setParameterTouched(id, knob->isRecentlyMoved());

        if (knob->isRecentlyMoved())
            continue;

        // Otherwise the dial shows whatever the parameter actually says, so
        // anything moving the synth from elsewhere is visible rather than
        // happening silently behind the panel.
        //
        // This used to ask WHICH of two things was driving - a recorded lane or
        // a learned controller - and a host automating the parameter was
        // neither, so its dials sat still while the sound changed underneath
        // them. Worse than cosmetic, and the same bug as 0.27.1 with a
        // different driver: the next touch of a stale dial snaps the sound back
        // to where the dial was, undoing whatever the host had done.
        //
        // Asking the parameter instead of enumerating its drivers means the
        // next way of writing one is followed without anybody remembering to
        // come back here.
        knob->setRealValue((double) paramreg::valueOf(params, id).load());
    }
}

void WaveLatheEditor::rebuildMappedParameters()
{
    const auto& map = processorRef.getMidiMap();

    parameterIsMapped.fill(false);

    for (int cc = 0; cc < MidiMap::numControllers; ++cc)
    {
        int id = map.parameterFor(cc);
        if (id >= 0 && id < (int) parameterIsMapped.size())
            parameterIsMapped[(size_t) id] = true;
    }
}

void WaveLatheEditor::setPage(int page)
{
    currentPage = juce::jlimit((int) synthPage, (int) masterPage, page);

    for (auto* component : synthPageComponents)
        if (component != nullptr)
            component->setVisible(currentPage == synthPage);

    // Each page is refreshed as it is shown rather than kept in step while
    // hidden. A preset loaded while the Master page is behind the Synth page
    // would otherwise leave its dials showing the patch before last.
    if (sequencerPanel != nullptr)
    {
        sequencerPanel->setVisible(currentPage == sequencerPage);
        if (currentPage == sequencerPage)
            sequencerPanel->refreshFromProcessor();
    }

    if (drumPanel != nullptr)
    {
        drumPanel->setVisible(currentPage == drumsPage);
        if (currentPage == drumsPage)
            drumPanel->refresh();
    }

    // Leaving the Drums page puts the displays back on the patch. They follow
    // the page rather than latching: a sawtooth drawn above the drum mixer is
    // the wrong instrument, and a kick drawn above the filter section is the
    // same mistake the other way round.
    //
    // The Master page has its own reading of "the sound": the patch as it comes
    // out of the bus effects and the mastering chain, drawn over the patch as
    // it went in - so the picture is of what THIS page is doing to it.
    if (currentPage == drumsPage)
        waveformDisplay.showDrumSlot(drumPanel != nullptr ? drumPanel->lastTouched() : 0);
    else if (currentPage == masterPage)
        waveformDisplay.showMaster();
    else
        waveformDisplay.showSynth();

    // And the spectrum's EQ curve is the EQ on the page you are looking at.
    outputScope.showBusEq(currentPage == masterPage);

    if (masteringPanel != nullptr)
    {
        masteringPanel->setVisible(currentPage == masterPage);
        if (currentPage == masterPage)
            masteringPanel->refresh();
    }

    if (busEffectsPanel != nullptr)
    {
        busEffectsPanel->setVisible(currentPage == masterPage);
        if (currentPage == masterPage)
            busEffectsPanel->refresh();
    }

    pageTabs.setSelected(currentPage, juce::dontSendNotification);

    // The wash over the keyboard and the arpeggiator goes on and off with the
    // page, and nothing else on the content is repainting to carry it.
    content.repaint(playingStripBounds);

    // Whatever the computer keys were holding is let go of AT the change
    // rather than at the next timer tick. The two sets of keys overlap - W, E,
    // T, Y, U, O and P are notes on one page and drums on the other - so a key
    // held across the change would have its release read by the wrong handler
    // and the note would sound until something else happened to clear it.
    releaseComputerKeyboardNotes();
    std::fill(drumKeyHeld.begin(), drumKeyHeld.end(), false);

    switch (currentPage)
    {
        case sequencerPage:
            lcd.print("Sequencer page. Press Play, or Generate a line to start from.");
            break;
        case drumsPage:
            lcd.print("Drums page. Each voice runs through FX 1 then FX 2 - Amt is how much.");
            lcd.print("Q to ] plays the twelve slots left to right. Hold Shift for an accent.");
            break;
        case masterPage:
            lcd.print("Master page. Bus effects over synth and drums, then the mastering chain.");
            break;
        default:
            lcd.print("Synth page.");
            break;
    }

    content.repaint();
    layoutContent();
}

void WaveLatheEditor::gatherSynthPageComponents()
{
    synthPageComponents.clear();

    for (auto* section : {&oscSection, &osc2Section, &subNoiseSection, &filterSection, &driveSection,
                          &ampSection, &lfoSection, &distortionSection, &chorusSection, &phaserSection,
                          &delaySection, &reverbSection, &eqSection, &expressionSection,
                          &modEnvSection, &lfo2Section, &outSection})
        synthPageComponents.push_back(section);

    synthPageComponents.push_back(&stereoLfoButton);
    synthPageComponents.push_back(&monoButton);
    synthPageComponents.push_back(&legatoButton);
    synthPageComponents.push_back(&delaySyncButton);
    synthPageComponents.push_back(&lfo1SyncButton);
    synthPageComponents.push_back(&lfo2SyncButton);

    for (auto* knob : {waveKnob.get(), voicesKnob.get(), detuneKnob.get(), widthKnob.get(), osc1LevelKnob.get(),
                       osc2LevelKnob.get(), osc2WaveKnob.get(), osc2SemiKnob.get(), osc2FineKnob.get(),
                       subLevelKnob.get(), subWaveKnob.get(), subOctaveKnob.get(),
                       noiseLevelKnob.get(), noiseColourKnob.get(),
                       cutoffKnob.get(), resonanceKnob.get(), filterTypeKnob.get(), driveKnob.get(),
                       attackKnob.get(), decayKnob.get(), sustainKnob.get(), releaseKnob.get(),
                       lfoRateKnob.get(), lfoToFilterKnob.get(), lfoToAmpKnob.get(), lfoToWaveKnob.get(),
                       modEnvAttackKnob.get(), modEnvDecayKnob.get(), modEnvSustainKnob.get(),
                       modEnvReleaseKnob.get(), modEnvToWaveKnob.get(), modEnvToCutoffKnob.get(),
                       lfo2RateKnob.get(), lfo2ToWaveKnob.get(), lfo2ToCutoffKnob.get(),
                       gainKnob.get(), fxDistortionKnob.get(), delayTimeKnob.get(), delayFeedbackKnob.get(),
                       delayMixKnob.get(), reverbSizeKnob.get(), reverbMixKnob.get(),
                       chorusRateKnob.get(), chorusDepthKnob.get(), chorusMixKnob.get(),
                       phaserRateKnob.get(), phaserFeedbackKnob.get(), phaserMixKnob.get(),
                       eqLowKnob.get(), eqMidKnob.get(), eqHighKnob.get(), glideKnob.get(),
                       velToCutoffKnob.get(), velToWaveKnob.get(), velToAmpKnob.get(),
                       velToResonanceKnob.get(), accentKnob.get(),
                       wheelToCutoffKnob.get(), wheelToWaveKnob.get(), pressureToCutoffKnob.get()})
        synthPageComponents.push_back(knob);
}

void WaveLatheEditor::showOptionsMenu()
{
    auto& params = processorRef.getParameters();

    int currentKey = juce::jlimit(0, music::numKeys - 1, (int) std::round(params.musicalKey.load()));
    int currentScale = juce::jlimit(0, (int) music::Scale::numScales - 1,
                                     (int) std::round(params.musicalScale.load()));
    int currentChord = juce::jlimit(0, (int) music::ChordMode::numModes - 1,
                                     (int) std::round(params.chordMode.load()));

    // Deep match runs for a long time, so its item doubles as the way to stop
    // one - the job the old button's changing label used to do.
    bool matching = optimizerRunning.load();

    juce::PopupMenu menu;
    menu.setLookAndFeel(&lookAndFeel);

    // ---- Presets ----------------------------------------------------------
    // Undo at the top, where every menu keeps it, naming what it will reverse
    // so it is clear what is about to come back.
    const auto& history = processorRef.getHistory();
    menu.addItem(undoAction, history.canUndo() ? "Undo " + history.undoName() : juce::String("Undo"),
                 history.canUndo());
    menu.addItem(redoAction, history.canRedo() ? "Redo " + history.redoName() : juce::String("Redo"),
                 history.canRedo());

    if (processorRef.hasRecoverableProject())
        menu.addItem(recoverProject, "Recover Autosave...");

    menu.addSeparator();

    // A project first, because it is the thing you come back to: everything on
    // every page. A synth preset below it is the Synth page's sound on its own,
    // which is what you reach for while building one - and every item says
    // "synth", because that is exactly and only what it saves or loads.
    menu.addSectionHeader("Project");
    menu.addItem(saveProject, "Save Project...");
    menu.addItem(loadProject, "Load Project...");

    menu.addSectionHeader("Synth Presets");
    menu.addItem(browsePresets, "Browse Synth Presets...");
    menu.addItem(savePreset, "Save Synth Preset...");
    menu.addItem(loadPreset, "Load Synth Preset...");
    menu.addItem(initPatch, "Init Synth Patch");

    // ---- Playing in key ---------------------------------------------------
    // The point of this section is that nothing below it can produce a wrong
    // note, so it is placed above the more technical entries.
    juce::PopupMenu keys;
    for (int key = 0; key < music::numKeys; ++key)
        keys.addItem(firstKey + key, music::keyName(key), true, key == currentKey);

    juce::PopupMenu scales;
    for (int scale = 0; scale < (int) music::Scale::numScales; ++scale)
    {
        juce::PopupMenu::Item item;
        item.itemID = firstScale + scale;
        item.text = music::scaleName(scale);
        item.shortcutKeyDescription = music::scaleDescription(scale);
        item.isTicked = (scale == currentScale);
        scales.addItem(item);
    }

    juce::PopupMenu chords;
    for (int mode = 0; mode < (int) music::ChordMode::numModes; ++mode)
        chords.addItem(firstChord + mode, music::chordModeName(mode), true, mode == currentChord);

    bool locked = currentScale != (int) music::Scale::chromatic;

    menu.addSectionHeader("Play in Key");
    menu.addSubMenu("Key: " + juce::String(music::keyName(currentKey)), keys);
    menu.addSubMenu("Scale: " + juce::String(music::scaleName(currentScale)), scales);
    // Chords are stacked out of scale steps, so there has to be a scale to
    // stack them in - said plainly rather than leaving a dead entry.
    bool chordAvailable = locked || currentChord != (int) music::ChordMode::off;
    menu.addSubMenu(chordAvailable ? "Chord: " + juce::String(music::chordModeName(currentChord))
                                   : juce::String("Chord: choose a scale first"),
                    chords, chordAvailable);

    // ---- Sound sources ----------------------------------------------------
    menu.addSectionHeader("Wavetable");
    menu.addItem(sampleWavetable, "Sample Wavetable from Audio...");
    menu.addItem(builtInWaves, "Use Built-in Waves");

    menu.addSectionHeader("Match a Sound");
    menu.addItem(quickMatch, "Quick Match...", !matching);
    menu.addItem(deepMatch, matching ? "Stop Deep Match" : "Deep Match...");
    menu.addItem(exportAb, "Export A/B...", !matching);

    // ---- Settings ---------------------------------------------------------
    // The folder each action opens in, listed with its current path so they can
    // be seen and changed here rather than only discovered by opening a dialog.
    juce::PopupMenu folders;
    for (auto which : AppSettings::allFolders())
    {
        juce::PopupMenu::Item item;
        item.itemID = firstFolder + (int) which;
        item.text = AppSettings::getFolderLabel(which);
        item.shortcutKeyDescription = folderFor(which).getFullPathName();
        folders.addItem(item);
    }
    folders.addSeparator();
    folders.addItem(resetFolders, "Reset All to Defaults");

    // The same question the button and setFullScreen ask, so the menu cannot
    // offer something they will refuse.
    auto* window = ownWindow();

    menu.addSectionHeader("Settings");
    menu.addItem(audioSettings, "Audio / MIDI Settings...");
    menu.addSubMenu("Folders", folders);
    menu.addItem(midiClock, "Follow MIDI Clock", true, params.midiClockSync.load() > 0.5f);

    // Which channel plays notes. Sitting next to MIDI Learn because the two
    // solve the same problem from opposite ends: one says which control drives
    // what, this says which keys are yours - so a controller whose pads or keys
    // you do not want can be silenced without unplugging its knobs.
    const int keyChannel = juce::jlimit(0, 16, (int) std::round(params.keyboardChannel.load()));

    juce::PopupMenu channels;
    channels.addItem(firstKeyboardChannel, "Omni - every channel", true, keyChannel == 0);
    channels.addSeparator();
    for (int ch = 1; ch <= 16; ++ch)
        channels.addItem(firstKeyboardChannel + ch, "Channel " + juce::String(ch), true,
                         keyChannel == ch);

    menu.addSubMenu(keyChannel == 0 ? juce::String("Keyboard Channel: Omni")
                                    : "Keyboard Channel: " + juce::String(keyChannel),
                    channels);

    // And which channel plays the KIT. Separate from the one above so a pad
    // controller and a keyboard can be plugged in at once - notes on this
    // channel strike drums and never reach the synth.
    const int drumChannel = juce::jlimit(0, 16, (int) std::round(params.drumChannel.load()));

    juce::PopupMenu drumChannels;
    drumChannels.addItem(firstDrumChannel, "Off - the kit is not played by MIDI", true,
                         drumChannel == 0);
    drumChannels.addSeparator();
    for (int ch = 1; ch <= 16; ++ch)
        drumChannels.addItem(firstDrumChannel + ch,
                             "Channel " + juce::String(ch) + (ch == 10 ? "  (General MIDI)" : ""),
                             true, drumChannel == ch);

    menu.addSubMenu(drumChannel == 0 ? juce::String("Drum Channel: Off")
                                     : "Drum Channel: " + juce::String(drumChannel),
                    drumChannels);

    // Where the kit listens. One control for the whole General MIDI map,
    // because a controller or a drum lane sitting an octave off GM is the
    // common case and twelve separate assignments would be a silly way to say
    // "up one". Anything a shift cannot express is a per-slot learn instead.
    //
    // Octaves rather than the semitones it is stored in: GM's drums are not
    // laid out chromatically, so a shift of seven would scatter the kit rather
    // than move it, and the menu should not offer a thing nobody wants.
    const int drumTranspose = juce::jlimit(-project::maxDrumTranspose,
                                           project::maxDrumTranspose,
                                           (int) std::round(params.drumTranspose.load()));

    juce::PopupMenu transposes;

    for (int octaves = -project::maxDrumTransposeOctaves;
         octaves <= project::maxDrumTransposeOctaves; ++octaves)
    {
        const int semitones = octaves * 12;

        // Named by where the KICK lands, which is the note somebody is
        // actually trying to move. "+1 octave" alone would still need working
        // out against whatever convention their DAW uses.
        const auto kick = juce::jlimit(0, 127, project::drumPrimaryNoteFor(0) + semitones);

        transposes.addItem(firstDrumTranspose + octaves + project::maxDrumTransposeOctaves,
                           (octaves == 0 ? juce::String("General MIDI")
                                         : (octaves > 0 ? "+" : "") + juce::String(octaves)
                                               + (std::abs(octaves) == 1 ? " octave" : " octaves"))
                               + "  -  kick on "
                               + juce::MidiMessage::getMidiNoteName(kick, true, true, 3),
                           true, semitones == drumTranspose);
    }

    menu.addSubMenu("Drum Notes: kick on "
                        + juce::MidiMessage::getMidiNoteName(
                              juce::jlimit(0, 127, project::drumPrimaryNoteFor(0) + drumTranspose),
                              true, true, 3),
                    transposes, drumChannel != 0);

    // Learn is a checkable mode rather than a one-shot action, because it stays
    // on while you assign several dials - and because the way out of it has to
    // be the same control that got you in.
    menu.addItem(midiLearn, "MIDI Learn", true, midiLearnOn);
    menu.addItem(midiAssignments,
                 "MIDI Assignments... (" + juce::String(processorRef.getMidiMap().countAssignments())
                     + ")");

    // Checkable, like Learn, because it is a mode you leave rather than an
    // action you run - and because switching it off is what prints the count.
    menu.addItem(midiMonitor, "MIDI Monitor", true, processorRef.getMidiMonitor().isEnabled());

    menu.addItem(fullScreen, "Full Screen", window != nullptr, isFullScreen());

    menu.addSeparator();
    menu.addItem(-1, "WaveLathe " + version::string(), false, false);

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(optionsButton)
                           .withMinimumWidth(260),
                       [this](int result) { handleOptionsMenuResult(result); });
}

void WaveLatheEditor::handleOptionsMenuResult(int result)
{
    if (result <= 0)
        return;

    auto& params = processorRef.getParameters();

    // The ranged sections first, so the switch below only handles one-offs.
    // Highest range first, because these are open-ended `>=` tests and the
    // keyboard's would otherwise swallow every drum id above it.
    if (result >= firstDrumTranspose)
    {
        const int octaves = result - firstDrumTranspose - project::maxDrumTransposeOctaves;
        const int semitones = juce::jlimit(-project::maxDrumTranspose, project::maxDrumTranspose,
                                           octaves * 12);

        params.drumTranspose = (float) semitones;
        AppSettings::getInstance().setDrumTranspose(semitones);

        // The kit's own page draws the twelve notes, and it is not necessarily
        // the page you are on - this is in Options, which is reachable from
        // anywhere. Repainted whether or not it is showing, because the cost is
        // nothing and the alternative is a page that is right only if you were
        // already looking at it.
        if (drumPanel != nullptr)
            drumPanel->repaint();

        const int kick = juce::jlimit(0, 127, project::drumPrimaryNoteFor(0) + semitones);
        const int snare = juce::jlimit(0, 127, project::drumPrimaryNoteFor(1) + semitones);

        lcd.print("Kit notes shifted "
                  + (semitones == 0 ? juce::String("back to General MIDI")
                                    : juce::String(octaves > 0 ? "up " : "down ")
                                          + juce::String(std::abs(octaves))
                                          + (std::abs(octaves) == 1 ? " octave" : " octaves"))
                  + " - kick " + juce::MidiMessage::getMidiNoteName(kick, true, true, 3)
                  + " (" + juce::String(kick) + "), snare "
                  + juce::MidiMessage::getMidiNoteName(snare, true, true, 3)
                  + " (" + juce::String(snare) + ").");
        return;
    }

    if (result >= firstDrumChannel)
    {
        int channel = result - firstDrumChannel;
        params.drumChannel = (float) channel;
        AppSettings::getInstance().setDrumChannel(channel);

        if (channel == 0)
        {
            lcd.print("The kit is played by the pattern only - no MIDI channel strikes it.");
            return;
        }

        // The notes named rather than assumed, because they move now: the kit
        // can be shifted whole octaves and individual slots can be taught their
        // own pad, so "C1 kick" was a sentence that could be false.
        const auto& noteMap = processorRef.getDrumNoteMap();
        const int shift = juce::jlimit(-project::maxDrumTranspose, project::maxDrumTranspose,
                                       (int) std::round(params.drumTranspose.load()));
        const auto nameOf = [&noteMap, shift](int voice)
        {
            return juce::MidiMessage::getMidiNoteName(drumTriggerNote(noteMap, voice, shift),
                                                      true, true, 3);
        };

        lcd.print("Channel " + juce::String(channel) + " strikes the drum kit: " + nameOf(0)
                  + " kick, " + nameOf(1) + " snare, " + nameOf(8) + " closed hat.");

        const int keyboard = juce::jlimit(0, 16, (int) std::round(params.keyboardChannel.load()));

        // The one combination that surprises people. Omni means the keyboard
        // hears everything EXCEPT what the kit has already taken, which is
        // usually what was wanted and is worth saying rather than leaving to
        // be discovered.
        if (keyboard == 0)
            lcd.print("The keyboard is omni, so it now hears every channel but this one.");
        else if (keyboard == channel)
            lcd.print("The keyboard is on channel " + juce::String(channel)
                      + " too, so it will hear nothing until you move it.");

        return;
    }

    if (result >= firstKeyboardChannel)
    {
        int channel = result - firstKeyboardChannel;
        params.keyboardChannel = (float) channel;
        AppSettings::getInstance().setKeyboardChannel(channel);

        lcd.print(channel == 0
                      ? "Keyboard listens on every MIDI channel."
                      : "Keyboard listens on channel " + juce::String(channel) + " only.");
        lcd.print("Learned controls still work on any channel.");
        return;
    }

    if (result >= 400)
    {
        int mode = result - 400;
        params.chordMode = (float) mode;
        lcd.print(juce::String("Chord: ") + music::chordModeName(mode)
                  + (mode == 0 ? " - one key plays one note." : " - one key plays a chord in the key."));
        return;
    }

    if (result >= 300)
    {
        int scale = result - 300;
        params.musicalScale = (float) scale;
        lcd.print(juce::String("Scale: ") + music::scaleName(scale));
        lcd.print(music::scaleDescription(scale));
        return;
    }

    if (result >= 200)
    {
        int key = result - 200;
        params.musicalKey = (float) key;
        lcd.print(juce::String("Key: ") + music::keyName(key) + " "
                  + music::scaleName((int) std::round(params.musicalScale.load())));
        return;
    }

    if (result >= 100)
    {
        changeFolderClicked((AppSettings::Folder) (result - 100));
        return;
    }

    switch (result)
    {
        case undoAction: performUndo(false); break;
        case redoAction: performUndo(true); break;
        case recoverProject: offerRecoveredProject(); break;

        case saveProject: saveProjectClicked(); break;
        case loadProject: loadProjectClicked(); break;

        case browsePresets: browsePresetsClicked(); break;
        case savePreset: savePresetClicked(); break;
        case loadPreset: loadPresetClicked(); break;

        case initPatch:
            processorRef.recordUndoPoint("Init Synth Patch");
            applyPresetValues(FactoryPresets::init(), "Synth patch initialised - the rest of the project is untouched.");
            break;

        case sampleWavetable: sampleWavetableClicked(); break;

        case builtInWaves:
            processorRef.revertToBuiltInWavetables();
            waveformDisplay.markDirty();
            lcd.print("Wavetable: built-in waves");
            break;

        case quickMatch: matchSampleClicked(); break;
        case deepMatch: deepMatchClicked(); break;
        case exportAb: exportAbClicked(); break;

        case midiClock:
        {
            bool on = params.midiClockSync.load() <= 0.5f;
            params.midiClockSync = on ? 1.0f : 0.0f;
            // In a plugin the host's transport outranks both of these, so say
            // so rather than offer a switch that decides nothing. The setting
            // is still kept: it is what this patch will do back in the
            // standalone, where there is no host to defer to.
            if (processorRef.wrapperType != juce::AudioProcessor::wrapperType_Standalone)
                lcd.print("Tempo follows the host here. This setting applies in the standalone.");
            else
                lcd.print(on ? "Tempo follows incoming MIDI clock (falls back to local BPM)."
                             : "Tempo uses the local BPM setting.");
            break;
        }

        case resetFolders:
            for (auto which : AppSettings::allFolders())
                AppSettings::getInstance().resetFolder(which);
            lcd.print("All folders reset to their defaults.");
            break;

        case midiLearn: setMidiLearnEnabled(!midiLearnOn); break;

        case midiAssignments:
            MidiMapView::show(processorRef.getMidiMap(),
                              [safeThis = juce::Component::SafePointer<WaveLatheEditor>(this)]
                              {
                                  if (safeThis == nullptr)
                                      return;

                                  safeThis->saveMidiMap();
                                  safeThis->learnLayer.repaint();
                              });
            break;

        case midiMonitor: toggleMidiMonitor(); break;

        case audioSettings: showAudioSettings(); break;
        case fullScreen: toggleFullScreen(); break;
        default: break;
    }
}

void WaveLatheEditor::browsePresetsClicked()
{
    // Taken before the browser opens rather than when Load is pressed: by then
    // the patch has already been replaced by whatever was being auditioned.
    processorRef.recordUndoPoint("Load Synth Preset");

    // Everything the browser might change, kept so previewing can be undone.
    // The wavetable travels as a pointer because the processor owns every set
    // it has ever loaded for the life of the session.
    auto snapshot = std::make_shared<PresetValues>(
        FxpPreset::fromSynthParameters(processorRef.getParameters(), "Before browsing"));
    auto snapshotWavetable = processorRef.getActiveWavetablePointer();

    // Every path in here is a synth preset: auditioning, choosing and backing
    // out all change the Synth page's sound and nothing else. A file saved by
    // an older build may carry a whole project; only its sound is taken.
    PresetBrowser::show(
        [this](const FactoryPresets::Entry& entry)
        {
            applyPresetValues(entry.values,
                              juce::String(FactoryPresets::categoryName((int) entry.category))
                                  + ": " + entry.name,
                              FxpPreset::ApplyScope::synthPreset);
        },
        [this](const juce::File& file)
        {
            PresetValues values;
            juce::String error;
            if (!FxpPreset::load(file, values, error))
            {
                lcd.print("Load failed: " + error);
                return;
            }

            auto note = loadWavetableFromPreset(values);
            applyPresetValues(values, "Loaded " + file.getFileNameWithoutExtension() + note,
                              FxpPreset::ApplyScope::synthPreset);
        },
        [this, snapshot, snapshotWavetable]
        {
            // Cancelled: put back exactly what was there before browsing.
            processorRef.setActiveWavetablePointer(snapshotWavetable);
            applyPresetValues(*snapshot, "Browsing cancelled - your patch is back.",
                              FxpPreset::ApplyScope::synthPreset);
        },
        [this]
        {
            lcd.print("Synth preset loaded.");
        });
}

void WaveLatheEditor::applyPresetValues(const PresetValues& values, const juce::String& message,
                                        FxpPreset::ApplyScope scope)
{
    const bool takeSequencer = scope == FxpPreset::ApplyScope::project;

    FxpPreset::applyToSynthParameters(values, processorRef.getParameters(), scope);

    // The pattern and its automation come back only with a project. Choosing
    // a sound is not the same act as replacing the bar you are writing, and
    // the two used to happen together only because they arrived in the same
    // file.
    if (takeSequencer)
    {
        processorRef.getSequencer().restoreState(values.sequencer);
        if (sequencerPanel != nullptr)
            sequencerPanel->refreshFromProcessor();

        // The kit itself: which drum is on each slot and what samples are
        // loaded. Guarded by takeSequencer for the reason the pattern is - a
        // PATCH carries no kit, so applying its empty slots would clear the
        // one you built just for choosing a different lead sound.
        //
        // This was missing, in the same way and for the same reason undo's
        // was: two paths restore a whole project - the host's, through
        // applyProject, and this one - and only the host's did the slots. So
        // "Open project..." in the standalone came back with the pattern and
        // the mixer and none of the samples.
        processorRef.restoreDrumSamples(values);

        for (int voice = 0; voice < project::numDrumVoices; ++voice)
            processorRef.setDrumEngine(voice, values.drumEngines[(size_t) voice]);
    }

    adoptLoadedPatch();
    waveformDisplay.markDirty();

    lcd.print(message);

    // What the project had that this kit no longer has room for. Said out
    // loud, because dropping somebody's work in silence is not a thing a
    // loader should do - and a project written when the kit had sixteen rows
    // can be carrying hits on four that are gone.
    if (!values.droppedDrumRows.isEmpty())
    {
        // Named here rather than in the reader, which deliberately knows
        // nothing about what a drum is called.
        juce::StringArray names;

        for (const auto row : values.droppedDrumRows)
            names.add(drums::engineName(row));

        lcd.print("This project used " + names.joinIntoString(", ")
                  + ", which the twelve-slot kit has no row for.");
        lcd.print("Those drums are still here - right-click a slot to put one back on it.");
    }

    // A patch that arrives with the arpeggiator running or the key locked would
    // otherwise change how the keyboard behaves with no explanation.
    juce::StringArray notes;
    if (values.arpEnabled > 0.5f)
        notes.add(juce::String("arp ") + Arpeggiator::modeName((int) std::round(values.arpMode))
                  + " / " + Arpeggiator::patternName((int) std::round(values.arpPattern)));

    int scale = (int) std::round(values.musicalScale);
    if (scale != (int) music::Scale::chromatic)
        notes.add(juce::String(music::keyName((int) std::round(values.musicalKey))) + " "
                  + music::scaleName(scale));

    int chord = (int) std::round(values.chordMode);
    if (chord != (int) music::ChordMode::off)
        notes.add(juce::String("chord ") + music::chordModeName(chord));

    if (!notes.isEmpty())
        lcd.print(notes.joinIntoString("  |  "));
}

// ---- MIDI learn -------------------------------------------------------------
// Learning is a mode rather than a right-click on each dial, because the thing
// you want to know while assigning is what is ALREADY assigned - and that is a
// question about the whole panel, not about the dial under the pointer.

void WaveLatheEditor::setMidiLearnEnabled(bool shouldLearn)
{
    midiLearnOn = shouldLearn;

    learnLayer.setVisible(shouldLearn);
    if (shouldLearn)
    {
        learnLayer.setBounds(content.getLocalBounds());
        learnLayer.toFront(false);
    }

    // Leaving the mode with a dial still armed would bind the next controller
    // you touched to a dial you stopped thinking about minutes ago.
    if (!shouldLearn)
        processorRef.getMidiMap().cancelLearn();

    int assigned = processorRef.getMidiMap().countAssignments();

    lcd.print(shouldLearn ? "MIDI learn on - click a dial, then move a control."
                          : "MIDI learn off.");

    if (shouldLearn)
        lcd.print(assigned == 0 ? "Nothing assigned yet."
                                : juce::String(assigned) + " control"
                                      + (assigned == 1 ? "" : "s") + " assigned.");
}

int WaveLatheEditor::parameterAt(juce::Point<int> pointOnLayer) const
{
    for (const auto& pair : automatedKnobs)
    {
        auto* knob = pair.first;
        if (knob == nullptr || !knob->isShowing())
            continue;

        if (learnLayer.getLocalArea(knob, knob->getLocalBounds()).contains(pointOnLayer))
            return pair.second;
    }

    return -1;
}

void WaveLatheEditor::paintLearnLayer(juce::Graphics& g)
{
    const auto& map = processorRef.getMidiMap();
    const int armed = map.getLearnTarget();
    const auto pointer = learnLayer.getMouseXYRelative();
    const int hovered = parameterAt(pointer);

    // Dimmed so the badges read as a layer over the panel rather than as part
    // of it, and so it is obvious at a glance that the dials are not live.
    g.fillAll(ui::colours::background.withAlpha(0.55f));

    for (const auto& pair : automatedKnobs)
    {
        auto* knob = pair.first;
        const int id = pair.second;

        if (knob == nullptr || !knob->isShowing())
            continue;

        auto area = learnLayer.getLocalArea(knob, knob->getLocalBounds());
        const int cc = map.controllerFor(id);

        // Three states, and each one says what it is rather than only being a
        // colour: listening, bound to a numbered control, or free.
        juce::String label;
        juce::Colour tint;

        if (id == armed)
        {
            label = "MOVE IT";
            tint = ui::colours::accentWarm;
        }
        else if (cc >= 0)
        {
            label = "CC " + juce::String(cc);
            tint = ui::colours::accent;
        }
        else
        {
            label = juce::String::fromUTF8("\xe2\x80\x93"); // an en dash, not a minus
            tint = ui::colours::textDim;
        }

        auto badge = area.reduced(2);
        const bool under = id == hovered;

        g.setColour(tint.withAlpha(under ? 0.30f : 0.16f));
        g.fillRoundedRectangle(badge.toFloat(), 4.0f);

        g.setColour(tint.withAlpha(under ? 1.0f : 0.75f));
        g.drawRoundedRectangle(badge.toFloat().reduced(0.5f), 4.0f, under ? 1.6f : 1.0f);

        g.setFont(juce::Font(juce::FontOptions((float) juce::jlimit(9, 13, badge.getHeight() / 4))));
        g.drawText(label, badge, juce::Justification::centred, false);
    }

    // What the pointer is over, spelled out along the bottom where it cannot
    // cover the dial being pointed at. A badge has room for "CC 74" and not for
    // the name of the dial it is sitting on.
    juce::String footer;
    if (armed >= 0)
        footer = "Move a control to assign it to " + juce::String(paramreg::name(armed))
                 + "  -  click elsewhere to cancel";
    else if (hovered >= 0)
    {
        int cc = map.controllerFor(hovered);
        footer = juce::String(paramreg::name(hovered));
        footer += cc >= 0 ? "  -  CC " + juce::String(cc) + ", right-click to clear"
                          : "  -  click, then move a control";
    }
    else
        footer = "Click a dial, then move the control you want for it.  Esc or Done to finish.";

    auto strip = learnLayer.getLocalBounds().removeFromBottom(MidiLearnLayer::footerHeight);
    g.setColour(ui::colours::panel.withAlpha(0.95f));
    g.fillRect(strip);
    g.setColour(ui::colours::accent.withAlpha(0.5f));
    g.drawLine((float) strip.getX(), (float) strip.getY(), (float) strip.getRight(),
               (float) strip.getY(), 1.0f);

    // The Done button sits at the right end of this strip, so the text stops
    // short of it rather than running underneath it.
    g.setColour(ui::colours::textPrimary);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText(footer, strip.withTrimmedRight(100).reduced(14, 0), juce::Justification::centredLeft,
               true);
}

void WaveLatheEditor::learnLayerClicked(const juce::MouseEvent& event)
{
    auto& map = processorRef.getMidiMap();
    const int id = parameterAt(event.getPosition());

    // Off a dial: the way out of an armed state that does not require finding
    // the right dial again.
    if (id < 0)
    {
        if (map.isLearning())
        {
            map.cancelLearn();
            lcd.print("Assignment cancelled.");
            learnLayer.repaint();
        }

        return;
    }

    if (event.mods.isPopupMenu())
    {
        int cc = map.controllerFor(id);
        if (cc < 0)
        {
            lcd.print(juce::String(paramreg::name(id)) + " has nothing assigned to it.");
        }
        else
        {
            map.clearParameter(id);
            lcd.print("Cleared CC " + juce::String(cc) + " from " + paramreg::name(id) + ".");
        }

        learnLayer.repaint();
        return;
    }

    map.armLearn(id);
    lcd.print("Move a control to assign it to " + juce::String(paramreg::name(id)) + ".");
    learnLayer.repaint();
}


HostParameter* WaveLatheEditor::hostParameterFor(const ui::ParameterKnob* knob) const
{
    if (knob == nullptr)
        return nullptr;

    // A scan of every registered dial on each step of a dial move, which is
    // nothing against the repaint it happens beside - and cheaper to keep
    // correct than a second index built from the same list.
    for (const auto& pair : automatedKnobs)
        if (pair.first == knob)
            return processorRef.getHostParameter(pair.second);

    return nullptr;
}
void WaveLatheEditor::followMidiMap()
{
    auto& map = processorRef.getMidiMap();

    const int armedNow = map.getLearnTarget();
    const int wasArmed = armedWhenLastChecked;
    armedWhenLastChecked = armedNow;

    const uint32_t version = map.getVersion();
    if (version == lastMapVersion)
        return;

    lastMapVersion = version;
    rebuildMappedParameters();

    // Something armed a moment ago and no longer armed means a controller
    // arrived and took it - the only way an assignment is ever made. Naming the
    // controller here is the confirmation, since the dial jumping to the
    // hardware's position only tells you SOMETHING happened.
    if (wasArmed >= 0 && armedNow < 0)
    {
        int cc = map.controllerFor(wasArmed);
        if (cc >= 0)
            lcd.print("CC " + juce::String(cc) + " now controls " + paramreg::name(wasArmed) + ".");
    }

    if (midiLearnOn)
        learnLayer.repaint();

    saveMidiMap();
}

void WaveLatheEditor::toggleMidiMonitor()
{
    auto& monitor = processorRef.getMidiMonitor();
    const bool turningOn = !monitor.isEnabled();

    if (!turningOn)
    {
        // Read before switching off, because switching ON is what clears the
        // counters - so the summary survives to be read, and stays on the
        // screen while you write the number down.
        lcd.print("MIDI monitor off. " + MidiMonitor::describe(monitor.getCounts()));
        monitor.setEnabled(false);
        return;
    }

    monitor.setEnabled(true);

    lastMonitorSequence = 0;
    lastMonitorKind = MidiMonitor::Kind::none;
    lastMonitorNumber = -1;
    lastMonitorChannel = -1;

    lcd.print("MIDI monitor on: real MIDI in only, not the keys on screen.");
    lcd.print("Switch it off again for a count of what arrived.");

    // The one setting of ours that can make the host look silent. Said now
    // rather than when it bites, because by then the readout is full of notes
    // that are about to be dropped and nothing on it says so.
    const int channel = juce::jlimit(0, 16,
                                     (int) std::round(processorRef.getParameters().keyboardChannel.load()));
    if (channel != 0)
        lcd.print("Keyboard channel is " + juce::String(channel)
                  + ": notes listed on other channels are then dropped.");
}

void WaveLatheEditor::followMidiMonitor()
{
    auto& monitor = processorRef.getMidiMonitor();
    if (!monitor.isEnabled())
        return;

    const auto event = monitor.getLastEvent();
    if (event.sequence == lastMonitorSequence)
        return;

    lastMonitorSequence = event.sequence;

    // Turning one knob rewrites one line; reaching for a second one starts a
    // second line. Stage 03 asks for a defined controller and an undefined one
    // precisely because hosts have been known to treat them differently, and
    // that difference has to be visible without scrolling anything.
    const bool sameControl = event.kind == lastMonitorKind
                             && event.number == lastMonitorNumber
                             && event.channel == lastMonitorChannel;

    lastMonitorKind = event.kind;
    lastMonitorNumber = event.number;
    lastMonitorChannel = event.channel;

    if (sameControl)
        lcd.printTransient(MidiMonitor::describe(event));
    else
        lcd.print(MidiMonitor::describe(event));
}

void WaveLatheEditor::saveMidiMap()
{
    AppSettings::getInstance().setMidiMap(processorRef.getMidiMap().toString());
}

void WaveLatheEditor::adoptMidiMap(const juce::String& text)
{
    processorRef.getMidiMap().fromString(text);
    AppSettings::getInstance().setMidiMap(text);

    // Both of these are derived from the map and would otherwise be a frame or
    // more out of date - the cache is what tells the dials to follow a
    // controller at all, so a stale one means the map loads and nothing moves.
    rebuildMappedParameters();
    lastMapVersion = processorRef.getMidiMap().getVersion();

    if (midiLearnOn)
        learnLayer.repaint();
}

void WaveLatheEditor::adoptKeyboardChannel(int channel)
{
    channel = juce::jlimit(0, 16, channel);

    processorRef.getParameters().keyboardChannel = (float) channel;
    AppSettings::getInstance().setKeyboardChannel(channel);
}

juce::String WaveLatheEditor::describeKeyboardChannel(int channel)
{
    return channel <= 0 ? juce::String("every channel") : "channel " + juce::String(channel);
}

void WaveLatheEditor::offerMidiSetupFromProject(const PresetValues& values)
{
    auto& map = processorRef.getMidiMap();

    const int liveChannel =
        juce::jlimit(0, 16, (int) std::round(processorRef.getParameters().keyboardChannel.load()));

    // Asked as one question, because they are one thing: which MIDI reaches
    // this synth. Two dialogs in a row for two halves of the same answer would
    // be worse than either.
    //
    // Absent is not the same as different. A project saved before the map
    // travelled carries no map, and one saved before channels did carries -1 -
    // neither is an instruction to clear what is on the desk.
    const bool hasMap = values.midiMap.isNotEmpty();
    const bool hasChannel = values.keyboardChannel >= 0;

    const bool mapDiffers = hasMap && values.midiMap != map.toString();
    const bool channelDiffers = hasChannel && values.keyboardChannel != liveChannel;

    // Nothing to decide. A question whose two answers do the same thing is a
    // click for nothing, so matching setups pass in silence.
    if (!mapDiffers && !channelDiffers)
        return;

    const int saved = hasMap ? juce::StringArray::fromTokens(values.midiMap, ",", "").size() : 0;

    juce::String message = "This project was saved with its own MIDI setup:\n";

    if (mapDiffers)
        message << "\n\x95  " << juce::String(saved) << " assignment" << (saved == 1 ? "" : "s")
                << ", against the " << juce::String(map.countAssignments()) << " you have now";

    if (channelDiffers)
        message << "\n\x95  Keyboard on " << describeKeyboardChannel(values.keyboardChannel)
                << ", against " << describeKeyboardChannel(liveChannel) << " now";

    // What it costs is the part worth spelling out: what is being replaced is
    // what is under your hands at this moment, on this desk, which may not be
    // the desk the project was saved at.
    message << "\n\nLoading it replaces what you are set up with here.";

    auto options = juce::MessageBoxOptions()
                       .withIconType(juce::MessageBoxIconType::QuestionIcon)
                       .withTitle("This project has its own MIDI setup")
                       .withMessage(message)
                       .withButton("Load its MIDI setup")
                       .withButton("Keep mine")
                       .withAssociatedComponent(this);

    // Load is listed first because JUCE gives the FIRST of two buttons result 1
    // and the Return key, and the SECOND result 0, Escape and the close box.
    // Keep has to be second for dismissing the dialog to mean keep - the same
    // trap, and the same answer, as the pattern question above.
    juce::AlertWindow::showAsync(
        options,
        [safeThis = juce::Component::SafePointer<WaveLatheEditor>(this), text = values.midiMap,
         channel = values.keyboardChannel, mapDiffers, channelDiffers, saved](int result)
        {
            if (safeThis == nullptr)
                return;

            if (result != 1)
            {
                safeThis->lcd.print("Kept your MIDI setup.");
                return;
            }

            if (mapDiffers)
            {
                safeThis->adoptMidiMap(text);
                safeThis->lcd.print("Loaded " + juce::String(saved) + " MIDI assignment"
                                    + (saved == 1 ? "" : "s") + " from the project.");
            }

            if (channelDiffers)
            {
                safeThis->adoptKeyboardChannel(channel);
                safeThis->lcd.print("Keyboard listens on " + describeKeyboardChannel(channel) + ".");
            }
        });
}

void WaveLatheEditor::changeFolderClicked(AppSettings::Folder which)
{
    auto label = AppSettings::getFolderLabel(which);
    fileChooser = std::make_unique<juce::FileChooser>("Folder for " + label, folderFor(which));

    fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectDirectories,
                             [this, which, label](const juce::FileChooser& chooser)
                             {
                                 auto folder = chooser.getResult();
                                 if (folder == juce::File())
                                     return;

                                 AppSettings::getInstance().setFolder(which, folder);
                                 lcd.print(label + " folder: " + folder.getFullPathName());
                             });
}

void WaveLatheEditor::updateArpPatternButton()
{
    int pattern = juce::jlimit(0, Arpeggiator::numPatterns() - 1,
                                (int) std::round(processorRef.getParameters().arpPattern.load()));
    arpPatternButton.setButtonText(Arpeggiator::patternName(pattern));
}

void WaveLatheEditor::showArpPatternMenu()
{
    // Grouped by the feel each rhythm gives, since that is how you go looking
    // for one - not by how many steps it happens to have.
    struct PatternGroup
    {
        const char* title;
        const char* firstPattern;
    };

    static const PatternGroup groups[] = {
        {"Even", "Straight"},
        {"Syncopated", "Dotted 8th"},
        {"Driving", "Gallop"},
        {"Repeats", "Double Up"},
        {"Octave", "Octave Jump"},
        {"Character", "Acid"}
    };

    int current = juce::jlimit(0, Arpeggiator::numPatterns() - 1,
                                (int) std::round(processorRef.getParameters().arpPattern.load()));

    juce::PopupMenu menu;
    menu.setLookAndFeel(&lookAndFeel);

    int nextGroup = 0;
    for (int pattern = 0; pattern < Arpeggiator::numPatterns(); ++pattern)
    {
        juce::String name(Arpeggiator::patternName(pattern));

        if (nextGroup < (int) juce::numElementsInArray(groups)
            && name == groups[nextGroup].firstPattern)
            menu.addSectionHeader(groups[nextGroup++].title);

        // The step map rides in the shortcut column, right-aligned and dimmed,
        // so the rhythm is visible before you commit to hearing it - and stays
        // lined up, which hand-padded spaces would not in a proportional font.
        juce::PopupMenu::Item item;
        item.itemID = pattern + 1;
        item.text = name;
        item.shortcutKeyDescription = Arpeggiator::patternSteps(pattern);
        item.isTicked = (pattern == current);
        menu.addItem(item);
    }

    menu.showMenuAsync(juce::PopupMenu::Options()
                           .withTargetComponent(arpPatternButton)
                           .withMinimumWidth(280),
                       [this](int result)
                       {
                           if (result <= 0)
                               return;

                           int chosen = result - 1;
                           processorRef.getParameters().arpPattern = (float) chosen;
                           updateArpPatternButton();
                           lcd.print(juce::String("Arp rhythm: ") + Arpeggiator::patternName(chosen)
                                     + "  " + Arpeggiator::patternSteps(chosen));
                           lcd.print(Arpeggiator::patternDescription(chosen));
                       });
}

juce::File WaveLatheEditor::folderFor(AppSettings::Folder which) const
{
    return AppSettings::getInstance().getFolder(which);
}

void WaveLatheEditor::rememberFolder(AppSettings::Folder which, const juce::File& file)
{
    auto folder = file.isDirectory() ? file : file.getParentDirectory();
    AppSettings::getInstance().setFolder(which, folder);
}

void WaveLatheEditor::setHoldEnabled(bool shouldHold)
{
    // Hold is now a latch across everything played rather than a grip on one
    // note, so the work happens in the MIDI chain and this just arms it.
    processorRef.getParameters().holdNotes = shouldHold ? 1.0f : 0.0f;

    lcd.print(shouldHold ? "Hold on - every note you play stays down. Tap a held note to drop it."
                         : "Hold off - everything released.");
}

// The usual two rows: the home row is the white keys, the row above holds the
// black keys where they fall on a piano.
const WaveLatheEditor::ComputerKey WaveLatheEditor::computerKeys[] = {
    {'a', 0},  {'w', 1},  {'s', 2},  {'e', 3},  {'d', 4},  {'f', 5},  {'t', 6},
    {'g', 7},  {'y', 8},  {'h', 9},  {'u', 10}, {'j', 11},
    {'k', 12}, {'o', 13}, {'l', 14}, {'p', 15}, {';', 16}, {'\'', 17}
};

const int WaveLatheEditor::numComputerKeys =
    (int) (sizeof(WaveLatheEditor::computerKeys) / sizeof(WaveLatheEditor::computerKeys[0]));

namespace
{
// The top letter row and the two brackets after it: twelve adjacent keys,
// left to right, under twelve strips that also read left to right. Q is the
// Kick because Q is the leftmost, not because of anything about Q.
//
// A drum kit is not a keyboard, so this does not reuse the piano layout the
// note keys have - the keys are free while the Drums page is up, but mapping a
// kick to a black note would borrow a shape that means nothing here.
//
// The brackets are the price of the row being exactly twelve long. They sit
// where they sit on every layout this is likely to meet, and the alternative -
// stopping at P and leaving Crash and Aux unplayable - is worse than an
// awkward key for the last two.
constexpr int drumKeys[] = {
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']'
};

constexpr int numDrumKeys = (int) (sizeof(drumKeys) / sizeof(drumKeys[0]));

static_assert(numDrumKeys == project::numDrumVoices,
              "There is one computer key per drum slot. A thirteenth slot with no key would be "
              "unplayable from the keyboard with nothing saying why.");
} // namespace

bool WaveLatheEditor::keyboardIsElsewhere() const
{
    // A menu, an alert, a file dialog or the preset browser is up. Every one of
    // those is a separate WINDOW of this same process, so the foreground-process
    // check waves them all through - which is how typing a file name into the
    // save dialog played a tune, and how the letters that played it were also
    // picking items out of the Options menu on the way past.
    //
    // They are what JUCE calls modal, whether they are its own windows or the
    // system's, so one question covers all of them.
    if (juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0)
        return true;

    auto* focused = juce::Component::getCurrentlyFocusedComponent();

    // Nothing focused is the ORDINARY state here, not an exceptional one: the
    // note keys are polled rather than delivered precisely so that clicking a
    // dial does not stop them working. Blocking on this would mean blocking
    // almost always.
    if (focused == nullptr)
        return false;

    // A search box or a name field should get its letters, not play notes.
    if (dynamic_cast<juce::TextEditor*>(focused) != nullptr)
        return true;

    // And anything focused that is not part of this editor is part of some
    // other window of ours, whose keys are its own business.
    return focused != this && !isParentOf(focused);
}

bool WaveLatheEditor::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    if (keyboardIsElsewhere())
        return false;

    // Escape leaves MIDI learn, before anything else looks at the key. The
    // layer covers the whole panel - that is the point of it - which includes
    // the Options button that switched learn on, so without this and the Done
    // button beside it there is no way back out.
    if (midiLearnOn && key.isKeyCode(juce::KeyPress::escapeKey))
    {
        setMidiLearnEnabled(false);
        return true;
    }

    // Then full screen, on the two keys everything else uses for it. Escape
    // only does anything while it is on, so it is free for whatever else wants
    // it the rest of the time.
    if (key.isKeyCode(juce::KeyPress::F11Key))
    {
        toggleFullScreen();
        return true;
    }

    if (isFullScreen() && key.isKeyCode(juce::KeyPress::escapeKey))
    {
        setFullScreen(false);
        return true;
    }


    // Undo is deliberately NOT handled here. checkUndoShortcuts, driven by the
    // timer, owns it - that is the path that works wherever focus happens to
    // be. Handling it here as well does not make it more reliable, it makes it
    // happen twice: whenever key presses do reach this editor, one Ctrl+Z
    // stepped back two entries.

    // Consume whichever keys are currently playing something, so they do not
    // also ripple through as shortcuts - but let everything else past. The
    // actual notes and hits are started by the key STATE change, which is the
    // only part that also tells us about release.
    //
    // Which set that is depends on the page: on the Drums page the twelve
    // drum keys are live and the piano row is not, and consuming a key that is
    // no longer playing anything would swallow a shortcut for nothing.
    const auto matches = [&key](int keyCode)
    {
        return key.getKeyCode() == keyCode
               || key.getTextCharacter() == (juce::juce_wchar) keyCode;
    };

    if (computerKeysPlayDrums())
    {
        for (int i = 0; i < numDrumKeys; ++i)
            if (matches(drumKeys[i]))
                return true;

        return false;
    }

    for (int i = 0; i < numComputerKeys; ++i)
        if (matches(computerKeys[i].keyCode))
            return true;

    return false;
}

bool WaveLatheEditor::keyStateChanged(bool, juce::Component*)
{
    updateComputerKeyboardNotes();
    return false; // never swallow the event: other components may want it too
}

void WaveLatheEditor::updateComputerKeyboardNotes(bool restartHeldKeys)
{
    if ((int) computerKeyNotes.size() != numComputerKeys)
        computerKeyNotes.assign((size_t) numComputerKeys, -1);

    if ((int) drumKeyHeld.size() != numDrumKeys)
        drumKeyHeld.assign((size_t) numDrumKeys, false);

    auto& keyboardState = processorRef.getKeyboardState();

    // Key state is read globally, so nothing stops these keys on the way in -
    // every rule about when they should NOT play a note has to be written here.
    // The foreground check keeps typing in another APPLICATION out;
    // keyboardIsElsewhere keeps typing in another window of this one out, which
    // is every menu, dialog and file chooser.
    // A modifier held means a command was meant, not a note - otherwise Ctrl+Y
    // would sound the note on the Y key on its way to redoing something.
    bool blocked = keyboardIsElsewhere() || !isShowing() || !juce::Process::isForegroundProcess()
                   || juce::ModifierKeys::getCurrentModifiers().isCommandDown()
                   || juce::ModifierKeys::getCurrentModifiers().isAltDown();

    // ---- The Drums page: the same keys strike the kit ----------------------
    //
    // The synth's note keys are let go of FIRST and unconditionally, so a key
    // held while the page changes cannot leave a note sounding that nothing
    // will ever release - the release would arrive while this branch owns the
    // keyboard and be dropped.
    if (computerKeysPlayDrums())
    {
        releaseComputerKeyboardNotes();

        for (int i = 0; i < numDrumKeys; ++i)
        {
            const bool down = !blocked && juce::KeyPress::isKeyCurrentlyDown(drumKeys[i]);
            auto held = drumKeyHeld[(size_t) i];

            // The DOWN edge only. A drum is struck rather than held, so leaning
            // on a key is one hit and not a machine-gun at the polling rate.
            if (down && !held)
            {
                // Shift is the accent. The kit keeps a quarter of its range
                // back for accented hits and a computer key has no velocity to
                // reach it with, so without this the loudest thing playable
                // from the keyboard would be three quarters of the loudest
                // thing the kit can do.
                processorRef.auditionDrumVoice(
                    i, juce::ModifierKeys::getCurrentModifiers().isShiftDown());

                waveformDisplay.showDrumSlot(i);

                lcd.printTransient(juce::String(project::drumVoiceName(i)).toUpperCase() + ": "
                                   + processorRef.getDrumSlotDescription(i));
            }

            drumKeyHeld[(size_t) i] = down;
        }

        return;
    }

    // Off the Drums page, nothing is held on the kit: the keys go back to
    // being notes and the next visit starts from silence rather than from
    // whatever was down when the page changed.
    std::fill(drumKeyHeld.begin(), drumKeyHeld.end(), false);

    for (int i = 0; i < numComputerKeys; ++i)
    {
        bool wantDown = !blocked && juce::KeyPress::isKeyCurrentlyDown(computerKeys[i].keyCode);
        int& sounding = computerKeyNotes[(size_t) i];

        // An octave shift restarts whatever is held, so the key you are leaning
        // on jumps with the buttons instead of staying where it started.
        if (restartHeldKeys && sounding >= 0)
        {
            keyboardState.noteOff(1, sounding, 0.0f);
            sounding = -1;
        }

        if (wantDown && sounding < 0)
        {
            int note = juce::jlimit(0, 127, computerKeyBaseNote + computerKeys[i].semitoneOffset);
            keyboardState.noteOn(1, note, 0.85f);
            sounding = note;
            lastClickedNote.store(note);
        }
        else if (!wantDown && sounding >= 0)
        {
            keyboardState.noteOff(1, sounding, 0.0f);
            sounding = -1;
        }
    }
}

void WaveLatheEditor::releaseComputerKeyboardNotes()
{
    auto& keyboardState = processorRef.getKeyboardState();

    for (auto& sounding : computerKeyNotes)
        if (sounding >= 0)
        {
            keyboardState.noteOff(1, sounding, 0.0f);
            sounding = -1;
        }
}

void WaveLatheEditor::timerCallback()
{
    // A key can go down while a menu or dialog is up, and its release then
    // never reaches us. Reconciling every tick means a note can be left hanging
    // for at most a twentieth of a second rather than indefinitely.
    updateComputerKeyboardNotes();

    // The sequencer's rate is set on another page, so the arpeggiator's dial
    // picks up a change to it here rather than being told about it.
    updateArpRateScale();

    // The sequencer page has its own transport buttons, so these follow what
    // the sequencer is doing rather than only what was pressed down here.
    refreshTransportButtons();

    checkUndoShortcuts();

    // Binds happen on the audio thread, where the controller arrives, so the
    // interface notices one by watching the map rather than being told.
    followMidiMap();

    // A preset chosen from the host's own menu, for the same reason.
    followProgramChange();

    // And the tempo, when the host or a MIDI clock is the one setting it.
    followTempo();

    // What is arriving, when the monitor is on. Printed from here for the same
    // reason everything else in this tick is: it is caught on the audio thread,
    // and that is not a thread that may touch a component.
    followMidiMonitor();

    // The window is not built yet when this editor is, so the wrapper's own
    // button is taken out of the way on the first tick that finds it.
    if (!wrapperButtonHidden)
    {
        hideWrapperOptionsButton();

        // Sized to the screen it opened on, now that there is a window and it
        // has been put on one. Once, at startup: a window that resized itself
        // every time it was dragged across a monitor edge would be a window you
        // could not drag across a monitor edge.
        fitToOpeningDisplay();

        // The standalone wrapper asks for a minimise and a close and stops
        // there, which leaves a resizable window with no way to fill the screen
        // from its own corner. Asked for once, here, because the call rebuilds
        // the buttons.
        if (auto* window = ownWindow())
            window->setTitleBarButtonsRequired(juce::DocumentWindow::minimiseButton
                                                   | juce::DocumentWindow::maximiseButton
                                                   | juce::DocumentWindow::closeButton,
                                               false);

        wrapperButtonHidden = getTopLevelComponent() != nullptr && getTopLevelComponent() != this;
    }

    // Kept beside those buttons, and taken back into the header whenever there
    // is no title bar to be beside. Reconciled on the tick rather than from
    // resized(): DocumentWindow lays its own buttons out AFTER it has resized
    // the content component, so anything measuring them from in there measures
    // where they used to be.
    placeFullScreenButton();

    // Naming what was just played is how the key lock teaches rather than only
    // corrects: you see that the black key you hit became Eb, and that it is
    // the third degree of the key. Done here rather than in the note callback,
    // which can arrive on the audio thread.
    {
        auto& p = processorRef.getParameters();
        int played = lastClickedNote.load();
        int scale = juce::jlimit(0, (int) music::Scale::numScales - 1,
                                  (int) std::round(p.musicalScale.load()));

        if (played != lastAnnouncedNote && scale != (int) music::Scale::chromatic)
        {
            lastAnnouncedNote = played;
            lcd.printTransient("Playing " + music::chordLabel(played,
                                   juce::jlimit(0, music::numKeys - 1, (int) std::round(p.musicalKey.load())),
                                   (music::Scale) scale,
                                   (music::ChordMode) juce::jlimit(0, (int) music::ChordMode::numModes - 1,
                                                                    (int) std::round(p.chordMode.load()))));
        }
        else if (scale == (int) music::Scale::chromatic)
        {
            lastAnnouncedNote = played;
        }
    }

    // Keep the automation lanes and the dials in step with each other.
    followAutomation();

    if (optimizerRunning.load())
    {
        int gen = optimizerGeneration.load();
        int total = optimizerTotalGenerations.load();
        if (gen > 0)
            lcd.printTransient("Matching... gen " + juce::String(gen) + "/" + juce::String(total) + "  dist "
                               + juce::String(optimizerBestDistance.load(), 1));
    }
}

void WaveLatheEditor::handleNoteOn(juce::MidiKeyboardState*, int, int midiNoteNumber, float)
{
    lastClickedNote.store(midiNoteNumber); // audio thread: atomics only
}

void WaveLatheEditor::handleNoteOff(juce::MidiKeyboardState*, int, int, float) {}

void WaveLatheEditor::paint(juce::Graphics& g)
{
    g.fillAll(ui::colours::background);
}

void WaveLatheEditor::paintContentOverChildren(juce::Graphics& g)
{
    // The keyboard and the arpeggiator play the SYNTH, and the Drums page has
    // nothing to do with either. Washed rather than hidden, and rather than
    // disabled: hiding them would make the window jump half its height every
    // time the page changed, and disabling them would take away the one way to
    // check a patch without leaving the page. Dimmed says "not this page"
    // while leaving both working.
    if (currentPage != drumsPage || playingStripBounds.isEmpty())
        return;

    g.setColour(ui::colours::background.withAlpha(0.72f));
    g.fillRect(playingStripBounds);
}

void WaveLatheEditor::paintContent(juce::Graphics& g)
{
    g.fillAll(ui::colours::background);

    // The wordmark sits on the right, opposite the controls. Its three parts are
    // measured and placed as one block so the group ends flush with the edge
    // however wide the version number grows.
    auto header = content.getLocalBounds().removeFromTop(44).reduced(16, 0);

    juce::Font titleFont(juce::FontOptions(22.0f, juce::Font::bold));
    juce::Font versionFont(juce::FontOptions(12.0f, juce::Font::bold));
    juce::Font subtitleFont(juce::FontOptions(11.0f));

    auto versionText = version::string();
    const juce::String subtitleText("wavetable synthesiser");

    // A few pixels over the measured width each, because text drawn into a box
    // its own exact size gets an ellipsis put through it.
    constexpr int slack = 6;
    int nameWidth = (int) juce::GlyphArrangement::getStringWidth(titleFont, "WAVELATHE") + slack;
    int versionWidth = (int) juce::GlyphArrangement::getStringWidth(versionFont, versionText) + slack;
    int subtitleWidth = (int) juce::GlyphArrangement::getStringWidth(subtitleFont, subtitleText) + slack;

    constexpr int nameGap = 10;
    constexpr int versionGap = 12;
    int blockWidth = nameWidth + nameGap + versionWidth + versionGap + subtitleWidth;

    auto block = header.removeFromRight(juce::jmin(header.getWidth(), blockWidth));

    g.setColour(ui::colours::textPrimary);
    g.setFont(titleFont);
    g.drawText("WAVELATHE", block.removeFromLeft(nameWidth), juce::Justification::centredLeft);

    block.removeFromLeft(nameGap);
    g.setColour(ui::colours::accent);
    g.setFont(versionFont);
    g.drawText(versionText, block.removeFromLeft(versionWidth), juce::Justification::centredLeft);

    block.removeFromLeft(versionGap);
    g.setColour(ui::colours::textDim);
    g.setFont(subtitleFont);
    g.drawText(subtitleText, block, juce::Justification::centredLeft);

    // The transport's own box, so the pair reads as one control rather than two
    // loose buttons. Built like the readout it stands against - same bezel,
    // same radius, same inset face - so the two read as one panel with a screen
    // on it rather than as a control that happened to be parked there.
    if (!transportBox.isEmpty())
    {
        auto box = transportBox.toFloat();
        g.setColour(juce::Colour{0xff05070a});
        g.fillRoundedRectangle(box, 5.0f);

        auto face = box.reduced(4.0f);
        g.setColour(ui::colours::panel);
        g.fillRoundedRectangle(face, 3.0f);
        g.setColour(ui::colours::accent.withAlpha(0.30f));
        g.drawRoundedRectangle(face, 3.0f, 1.0f);
    }

    // Group outlines first, so they sit behind everything else.
    for (const auto& group : groupBoxes)
    {
        auto r = group.bounds.toFloat();
        g.setColour(group.tint.withAlpha(0.05f));
        g.fillRoundedRectangle(r, 10.0f);
        g.setColour(group.tint.withAlpha(0.35f));
        g.drawRoundedRectangle(r.reduced(0.5f), 10.0f, 1.2f);

        // Title sits on the top edge, with the outline broken behind it.
        g.setFont(juce::Font(juce::FontOptions(10.5f, juce::Font::bold)));
        auto textWidth = juce::GlyphArrangement::getStringWidth(g.getCurrentFont(), group.title.toUpperCase())
                         + 12.0f;
        juce::Rectangle<float> label(r.getX() + 14.0f, r.getY() - 7.0f, textWidth, 14.0f);
        g.setColour(ui::colours::background);
        g.fillRect(label);
        g.setColour(group.tint.withAlpha(0.9f));
        g.drawText(group.title.toUpperCase(), label, juce::Justification::centred);
    }

}

void WaveLatheEditor::layoutSection(ui::FlowSection& section, juce::Rectangle<int> area,
                                     const std::vector<ui::ParameterKnob*>& knobs)
{
    section.setBounds(area);

    auto inner = area.reduced(8, 0).withTrimmedTop(18).withTrimmedBottom(6);
    if (knobs.empty())
        return;

    int knobWidth = inner.getWidth() / (int) knobs.size();
    for (auto* knob : knobs)
        knob->setBounds(inner.removeFromLeft(knobWidth).reduced(2, 0));
}

void WaveLatheEditor::resized()
{
    content.setBounds(getLocalBounds());

    // Covers the whole panel, so a dial near an edge is as clickable in learn
    // mode as one in the middle.
    learnLayer.setBounds(content.getLocalBounds());
    learnLayer.toFront(false);
    layoutContent();
}

void WaveLatheEditor::layoutContent()
{
    auto bounds = content.getLocalBounds();
    auto titleRow = bounds.removeFromTop(44);
    bounds.reduce(12, 0);

    // Controls on the left where reading starts, the wordmark on the right.
    // The tabs reach the bottom of the row so the selected one joins the page
    // underneath it rather than floating above it.
    optionsButton.setBounds(titleRow.removeFromLeft(104).reduced(12, 9));
    pageTabs.setBounds(titleRow.removeFromLeft(pageTabs.getPreferredWidth()).withTrimmedTop(10));

    // Worked out either way; only used when the title bar is not there to hold
    // it. Applying it unconditionally would fight the title bar for a button
    // that is no longer a child of this component.
    titleRow.removeFromLeft(14);
    headerFullScreenBounds = titleRow.removeFromLeft(30).withSizeKeepingCentre(30, 26);

    if (fullScreenButton.getParentComponent() == &content)
        fullScreenButton.setBounds(headerFullScreenBounds);

    groupBoxes.clear();
    constexpr int gap = 14;
    constexpr int groupPad = 15; // room inside a group outline, incl. its title
    constexpr int mesh = ui::FlowSection::chevronWidth; // overlap so chevrons interlock

    constexpr int lcdHeight = 58;
    constexpr int octaveHeight = 24;
    int keyboardHeight = juce::jlimit(70, 120, bounds.getHeight() / 8);
    int waveformHeight = juce::jlimit(78, 140, bounds.getHeight() / 8);

    // The strip is the same height it always was - the complaint about it was
    // its WIDTH, one picture stretched across the panel with nothing to fill it.
    // Split instead: the offline views keep the left, which is as much room as a
    // few cycles and an envelope actually need, and the live view takes the
    // rest, which is what a scope can use.
    auto waveformRow = bounds.removeFromTop(waveformHeight);
    waveformDisplay.setBounds(waveformRow.removeFromLeft((int) (waveformRow.getWidth() * 0.42f)));
    waveformRow.removeFromLeft(gap);
    outputScope.setBounds(waveformRow);
    bounds.removeFromTop(10);

    // Everything below the panel, counted exactly: the readout with the
    // transport beside it, the air under it, and the keys. Under-counting here
    // costs the keyboard the bottom of the window on a short screen.
    int fixedBelow = lcdHeight + 12 + keyboardHeight;
    auto panelArea = bounds.removeFromTop(juce::jmax(240, bounds.getHeight() - fixedBelow - gap));

    // The sequencer and Master pages occupy the same rectangle the synth
    // controls do, so each gets the full width rather than being squeezed in
    // beside them.
    if (sequencerPanel != nullptr)
        sequencerPanel->setBounds(panelArea);

    if (drumPanel != nullptr)
        drumPanel->setBounds(panelArea);

    // The Master page stacks its two halves in the order the signal takes them:
    // the bus effects on top, the mastering chain under them. The bus gets a
    // little under half - one row of seven stages is all it holds - and the
    // mastering panel keeps the rest, which it centres its own row in.
    if (busEffectsPanel != nullptr && masteringPanel != nullptr)
    {
        auto masterArea = panelArea;
        const int busHeight = juce::jlimit(170, 250, (int) (masterArea.getHeight() * 0.42f));

        busEffectsPanel->setBounds(masterArea.removeFromTop(busHeight));
        masterArea.removeFromTop(gap);
        masteringPanel->setBounds(masterArea);
    }
    else if (masteringPanel != nullptr)
    {
        masteringPanel->setBounds(panelArea);
    }

    if (currentPage != synthPage)
        return; // the synth sections are hidden, so there is nothing to place

    // Two columns: the voice chain on the left, effects and modulation on the
    // right. Halving the number of rows is what lets each dial be a usable size.
    int columnWidth = (panelArea.getWidth() - gap * 2) / 2;
    auto leftColumn = panelArea.removeFromLeft(columnWidth);
    panelArea.removeFromLeft(gap * 2);
    auto rightColumn = panelArea;

    // Chain sections mesh by overlapping their chevron point into the next
    // section's notch, so a row of them reads as one continuous flow.
    auto splitChain = [mesh](juce::Rectangle<int>& row, const std::vector<int>& weights)
    {
        std::vector<juce::Rectangle<int>> areas;
        int totalWeight = 0;
        for (int w : weights) totalWeight += w;
        int span = row.getWidth() + mesh * ((int) weights.size() - 1);

        int x = row.getX();
        for (size_t i = 0; i < weights.size(); ++i)
        {
            int width = i + 1 == weights.size() ? row.getRight() - x : span * weights[i] / totalWeight;
            areas.push_back({x, row.getY(), width, row.getHeight()});
            x += width - mesh;
        }
        return areas;
    };

    auto splitPlain = [gap](juce::Rectangle<int>& row, const std::vector<int>& weights)
    {
        std::vector<juce::Rectangle<int>> areas;
        int totalWeight = 0;
        for (int w : weights) totalWeight += w;
        int usable = row.getWidth() - gap * ((int) weights.size() - 1);

        for (size_t i = 0; i < weights.size(); ++i)
        {
            int width = i + 1 == weights.size() ? row.getWidth() : usable * weights[i] / totalWeight;
            areas.push_back(row.removeFromLeft(width));
            if (i + 1 < weights.size())
                row.removeFromLeft(gap);
        }
        return areas;
    };

    // ===================== Left column: the voice signal chain =============
    {
        auto column = leftColumn;
        int groupTop = column.getY();
        column.removeFromTop(groupPad);
        column.reduce(8, 0);

        int rowHeight = (column.getHeight() - groupPad - gap * 2) / 3;

        auto row1 = column.removeFromTop(rowHeight);
        auto a1 = splitChain(row1, {5, 4});
        layoutSection(oscSection, a1[0],
                      {osc1LevelKnob.get(), waveKnob.get(), voicesKnob.get(), detuneKnob.get(),
                       widthKnob.get(), glideKnob.get()});
        layoutSection(osc2Section, a1[1],
                      {osc2LevelKnob.get(), osc2WaveKnob.get(), osc2SemiKnob.get(), osc2FineKnob.get()});
        oscSection.setStyle(ui::SectionStyle::chainStart, 1);
        osc2Section.setStyle(ui::SectionStyle::chainMiddle, 2);

        // Mono and legato belong to how the pitch moves, so they sit on the
        // oscillator row next to the glide that they change the meaning of.
        monoButton.setBounds(a1[0].getRight() - 132, a1[0].getY() + 1, 60, 15);
        legatoButton.setBounds(a1[0].getRight() - 70, a1[0].getY() + 1, 66, 15);
        column.removeFromTop(gap);

        auto row2 = column.removeFromTop(rowHeight);
        auto a2 = splitChain(row2, {5, 4});
        layoutSection(subNoiseSection, a2[0],
                      {subLevelKnob.get(), subWaveKnob.get(), subOctaveKnob.get(), noiseLevelKnob.get(),
                       noiseColourKnob.get()});
        layoutSection(filterSection, a2[1],
                      {filterTypeKnob.get(), cutoffKnob.get(), resonanceKnob.get()});
        subNoiseSection.setStyle(ui::SectionStyle::chainMiddle, 3);
        filterSection.setStyle(ui::SectionStyle::chainMiddle, 4);
        column.removeFromTop(gap);

        auto row3 = column.removeFromTop(rowHeight);
        auto a3 = splitChain(row3, {2, 5});
        layoutSection(driveSection, a3[0], {driveKnob.get()});
        layoutSection(ampSection, a3[1],
                      {attackKnob.get(), decayKnob.get(), sustainKnob.get(), releaseKnob.get()});
        driveSection.setStyle(ui::SectionStyle::chainMiddle, 5);
        ampSection.setStyle(ui::SectionStyle::chainEnd, 6);

        groupBoxes.push_back({leftColumn.withTop(groupTop).withBottom(column.getY() + 4), "Voice signal chain",
                              ui::colours::accent});
    }

    // ============ Right column: master effects, then modulation ============
    {
        auto column = rightColumn;
        int fxHeight = column.getHeight() / 2; // three rows of effects now

        auto fxArea = column.removeFromTop(fxHeight);
        {
            int groupTop = fxArea.getY();
            auto inner = fxArea;
            inner.removeFromTop(groupPad);
            inner.reduce(8, 0);

            // Three rows now, laid out in the order the signal passes through them.
            int rowHeight = (inner.getHeight() - groupPad - gap * 2) / 3;

            auto row1 = inner.removeFromTop(rowHeight);
            auto b1 = splitChain(row1, {2, 5});
            layoutSection(distortionSection, b1[0], {fxDistortionKnob.get()});
            layoutSection(chorusSection, b1[1],
                          {chorusRateKnob.get(), chorusDepthKnob.get(), chorusMixKnob.get()});
            distortionSection.setStyle(ui::SectionStyle::chainStart, 7);
            chorusSection.setStyle(ui::SectionStyle::chainMiddle, 8);
            inner.removeFromTop(gap);

            auto row2 = inner.removeFromTop(rowHeight);
            auto b2 = splitChain(row2, {1, 1});
            layoutSection(phaserSection, b2[0],
                          {phaserRateKnob.get(), phaserFeedbackKnob.get(), phaserMixKnob.get()});
            layoutSection(delaySection, b2[1],
                          {delayTimeKnob.get(), delayFeedbackKnob.get(), delayMixKnob.get()});
            phaserSection.setStyle(ui::SectionStyle::chainMiddle, 9);
            delaySection.setStyle(ui::SectionStyle::chainMiddle, 10);
            delaySyncButton.setBounds(b2[1].getRight() - 62, b2[1].getY() + 1, 58, 15);
            inner.removeFromTop(gap);

            auto row3 = inner.removeFromTop(rowHeight);
            auto b3 = splitChain(row3, {3, 4, 2});
            layoutSection(reverbSection, b3[0], {reverbSizeKnob.get(), reverbMixKnob.get()});
            layoutSection(eqSection, b3[1], {eqLowKnob.get(), eqMidKnob.get(), eqHighKnob.get()});
            layoutSection(outSection, b3[2], {gainKnob.get()});
            reverbSection.setStyle(ui::SectionStyle::chainMiddle, 11);
            eqSection.setStyle(ui::SectionStyle::chainMiddle, 12);
            outSection.setStyle(ui::SectionStyle::chainEnd, 13);

            groupBoxes.push_back({fxArea.withTop(groupTop).withBottom(inner.getY() + 4), "Master effects",
                                  ui::colours::accentWarm});
        }

        column.removeFromTop(gap * 2);

        {
            int groupTop = column.getY();
            auto inner = column;
            inner.removeFromTop(groupPad);
            inner.reduce(8, 0);

            int rowHeight = (inner.getHeight() - groupPad - gap * 2) / 3; // LFOs, envelope, expression

            auto row1 = inner.removeFromTop(rowHeight);
            auto c1 = splitPlain(row1, {4, 3});
            layoutSection(lfoSection, c1[0],
                          {lfoRateKnob.get(), lfoToFilterKnob.get(), lfoToAmpKnob.get(), lfoToWaveKnob.get()});
            layoutSection(lfo2Section, c1[1],
                          {lfo2RateKnob.get(), lfo2ToWaveKnob.get(), lfo2ToCutoffKnob.get()});
            lfoSection.setStyle(ui::SectionStyle::modulator);
            lfo2Section.setStyle(ui::SectionStyle::modulator);
            lfo1SyncButton.setBounds(c1[0].getRight() - 62, c1[0].getY() + 1, 58, 15);
            // Stereo belongs with the LFO it widens, not out by the keyboard.
            stereoLfoButton.setBounds(c1[0].getRight() - 130, c1[0].getY() + 1, 66, 15);
            lfo2SyncButton.setBounds(c1[1].getRight() - 62, c1[1].getY() + 1, 58, 15);
            inner.removeFromTop(gap);

            auto row2 = inner.removeFromTop(rowHeight);
            layoutSection(modEnvSection, row2,
                          {modEnvAttackKnob.get(), modEnvDecayKnob.get(), modEnvSustainKnob.get(),
                           modEnvReleaseKnob.get(), modEnvToWaveKnob.get(), modEnvToCutoffKnob.get()});
            modEnvSection.setStyle(ui::SectionStyle::modulator);
            inner.removeFromTop(gap);

            // Playing itself, as a source alongside the LFOs and the envelope.
            auto row3 = inner.removeFromTop(rowHeight);
            layoutSection(expressionSection, row3,
                          {velToCutoffKnob.get(), velToWaveKnob.get(), velToAmpKnob.get(),
                           velToResonanceKnob.get(), accentKnob.get(),
                           wheelToCutoffKnob.get(), wheelToWaveKnob.get(), pressureToCutoffKnob.get()});
            expressionSection.setStyle(ui::SectionStyle::modulator);

            groupBoxes.push_back({rightColumn.withTop(groupTop).withBottom(inner.getY() + 4), "Modulation",
                                  ui::colours::accentMod});
        }
    }

    bounds.removeFromTop(gap);

    // The transport belongs beside the readout rather than out among the
    // keyboard switches: you arm a take and then watch the screen say what it
    // recorded, so the two are read together. Sized to the screen's own height,
    // because a pair of badges next to a panel this tall reads as a footnote to
    // it rather than as the control you reach for first.
    auto lcdRow = bounds.removeFromTop(lcdHeight);

    constexpr int transportGap = 10;
    constexpr int transportPad = 10;
    const int transportSize = lcdHeight - transportPad * 2;
    const int transportWidth = transportSize * 2 + transportGap + transportPad * 2;

    transportBox = lcdRow.removeFromLeft(transportWidth);
    lcdRow.removeFromLeft(gap);
    lcd.setBounds(lcdRow);

    auto transport = transportBox.reduced(transportPad);
    playStopButton.setBounds(transport.removeFromLeft(transportSize));
    transport.removeFromLeft(transportGap);
    recordButton.setBounds(transport.removeFromLeft(transportSize));

    bounds.removeFromTop(12);
    auto keyboardRow = bounds.removeFromTop(keyboardHeight);

    // Kept whole, before the columns below eat into it, so the Drums page can
    // wash over the lot: the keys, the octave buttons, Hold, and the
    // arpeggiator beside them. None of it plays the kit.
    playingStripBounds = keyboardRow;

    // Arpeggiator sits beside the keyboard: it acts on what you play, so it
    // belongs with the performance controls rather than in the patch panel.
    auto arpArea = keyboardRow.removeFromRight(juce::jlimit(400, 520, keyboardRow.getWidth() / 3));
    keyboardRow.removeFromRight(10);

    // Octave and hold now ride on the keyboard's own line, which is where the
    // things they do to it can be seen happening.
    constexpr int switchWidth = 132;
    auto switchColumn = keyboardRow.removeFromRight(switchWidth);
    keyboardRow.removeFromRight(12);

    auto stack = switchColumn.withSizeKeepingCentre(switchWidth, octaveHeight * 2 + 6);
    auto octaveRow = stack.removeFromTop(octaveHeight);
    constexpr int octaveButtonWidth = (switchWidth - 6) / 2;
    octaveDownButton.setBounds(octaveRow.removeFromLeft(octaveButtonWidth));
    octaveUpButton.setBounds(octaveRow.removeFromRight(octaveButtonWidth));
    stack.removeFromTop(6);
    holdNoteButton.setBounds(stack.removeFromTop(octaveHeight));

    // Five octaves, not the twelve this row used to spread across a wide
    // screen - where the last few were drawn past note 127 as blank board, and
    // the ones that were real had keys too narrow to hit one at a time. The
    // keys keep the whole row and get the width back instead: on a 1920 screen
    // that is 34 pixels a key rather than 16. Oct - and Oct + still walk the
    // five up and down.
    constexpr int whiteKeysShown = 5 * 7;
    const int keyWidth = juce::jmax(10, keyboardRow.getWidth() / whiteKeysShown);
    keyboardComponent.setKeyWidth((float) keyWidth);

    // Sized to a whole number of keys, so the row ends on a B rather than on
    // half of the C after it.
    keyboardComponent.setBounds(keyboardRow.removeFromLeft(
        juce::jmin(keyboardRow.getWidth(), keyWidth * whiteKeysShown)));

    arpSection.setBounds(arpArea);
    // Sync switches ride on the top-right of the section they control.
    auto syncBadge = [](juce::Rectangle<int> section) {
        return juce::Rectangle<int>(section.getRight() - 62, section.getY() + 1, 58, 15);
    };
    arpSyncButton.setBounds(syncBadge(arpArea));
    auto arpInner = arpArea.reduced(8, 0).withTrimmedTop(16).withTrimmedBottom(4);
    // Wide enough for the longest names ("Pinky Up-Down", "Octave Bounce") to
    // stay readable rather than being squeezed down to an ellipsis.
    auto arpButtons = arpInner.removeFromLeft(112);
    int arpButtonHeight = arpButtons.getHeight() / 3;
    arpEnabledButton.setBounds(arpButtons.removeFromTop(arpButtonHeight).reduced(0, 2));
    arpModeButton.setBounds(arpButtons.removeFromTop(arpButtonHeight).reduced(0, 2));
    arpPatternButton.setBounds(arpButtons.reduced(0, 2));

    // Link sits next to Rate because the two are read together: Link says what
    // grid the arpeggiator is on, Rate says where on it.
    int arpKnobWidth = arpInner.getWidth() / 6;
    arpLinkKnob->setBounds(arpInner.removeFromLeft(arpKnobWidth).reduced(2, 0));
    arpRateKnob->setBounds(arpInner.removeFromLeft(arpKnobWidth).reduced(2, 0));
    arpSwingKnob->setBounds(arpInner.removeFromLeft(arpKnobWidth).reduced(2, 0));
    arpOctavesKnob->setBounds(arpInner.removeFromLeft(arpKnobWidth).reduced(2, 0));
    arpGateKnob->setBounds(arpInner.removeFromLeft(arpKnobWidth).reduced(2, 0));
    bpmKnob->setBounds(arpInner.reduced(2, 0));
}
} // namespace wavelathe
