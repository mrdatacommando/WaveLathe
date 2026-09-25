// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "SynthParameters.h"
#include "WavetableOscillator.h"
#include "SynthSound.h"
#include "SynthVoice.h"
#include "MasterEffects.h"
#include "Mastering/MasteringChain.h"
#include "Mastering/MasteringControls.h"
#include "Drums/DrumKit.h"
#include "DrumSampleLoader.h"
#include "Arpeggiator.h"
#include "HarmonyProcessor.h"
#include "NoteLatch.h"
#include "MonoVoice.h"
#include "StepSequencer.h"
#include "TempoSync.h"
#include "FxpPreset.h"
#include "UndoHistory.h"
#include "MidiMap.h"
#include "MidiMonitor.h"
#include "HostParameters.h"
#include "OutputTap.h"
#include "ParameterRegistry.h"
#include "ParameterMask.h"
#include <array>

namespace wavelathe
{
// Also mastering::Controls, which is how the Master page reaches these dials
// without knowing what a WaveLatheProcessor is. The same page appears in the
// separate effect plugin, where the values are host parameters instead of
// fields in a preset - the panel cannot be allowed to care which.
class WaveLatheProcessor : public juce::AudioProcessor,
                           public mastering::Controls,
                           private juce::Timer,
                           private juce::AsyncUpdater
{
public:
    WaveLatheProcessor();
    ~WaveLatheProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "WaveLathe"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    // Not zero, which is what it said from the first build until somebody
    // bounced a track. The host renders for exactly this long after the last
    // note lets go, so a wrong answer here does not sound odd - it cuts the end
    // off the sound, silently, and only on export.
    double getTailLengthSeconds() const override;

    // The factory bank, as the host's own preset list. It said one nameless
    // program until 0.38.2, which is what a plugin with nothing to offer says -
    // so a DAW's preset menu was empty while sixty-five sounds sat inside,
    // reachable only from WaveLathe's own browser. A host counts programs to
    // decide whether to draw that menu at all.
    //
    // Read-only: these are the shipped sounds, and changeProgramName does
    // nothing rather than pretend to rename one. Somebody's own patches are
    // saved as files, which is a different thing from a program slot.
    int getNumPrograms() override;
    int getCurrentProgram() override { return currentProgram.load(); }
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int, const juce::String&) override {}

    // Bumped by every program change, whoever asked for it. The editor watches
    // this rather than being told, the same way it watches the MIDI map: a
    // program change can arrive on the audio thread, and that is not a thread
    // that may touch a component.
    uint32_t getProgramChangeCount() const { return programChanges.load(); }

    // The tempo everything is actually syncing to, and whether it came from
    // somewhere other than the BPM dial. Published from the audio thread each
    // block so the panel can show what is true rather than what was typed: a
    // readout saying 154 while the arpeggiator runs at the song's 120 is worse
    // than no readout, because it is believed.
    double getEffectiveBpm() const { return effectiveBpm.load(); }
    bool isTempoExternal() const { return tempoIsExternal.load(); }

    // The whole project - synth, arpeggiator, tempo, and the sequencer with its
    // pattern and automation. The standalone app stores this on its way out and
    // hands it back on the next launch, and a host saves it with the song, so
    // what you were working on is still there when you come back to it.
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // The same project, as values - what Save Preset writes and Load reads.
    // includeWavetable is off for undo snapshots: the tables are held by the
    // processor for as long as it lives, so a snapshot can point at one instead
    // of carrying a copy.
    PresetValues captureProject(const juce::String& name, bool includeWavetable = true) const;
    void applyProject(const PresetValues& values);

    // Puts the sixteen sample slots back from a captured state. Part of
    // applyProject, and separately reachable because undo, redo and crash
    // recovery rebuild a state through the editor rather than through
    // applyProject and have to do this themselves.
    void restoreDrumSamples(const PresetValues& values);

    // ---- Undo -----------------------------------------------------------
    // Called before anything destructive, with a name for what is about to
    // happen. Everything else follows from the snapshot.
    void recordUndoPoint(const juce::String& action);
    bool undo(PresetValues& outValues);
    bool redo(PresetValues& outValues);
    const UndoHistory& getHistory() const { return history; }

    // ---- Recovery -------------------------------------------------------
    // The project is written to a temporary file every few minutes and the
    // file is deleted on a clean exit, so one being there at startup means the
    // last run ended in a way that did not get to tidy up.
    static juce::File getRecoveryFile();
    bool hasRecoverableProject() const { return recoveryAvailable; }
    bool loadRecoveredProject(PresetValues& outValues, juce::String& errorMessage);
    void discardRecovery();
    void saveRecoveryNow();

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    SynthParameters& getParameters() { return parameters; }
    juce::MidiKeyboardState& getKeyboardState() { return keyboardState; }
    StepSequencer& getSequencer() { return sequencer; }

