// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "FxpPreset.h"
#include "UiComponents.h"
#include "WaveformDisplay.h"
#include "OutputScope.h"
#include "AppSettings.h"
#include "FactoryPresets.h"
#include "PresetBrowser.h"
#include "Scales.h"
#include "Arpeggiator.h"
#include "SequencerPanel.h"
#include "MasteringPanel.h"
#include "BusEffectsPanel.h"
#include "DrumPanel.h"
#include "ParameterRegistry.h"
#include <array>
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

namespace wavelathe
{
class WaveLatheEditor : public juce::AudioProcessorEditor,
                        private juce::Timer,
                        private juce::KeyListener,
                        private juce::MidiKeyboardState::Listener
{
public:
    explicit WaveLatheEditor(WaveLatheProcessor& processor);
    ~WaveLatheEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // Painted by the scrolled content component.
    void paintContent(juce::Graphics&);
    void paintContentOverChildren(juce::Graphics&);

    // The line under "Saved project": what the file carries on top of every
    // page's settings. Static and public so a test can hold it to its word.
    static juce::String describeSavedProject(const PresetValues& values);

private:
    WaveLatheProcessor& processorRef;
    ui::WaveLatheLookAndFeel lookAndFeel;

    // The panel keeps growing as the synth does, so it lives in a viewport
    // and scrolls rather than forcing an ever-taller window.
    struct ContentComponent : juce::Component
    {
        explicit ContentComponent(WaveLatheEditor& ownerToUse) : owner(ownerToUse) {}
        void paint(juce::Graphics& g) override { owner.paintContent(g); }

        // OVER the children, not behind them. paint() in JUCE draws beneath a
        // component's children, so the Drums page's wash over the keyboard
        // drawn there would sit under the very keys it is meant to be dimming
        // and do nothing visible - the same trap DrumPanel names.
        void paintOverChildren(juce::Graphics& g) override { owner.paintContentOverChildren(g); }

        WaveLatheEditor& owner;
    };

    ContentComponent content{*this};

    // Sits over the whole panel while MIDI learn is on, showing what each dial
    // is bound to and taking the clicks that assign it. One component rather
    // than one per dial: sixty-three overlays would be sixty-three things to
    // keep in step with a layout that already moves them, and the layer has to
    // swallow clicks anyway so that learning a dial cannot also turn it.
    struct MidiLearnLayer : juce::Component
    {
        explicit MidiLearnLayer(WaveLatheEditor& ownerToUse) : owner(ownerToUse)
        {
            // Clicks land here, EXCEPT on the button below - which is the one
            // child, and the one thing on this layer that must stay clickable.
            setInterceptsMouseClicks(true, true);

            addAndMakeVisible(doneButton);
            doneButton.onClick = [this] { owner.setMidiLearnEnabled(false); };

            // Never takes focus, so Escape keeps reaching the editor and the
            // computer keyboard keeps playing notes while learn mode is up.
            doneButton.setWantsKeyboardFocus(false);
            doneButton.setColour(juce::TextButton::buttonColourId, ui::colours::accent.withAlpha(0.25f));
            doneButton.setColour(juce::TextButton::textColourOffId, ui::colours::textPrimary);
        }

        void paint(juce::Graphics& g) override { owner.paintLearnLayer(g); }
        void mouseDown(const juce::MouseEvent& e) override { owner.learnLayerClicked(e); }
        void mouseMove(const juce::MouseEvent&) override { repaint(); }
        juce::MouseCursor getMouseCursor() override { return juce::MouseCursor::PointingHandCursor; }

        void resized() override
        {
            auto strip = getLocalBounds().removeFromBottom(footerHeight);
            doneButton.setBounds(strip.removeFromRight(96).reduced(10, 5));
        }

        // The way out, sitting on the layer itself because the layer covers the
        // Options button that switched learn on. Escape does the same thing,
        // but a mode you can only leave by knowing a keystroke is a trap.
        static constexpr int footerHeight = 30;
        juce::TextButton doneButton{"Done"};

        WaveLatheEditor& owner;
    };

    MidiLearnLayer learnLayer{*this};
    bool midiLearnOn = false;
    uint32_t lastMapVersion = 0;

    // What was armed when we last looked. The bind itself happens on the audio
    // thread and leaves nothing behind saying which dial it was for, so the
    // dial is remembered here in order to name it afterwards.
    int armedWhenLastChecked = -1;

    // Which parameters have a controller on them, rebuilt whenever the map
    // changes. Cached because the question is asked of every dial on every
    // frame, and answering it from the map means scanning all 128 slots.
    std::array<bool, paramreg::maxParameters> parameterIsMapped{};
    void rebuildMappedParameters();

    void setMidiLearnEnabled(bool shouldLearn);
    void saveMidiMap();
    void paintLearnLayer(juce::Graphics&);
    void learnLayerClicked(const juce::MouseEvent&);

    // The dial under a point on the layer, as a registry id, or -1. Bounds come
    // from the dials themselves so nothing has to be kept in step by hand.
    int parameterAt(juce::Point<int> pointOnLayer) const;

    // Watches the map's version so a bind that happened on the audio thread -
    // which is where every bind happens - shows up here without the audio
    // thread having to reach into the interface.
    void followMidiMap();

    // The host's parameter for a dial, or nullptr if that dial has no registry
    // id. Found through automatedKnobs, which already pairs every automatable
    // dial with its id - built for the sequencer, and the same question a host
    // needs answered. Three dials deliberately have no id (BPM, Arp Link, Arp
    // Rate), so nullptr is an ordinary answer rather than a fault.
    HostParameter* hostParameterFor(const ui::ParameterKnob* knob) const;

    // ---- MIDI monitor ------------------------------------------------------
    // Switched on from the Options menu, and off again with a summary of what
    // arrived - which is the part that makes a silent run into a result rather
    // than a shrug.
    void toggleMidiMonitor();

    // Prints whatever the audio thread last caught. A sweep of one control
    // rewrites its own line and reaching for a different one starts a new line,
    // because the test this exists for asks for two controls and the answer is
    // whether they read as two.
    void followMidiMonitor();

    uint32_t lastMonitorSequence = 0;
    MidiMonitor::Kind lastMonitorKind = MidiMonitor::Kind::none;
    int lastMonitorNumber = -1;
    int lastMonitorChannel = -1;

    void layoutContent();

    // Dials, grouped in the order the signal actually passes through them.
    std::unique_ptr<ui::ParameterKnob> waveKnob, voicesKnob, detuneKnob, widthKnob, osc1LevelKnob;
    std::unique_ptr<ui::ParameterKnob> osc2LevelKnob, osc2WaveKnob, osc2SemiKnob, osc2FineKnob;
    std::unique_ptr<ui::ParameterKnob> subLevelKnob, subWaveKnob, subOctaveKnob;
    std::unique_ptr<ui::ParameterKnob> noiseLevelKnob, noiseColourKnob;
    std::unique_ptr<ui::ParameterKnob> cutoffKnob, resonanceKnob, filterTypeKnob;
    std::unique_ptr<ui::ParameterKnob> driveKnob;
    std::unique_ptr<ui::ParameterKnob> attackKnob, decayKnob, sustainKnob, releaseKnob;
    std::unique_ptr<ui::ParameterKnob> lfoRateKnob, lfoToFilterKnob, lfoToAmpKnob, lfoToWaveKnob;
    std::unique_ptr<ui::ParameterKnob> modEnvAttackKnob, modEnvDecayKnob, modEnvSustainKnob, modEnvReleaseKnob;
    std::unique_ptr<ui::ParameterKnob> modEnvToWaveKnob, modEnvToCutoffKnob;
    std::unique_ptr<ui::ParameterKnob> lfo2RateKnob, lfo2ToWaveKnob, lfo2ToCutoffKnob;
    std::unique_ptr<ui::ParameterKnob> gainKnob;
    std::unique_ptr<ui::ParameterKnob> fxDistortionKnob, delayTimeKnob, delayFeedbackKnob, delayMixKnob;
    std::unique_ptr<ui::ParameterKnob> reverbSizeKnob, reverbMixKnob;
    std::unique_ptr<ui::ParameterKnob> chorusRateKnob, chorusDepthKnob, chorusMixKnob;
    std::unique_ptr<ui::ParameterKnob> phaserRateKnob, phaserFeedbackKnob, phaserMixKnob;
    std::unique_ptr<ui::ParameterKnob> eqLowKnob, eqMidKnob, eqHighKnob;
    std::unique_ptr<ui::ParameterKnob> glideKnob;

    // One window serves every tooltip in the editor. Only components that
    // return a non-empty getTooltip show one, so this costs nothing where
    // nothing has anything to say. Declared before the child components so it
    // is still alive while they are being torn down.
    juce::TooltipWindow tooltips{nullptr, 700};
    std::unique_ptr<ui::ParameterKnob> velToCutoffKnob, velToWaveKnob, velToAmpKnob;
    std::unique_ptr<ui::ParameterKnob> velToResonanceKnob, accentKnob;
    std::unique_ptr<ui::ParameterKnob> wheelToCutoffKnob, wheelToWaveKnob, pressureToCutoffKnob;
    juce::ToggleButton stereoLfoButton{"Stereo"};

    // How a line moves between notes. They sit with the oscillators because
    // that is what they change - the pitch, not the effects.
    juce::ToggleButton monoButton{"Mono"};
    juce::ToggleButton legatoButton{"Legato"};

    ui::FlowSection oscSection{"Oscillator 1", ui::colours::accent};
    ui::FlowSection osc2Section{"Oscillator 2", ui::colours::accent};
    ui::FlowSection subNoiseSection{"Sub & Noise", ui::colours::accent};
    ui::FlowSection filterSection{"Filter", ui::colours::accent};
    ui::FlowSection driveSection{"Drive", ui::colours::accentWarm};
    ui::FlowSection ampSection{"Amp Envelope", ui::colours::accentWarm};
    ui::FlowSection lfoSection{"LFO", ui::colours::accent};
    ui::FlowSection distortionSection{"Distortion", ui::colours::accentWarm};
    ui::FlowSection delaySection{"Delay", ui::colours::accent};
    ui::FlowSection chorusSection{"Chorus", ui::colours::accent};
    ui::FlowSection phaserSection{"Phaser", ui::colours::accent};
    ui::FlowSection reverbSection{"Reverb", ui::colours::accent};
    ui::FlowSection eqSection{"EQ", ui::colours::accentWarm};
    ui::FlowSection modEnvSection{"Mod Envelope", ui::colours::accentWarm};
    ui::FlowSection lfo2Section{"LFO 2", ui::colours::accent};
    ui::FlowSection expressionSection{"Expression", ui::colours::accentMod};
    ui::FlowSection outSection{"Output", ui::colours::accentWarm};

    WaveformDisplay waveformDisplay;

    // Beside it rather than inside it, because the two are different kinds of
    // picture: one is what the patch would sound like, the other is what the
    // synth is doing. Sharing a component would have meant sharing a redraw
    // policy, and one of them redraws on a dial and the other at screen rate.
    OutputScope outputScope;
    ui::LcdDisplay lcd{3};

    // Every file action, folder setting and view option lives behind this one
    // button in the header.
    juce::TextButton optionsButton{"Options"};

    // Swaps the panel between the synth's controls and the sequencer page.
    // Three pages now. The index IS the page: the two-page version carried a
    // bool called showingSequencer, which had exactly one more page in it than
    // it could name.
    // Drums sits between the Sequencer and the Master, which is where it sits
    // in the signal too: you draw a beat, you mix it, then the whole thing
    // meets the mastering chain.
    enum Page { synthPage = 0, sequencerPage = 1, drumsPage = 2, masterPage = 3 };
    ui::PageTabs pageTabs{juce::StringArray{"Synth", "Sequencer", "Drums", "Master"}};

    // Gives the synth the whole display. It lives in the window's title bar
    // beside minimise and close, because that is where the controls that act on
    // the window belong - and it falls back into the header while full screen
    // is on, since full screen takes that title bar away with it.
    ui::FullScreenButton fullScreenButton;
    void placeFullScreenButton();

    // The window full screen acts on, or nullptr when there is none of ours.
    // That is every plugin instance: the window belongs to the host. Asked as
    // one question in one place because three separate copies of it had already
    // drifted apart - the menu asked for a ResizableWindow and was right, the
    // button asked for a DocumentWindow and fell back into the header, and
    // setFullScreen asked for neither and so put LIVE'S window into kiosk mode.
    juce::DocumentWindow* ownWindow() const;

    // Sizes the window to the screen it was actually opened on. Run once, on
    // the first tick that finds a window: the editor's own constructor has to
    // guess at a size before there is a window to have been placed anywhere.
    void fitToOpeningDisplay();

    // Where it goes when the title bar is not there to hold it. Worked out by
    // the layout whether it is needed or not, so the fallback does not have to
    // guess at a position the header never actually laid out.
    juce::Rectangle<int> headerFullScreenBounds;
    int currentPage = synthPage;

    // The keyboard, its octave buttons and the arpeggiator, as one strip. Kept
    // so the Drums page can dim the lot: none of it plays the kit, and a row of
    // controls that looks live and does nothing for the page you are on is
    // worse than one that says so.
    juce::Rectangle<int> playingStripBounds;
    std::unique_ptr<SequencerPanel> sequencerPanel;
    std::unique_ptr<DrumPanel> drumPanel;
    std::unique_ptr<MasteringPanel> masteringPanel;

    // The Master page's bus effects, above the mastering chain - see
    // BusEffectsPanel for why it is not part of MasteringPanel.
    std::unique_ptr<BusEffectsPanel> busEffectsPanel;

    // Everything belonging to the synth page, so the two views can be swapped
    // without each component needing to be named twice.
    std::vector<juce::Component*> synthPageComponents;

    // Arpeggiator, sitting beside the keyboard as a performance control.
    ui::FlowSection arpSection{"Arpeggiator", ui::colours::accentMod};
    juce::ToggleButton arpEnabledButton{"On"};
    juce::TextButton arpModeButton{"Up"};
    juce::TextButton arpPatternButton{"Straight"};
    std::unique_ptr<ui::ParameterKnob> arpRateKnob, arpLinkKnob, arpOctavesKnob, arpGateKnob, arpSwingKnob;

    // The Rate dial measures note divisions while the arpeggiator is following
    // the tempo and plain speed while it is not, and reads out whatever the
    // sequencer is imposing when it is linked to it.
    void updateArpRateScale();

    // What the rate dial was last built for, so it is only rebuilt when the
    // sync setting, the link or the sequencer's own rate actually moves.
    int arpRateScaleBuiltFor = -1;

    // Tempo the synced controls follow, plus per-feature sync switches.
    std::unique_ptr<ui::ParameterKnob> bpmKnob;
    juce::ToggleButton arpSyncButton{"Sync"};
    juce::ToggleButton delaySyncButton{"Sync"};
    juce::ToggleButton lfo1SyncButton{"Sync"};
    juce::ToggleButton lfo2SyncButton{"Sync"};

    juce::MidiKeyboardComponent keyboardComponent;
    juce::TextButton octaveDownButton{"Oct -"};
    juce::TextButton octaveUpButton{"Oct +"};
    juce::ToggleButton holdNoteButton{"Hold note"};

    // The sequencer's transport, repeated down here by the keyboard: starting a
    // pattern and arming a take are things you reach for while playing, and the
    // controls for them should not be on a page you have to leave to get to.
    ui::TransportButton playStopButton{ui::TransportButton::Shape::play, ui::colours::accent};
    ui::TransportButton recordButton{ui::TransportButton::Shape::record, ui::colours::accentWarm};

    // The panel drawn behind them, kept from the layout so the paint can put a
    // box round the pair and set them apart from the keyboard controls.
    juce::Rectangle<int> transportBox;
    int lowestVisibleKey = 24;
    int heldNoteNumber = 36;
    std::atomic<int> lastClickedNote{36};
    int lastAnnouncedNote = -1;

    // Playing from the computer keyboard. Handled here rather than by the
    // MidiKeyboardComponent's own mapping, which only works while that one
    // component holds keyboard focus - so a single click on any dial used to
    // stop the keys working, exactly when you most want to hear the change.
    struct ComputerKey
    {
        int keyCode;
        int semitoneOffset;
    };

    static const ComputerKey computerKeys[];
    static const int numComputerKeys;

    // The computer keyboard's OTHER job: on the Drums page those same keys
    // strike the twelve slots instead of playing the synth. The table itself
    // lives in the .cpp - nothing outside needs it, and a file-scope array is
    // a size a static_assert can actually read.

    // Which drum keys are currently held, so a leant-on key strikes once.
    //
    // Only the DOWN edge matters: a drum has no length, so there is nothing for
    // a release to do. That is the whole difference between this and the note
    // keys above, which have to remember what they started in order to end it.
    std::vector<bool> drumKeyHeld;

    // Are the computer keys playing drums right now? Read in one place, so
    // switching page and switching what the keys do cannot disagree.
    bool computerKeysPlayDrums() const { return currentPage == drumsPage; }

    // The note each mapped key is currently sounding, or -1. Stored as the note
    // actually started, so releasing is correct even after an octave shift.
    std::vector<int> computerKeyNotes;
    int computerKeyBaseNote = 48; // the note the 'A' key plays

    // Whether the letters being typed belong to something other than this
    // panel - a menu, a dialog, a name field, another window of ours. The note
    // keys are read from the global key state rather than delivered as events,
    // so nothing stops them reaching this editor on their own.
    bool keyboardIsElsewhere() const;
    void updateComputerKeyboardNotes(bool restartHeldKeys = false);
    void releaseComputerKeyboardNotes();

    // Enclosing outlines that separate the voice's signal chain from the
    // master effects that sit across the whole output, and both from the
    // modulators, which are not in the audio path at all.
    struct GroupBox
    {
        juce::Rectangle<int> bounds;
        juce::String title;
        juce::Colour tint;
    };
    std::vector<GroupBox> groupBoxes;


    std::unique_ptr<juce::FileChooser> fileChooser;

    std::unique_ptr<std::thread> optimizerThread;
    std::shared_ptr<std::atomic<bool>> optimizerCancel;
    std::atomic<bool> optimizerRunning{false};
    std::atomic<int> optimizerGeneration{0};
    std::atomic<int> optimizerTotalGenerations{0};
    std::atomic<float> optimizerBestDistance{0.0f};

    void buildKnobs();
    void refreshKnobsFromParameters();

    // A patch has just arrived: put it on the dials, and tell the host.
    //
    // Everything that loads one from in here goes through this rather than
    // calling the two halves separately, because calling them separately is
    // exactly what went wrong the first time - the host half was added to the
    // one path that ran through the processor, and the five that do not were
    // left telling the host nothing. Live went on showing the previous patch's
    // values and would have written them back over this one.
    void adoptLoadedPatch();

    // A program change from the host lands in the parameters on whichever
    // thread the host chose, so the panel notices it on the tick rather than
    // being told - the same arrangement the MIDI map has, and for the same
    // reason: that thread may not touch a component.
    void followProgramChange();
    uint32_t lastProgramChange = 0;

    // The BPM dial, when something other than the dial is setting the tempo -
    // a host's transport, or incoming MIDI clock. Watched on the tick for the
    // same reason as the rest of this: the tempo is settled on the audio thread.
    void followTempo();
    bool lastTempoExternal = false;
    double lastShownBpm = 0.0;
    void savePresetClicked();
    void loadPresetClicked();

    // A project is everything: the synth, the sequencer's pattern and the
    // automation over it. A preset is the sound on its own.
    void saveProjectClicked();
    void loadProjectClicked();

    // The standalone wrapper owns the audio device, so this hands over to its
    // dialog rather than reimplementing one.
    void showAudioSettings();

    // Stepping through the history, and putting a recovered project back.
    void performUndo(bool redoInstead);

    // Undo is polled rather than handled as a key press, for the same reason
    // the note keys are: once anything in the panel has taken focus, key
    // presses stop arriving here. Polling asks the keyboard directly, so the
    // shortcut works wherever you last clicked.
    void checkUndoShortcuts();
    bool undoChordHeld = false;
    bool redoChordHeld = false;
    void applyRestoredProject(const PresetValues& values, const juce::String& message);
    void offerRecoveredProject();

    // The wrapper puts a second Options button in the title bar offering a
    // narrower version of this menu. Everything it does now lives here, so it
    // is taken out of the way rather than left to contradict this one.
    void hideWrapperOptionsButton();

    // Keeps the transport buttons showing what the sequencer is actually doing,
    // whichever page started it.
    void refreshTransportButtons();
    bool wrapperButtonHidden = false;
    void matchSampleClicked();
    void deepMatchClicked();
    void sampleWavetableClicked();
    void exportAbClicked();
    void toggleFullScreen();
    void setFullScreen(bool shouldBeFullScreen);
    bool isFullScreen() const;
    void showOptionsMenu();
    void handleOptionsMenuResult(int result);
    void changeFolderClicked(AppSettings::Folder which);
    void browsePresetsClicked();
    // Puts loaded values on screen and into the engine. A synth preset changes
    // the Synth page's sound and nothing else; a project changes everything on
    // every page - see FxpPreset::ApplyScope for exactly where the line falls.
    //
    // There used to be a third way, and it went in 0.48.0: a preset file saved
    // from here carried the whole project, so loading one asked afterwards
    // whether to take its pattern too. A synth preset carries no pattern now,
    // and a user who wants the pattern saves a project.
    void applyPresetValues(const PresetValues& values, const juce::String& message,
                           FxpPreset::ApplyScope scope = FxpPreset::ApplyScope::synthPreset);

    // Offers the assignments saved inside a project, once the rest of it is
    // loaded. Silent when the project has none, and when it has exactly the
    // ones already on the desk - a question whose two answers do the same
    // thing is a click for nothing.
    void offerMidiSetupFromProject(const PresetValues& values);

    void adoptKeyboardChannel(int channel);
    static juce::String describeKeyboardChannel(int channel);

    // Puts a map into the processor, remembers it, and gets the interface back
    // in step with it. Three things that must always happen together.
    void adoptMidiMap(const juce::String& text);
    juce::String loadWavetableFromPreset(const PresetValues& values);
    void setPage(int page);
    void gatherSynthPageComponents();

    // Pairs each dial with its automation lane, and each frame tells the
    // sequencer which ones the user currently has hold of - that is what REC
    // captures, and what stops a recorded lane fighting a hand on the dial.
    void registerAutomatableKnobs();
    void followAutomation();

    std::vector<std::pair<ui::ParameterKnob*, int>> automatedKnobs;

    void updateArpModeButton();
    void showArpModeMenu();
    void updateArpPatternButton();
    void showArpPatternMenu();
    juce::File folderFor(AppSettings::Folder which) const;
    void rememberFolder(AppSettings::Folder which, const juce::File& file);
    void startDeepMatch(const juce::File& file);
    void stopOptimizerThread();
    void shiftOctave(int semitones);
    void setHoldEnabled(bool shouldHold);
    void layoutSection(ui::FlowSection& section, juce::Rectangle<int> area,
                        const std::vector<ui::ParameterKnob*>& knobs);

    void timerCallback() override;
    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;
    bool keyStateChanged(bool isKeyDown, juce::Component* origin) override;
    void handleNoteOn(juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity) override;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WaveLatheEditor)
};
} // namespace wavelathe