    // Play one drum voice now, at the next block. What clicking a row's name
    // on the drum grid does - the way every drum machine lets you hear a voice
    // without running the pattern, and the only way to audition one at all
    // until phase 5 gives the kit a mixer.
    void auditionDrumVoice(int voice, bool accent = false);

    // The loudest that voice has been since this was last called, and cleared
    // by calling it. Drives the indicator beside each Level dial, so it is
    // expected to be read by exactly one caller at a display rate - two
    // readers would each see part of the signal.
    float takeDrumVoicePeak(int voice) { return drumKit.takeVoicePeak(voice); }

    // The kit's dials, for whatever ends up driving them. Not registered
    // parameters yet; see the note beside drumParameters.
    drums::KitParameters& getDrumParameters() { return drumParameters; }
    const drums::KitParameters& getDrumParameters() const { return drumParameters; }

    // The published sample bank, for anything that needs to draw or measure a
    // slot rather than play it. Const, and it is the same pointer the audio
    // thread is reading - so whoever takes it must only read, and must not
    // hold it across anything that could publish a new one.
    const drums::SampleBank* getPublishedSampleBank() const { return drumKit.getSampleBank(); }

    // ---- Sample slots --------------------------------------------------
    //
    // A slot holds a recording or it holds its own synthesised circuit, and
    // the two are alternatives. Loading one onto Kick replaces the kick.
    //
    // All four are MESSAGE THREAD ONLY. They rebuild the bank the audio
    // thread reads and publish it as one pointer, which is the only part of
    // this that crosses threads.
    bool loadDrumSample(int voice, const juce::File& file, juce::String& error);
    void setDrumSample(int voice, drums::SampleRef sample);
    void clearDrumSample(int voice);

    drums::SampleRef getDrumSample(int voice) const;
    juce::String getDrumSampleName(int voice) const;

    // Which synthesised drum a slot plays, or drums::noEngine for a blank one.
    //
    // Safe from any thread: it is one atomic, and the kit latches it when a
    // hit starts rather than reading it while one rings.
    void setDrumEngine(int voice, int engine);
    int getDrumEngine(int voice) const;

    // What the slot is actually making a sound with, for a header or a
    // tooltip: the sample's name if one is loaded, otherwise the drum's, and
    // "Empty" when it is neither.
    juce::String getDrumSlotDescription(int voice) const;

    // Which hardware controller moves which dial. Owned here, not in the
    // editor, because the window is usually shut while the controller is used.
    MidiMap& getMidiMap() { return midiMap; }
    const MidiMap& getMidiMap() const { return midiMap; }

    // Which pad hits which drum, for the slots that have been taught one.
    // Owned here for the same reason the CC map is, and persisted the same way.
    DrumNoteMap& getDrumNoteMap() { return drumNoteMap; }
    const DrumNoteMap& getDrumNoteMap() const { return drumNoteMap; }

    // What MIDI is actually arriving, when anyone asks. Switched on from the
    // Options menu, and the first thing to reach for when a controller that
    // works in the standalone stops working in a host - the two cases it exists
    // to tell apart look identical from anywhere else.
    MidiMonitor& getMidiMonitor() { return midiMonitor; }

    // ---- The host's parameters ---------------------------------------------
    // One per registry entry, so a DAW can automate every dial on the panel and
    // Live's Configure has something to list. Addressed by registry id, which
    // is what everything else here already speaks.
    //
    // NOTE: this is not juce::AudioProcessor::getParameters(). That one is
    // hidden by getParameters() above, which returns the synth's own block and
    // long predates there being a host list at all. The two are different types,
    // so confusing them fails to compile rather than quietly misbehaving.
    HostParameter* getHostParameter(int registryId) const;

    // Pushes every parameter's current value at the host. Called after anything
    // that moves a lot of them at once - loading a preset, undo, restoring a
    // project - because a DAW that was not told still believes its own values
    // and will write them back over the patch you just loaded. Ours change 63
    // things in one go, so this is not an edge case, it is every load.
    void notifyHostOfAllParameters();

    // The host just wrote this parameter. Audio thread or message thread, so
    // it only sets a bit; processBlock turns that into the same hold countdown
    // a learned controller gets.
    void noteHostWroteParameter(int registryId);

    // What actually left the plugin last block, for anything that wants to look
    // at it. Read from the message thread; written by the audio thread.
    OutputTap& getOutputTap() { return outputTap; }

    // The mastering chain's two meters, exposed one at a time rather than by
    // handing out the chain itself. A panel that held a reference to the chain
    // could call process() on it from the message thread, which is a crash
    // waiting for the right afternoon.
    float compressorReductionDb() const override { return masteringChain.getGainReductionDb(); }
    float limiterReductionDb() const override { return masteringChain.getLimiterReductionDb(); }

    // The other half of mastering::Controls: which atomic each named control
    // stands for. Defined in the .cpp beside updateMasteringParameters, which
    // is the only other place that has to know this mapping.
    float get(mastering::Control control) const override;
    void set(mastering::Control control, float value) override;

    // The wavetable set currently driving the oscillators (built-in or sampled).
    const WavetableSet& getActiveWavetables() const;

    // Borrowing and restoring whichever wavetable is live, so previewing a
    // preset can be undone without reloading anything. The sets themselves are
    // owned here for the life of the session, so the pointer stays valid.
    const WavetableSet* getActiveWavetablePointer() const;
    void setActiveWavetablePointer(const WavetableSet* set);

    // Installs a wavetable sampled from audio. The previous set is retained
    // rather than freed, since a voice may still be mid-block reading it.
    bool loadSampledWavetable(const float* data, int numSamples, double sampleRate, float fundamentalHz,
                               const juce::String& sourceName, juce::String& errorMessage);
    bool loadWavetableFromRaw(const std::vector<float>& rawData, const juce::String& sourceName,
                               juce::String& errorMessage);
    void revertToBuiltInWavetables();

    // Copies this synth's mastering dials into the chain's own parameter block,
    // once per block. The chain deliberately does not know what SynthParameters
    // is, so somebody has to carry the values across, and this is the seam where
    // the synth ends and the shared library begins.
    void updateMasteringParameters();

    // The copy itself, from any SynthParameters into any parameter block.
    //
    // Static and public because the processor is no longer its only caller.
    // The Master page's displays run their own private chain over the patch,
    // and they have to hand it exactly what the real chain gets - which one
    // function guarantees and two copies of these twelve lines would not. They
    // cannot simply read the processor's block, either: it is only refreshed
    // while audio is running, and the page is drawn whether it is or not.
    static void fillMasteringParameters(const SynthParameters& from, mastering::Parameters& to);

private:
    // Drains the sequencer's hits and the editor's auditions into the kit, and
    // adds the result to the buffer. The seam where a pattern becomes a sound:
    // the sequencer produces hits and knows no synthesis, the kit produces
    // sound and knows no pattern, and this is the only code that sees both.
    void renderDrums(juce::AudioBuffer<float>& buffer, double bpm);

    // Carries the drum dials across into the kit's own parameter block, once
    // per block.
    //
    // The same seam updateMasteringParameters is, and for the same reason: the
    // kit is a library that does not know what SynthParameters is, so somebody
    // has to copy. It is also the ONE place that converts units - the registry
    // holds Tune in semitones and Pan from left to right because that is what
    // a host should show, and the kit holds both as 0-1.
    //
    // It takes the tempo for one control: the send delay's time is a musical
    // division on the panel and seconds in the kit, and turning one into the
    // other needs a bpm the drums library has no business knowing.
    void updateDrumParameters(double bpm);

    SynthParameters parameters;
    WavetableSet builtInWavetables;
    WavetableProvider wavetableProvider;
    std::vector<std::unique_ptr<WavetableSet>> sampledWavetables;
    juce::Synthesiser synth;
    juce::MidiKeyboardState keyboardState;
    MasterEffects masterEffects;

    // The same chain again, on the Master page: over synth and drums together,
    // after both and before mastering. Its dials are SynthParameters::busFx.
    MasterEffects busEffects;

    // The mastering chain, and the parameter block it reads.
    //
    // Its own struct rather than SynthParameters, because the chain is a
    // library that a second product will link and a library that reached into
    // this synth could only ever be used by this synth. The copy across happens
    // once per block and is the price of that boundary.
    mastering::Chain masteringChain;
    mastering::Parameters masteringParameters;

    // The drum kit, and its own parameter block for the same reason the
    // mastering chain has one: it is a library that knows nothing about this
    // synth, and it is going to stay that way.
    //
    // Its dials are at their defaults and nothing moves them yet. Registering
    // them is phase 5, which is what the cap being raised to 128 was for -
    // sixteen voices times four controls is sixty-four, and there are exactly
    // sixty-five slots free.
    drums::Kit drumKit;
    drums::KitParameters drumParameters;

    // Voices the editor has asked to hear, one bit each, drained at the top of
    // every block.
    //
    // A bitmask and not a call into the kit. Kit::trigger writes a queue the
    // audio thread is reading, so the message thread cannot touch it - and an
    // audition is one voice at full tilt right now, which a single atomic says
    // completely. Nothing to allocate, nothing to lock, and a click that
    // arrives during a block is heard at the start of the next one.
    //
    // Two fields in ONE word: the low twelve bits are which slots were struck,
    // and the high twelve are which of those were accented. In one word and
    // not two atomics, because two would have to be exchanged one after the
    // other and a hit landing between the two exchanges would have its accent
    // read against the next hit instead of its own. One exchange cannot see a
    // half-written pair.
    static constexpr int auditionAccentShift = 16;
    std::atomic<juce::uint32> drumAuditions{0};

    static_assert(project::numDrumVoices <= auditionAccentShift,
                  "The struck and accented halves of drumAuditions would overlap, so an "
                  "accent on a low slot would read as a hit on a high one.");

    // What is loaded into each slot, as the message thread sees it, and the
    // keep-alive behind what the audio thread is reading.
    //
    // What each voice is playing, and what it was playing until recently.
    //
    // A render part way through a hit holds a raw pointer into a sample, so
    // one taken out of a slot cannot be freed on the spot. It used to be kept
    // for the life of the plugin instead - a fair bargain while loading a
    // sample was a deliberate act, and the wrong one now that browsing a
    // folder auditions a file per keypress. drums::RetiringStore keeps each
    // displaced one for longer than any hit could still be running, which is
    // the same safety at a bounded cost.
    //
    // The live bank is held here because the store must not be the only owner
    // of the one the audio thread is reading. The banks are sixteen pointers
    // apiece, so their history is measured in kilobytes either way.
    std::array<drums::SampleRef, project::numDrumVoices> drumSlots;
    std::unique_ptr<drums::SampleBank> liveBank;
    drums::RetiringStore<drums::SampleRef> retiringSamples;
    drums::RetiringStore<std::unique_ptr<drums::SampleBank>> retiringBanks;

    void publishSampleBank();

    Arpeggiator arpeggiator;
    HarmonyProcessor harmony;
    NoteLatch latch;
    MonoVoice monoVoice;
    StepSequencer sequencer;
    TempoClock tempoClock;
    MidiMap midiMap;
    DrumNoteMap drumNoteMap;
    MidiMonitor midiMonitor;

    // The host's parameter objects, by registry id. Owned by the base class
    // once addParameter has taken them; these are borrowed pointers kept so a
    // lookup by id is an index rather than a search.
    std::array<HostParameter*, paramreg::maxParameters> hostParameters{};

    // Parameters the host has written since the last block, one bit each.
    // Written from whichever thread the host chose, drained on the audio thread,
    // which is why it is a mask and not the countdown itself.
    AtomicParameterMask hostWroteParameters;

    // Which factory preset the host believes is loaded, and a count of the
    // times that has changed. Atomics because setCurrentProgram is a host call
    // and the host picks the thread: JUCE exposes programs to VST3 as an
    // automatable parameter, so a program change can arrive in a block.
    std::atomic<int> currentProgram{0};
    std::atomic<uint32_t> programChanges{0};

    std::atomic<double> effectiveBpm{120.0};
    std::atomic<bool> tempoIsExternal{false};

    OutputTap outputTap;

    UndoHistory history;
    bool recoveryAvailable = false;

    // Audio thread only: the channel filter in force last block, so a change to
    // it can let go of notes whose releases can no longer reach us.
    int lastNotesFromChannel = 0;

    // Audio thread only: how much longer each parameter counts as being driven
    // by a learned controller. A control sends no "let go" message - a knob you
    // stop turning simply stops sending - so being driven has to be a countdown
    // rather than a state something switches off.
    //
    // Refilled by every controller message and run down by each block, which is
    // what lets REC capture a knob sweep and what stops a lane fighting one.
    std::array<int, paramreg::maxParameters> controllerHoldSamples{};

    // Matched to how long a dial stays lit after the mouse lets go, so a sweep
    // from hardware and a sweep from the mouse are held for the same moment.
    static constexpr double controllerHoldSeconds = 0.9;
    void timerCallback() override;

    // Telling the host about sixty-three parameters means touching sixty-three
    // parameter objects, which is message-thread work. A program change may not
    // arrive on that thread, so the telling is posted rather than done in place.
    void handleAsyncUpdate() override;

    // Whether the crash-recovery file is ours to touch.
    //
    // Only in the standalone. It exists because there is no host to save the
    // session, which is precisely the job a host does - so in a plugin it is
    // redundant. Worse than redundant: the path is one fixed file in the temp
    // directory, so two instances on two tracks would autosave over each other
    // every five minutes, and closing either would delete the file the other
    // was relying on. Then the next standalone launch would offer to recover
    // whichever plugin instance happened to write last.
    bool ownsRecoveryFile() const { return wrapperType == wrapperType_Standalone; }

    // The one musical timeline the arpeggiator and the sequencer both read
    // their positions from, so a linked rate means the same instants rather
    // than two clocks that merely agree on a number.
    tempo::MusicalClock musicalClock;
    bool sequencerWasPlaying = false;

    static constexpr int numVoices = 16;
};
} // namespace wavelathe
