// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "AppSettings.h"
#include "FactoryPresets.h"

namespace wavelathe
{
WaveLatheProcessor::WaveLatheProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true))
{
    wavetableProvider.active.store(&builtInWavetables, std::memory_order_release);

    synth.addSound(new SynthSound());
    for (int i = 0; i < numVoices; ++i)
        synth.addVoice(new SynthVoice(wavetableProvider, parameters));

    // A recovery file still sitting there means the last run did not reach its
    // own shutdown, so there is work in it that nothing else saved.
    recoveryAvailable = ownsRecoveryFile() && getRecoveryFile().existsAsFile();

    // The map describes the hardware on the desk, so it comes back with the
    // application rather than with a patch. Loaded here and not in the editor
    // because a plugin usually runs with its window shut, and assignments that
    // only existed while the window was open would not be assignments.
    midiMap.fromString(AppSettings::getInstance().getMidiMap());
    drumNoteMap.fromString(AppSettings::getInstance().getDrumNoteMap());
    parameters.keyboardChannel = (float) AppSettings::getInstance().getKeyboardChannel();
    parameters.drumChannel = (float) AppSettings::getInstance().getDrumChannel();
    parameters.drumTranspose = (float) AppSettings::getInstance().getDrumTranspose();
    lastNotesFromChannel = (int) parameters.keyboardChannel.load();

    // Every registry entry becomes a parameter the host can see, automate and
    // MIDI-map. Nothing about the engine changes: these are windows onto the
    // atomics it already reads, added here rather than in prepareToPlay because
    // a host reads the list once, when it loads the plugin.
    for (int id = 0; id < paramreg::count(); ++id)
    {
        auto parameter = std::make_unique<HostParameter>(
            id, parameters, [this](int movedId) { noteHostWroteParameter(movedId); });
        hostParameters[(size_t) id] = parameter.get();
        addParameter(parameter.release()); // the base class owns it from here
    }

    if (ownsRecoveryFile())
        startTimer(5 * 60 * 1000);
}

WaveLatheProcessor::~WaveLatheProcessor()
{
    stopTimer();

    // Reaching here at all means the shutdown was orderly, and the session has
    // been stored the usual way - so the recovery copy has nothing to offer and
    // its absence is what tells the next run that all was well.
    if (ownsRecoveryFile())
        getRecoveryFile().deleteFile();
}

const WavetableSet& WaveLatheProcessor::getActiveWavetables() const
{
    auto* active = wavetableProvider.active.load(std::memory_order_acquire);
    return active != nullptr ? *active : builtInWavetables;
}

bool WaveLatheProcessor::loadSampledWavetable(const float* data, int numSamples, double sampleRate,
                                               float fundamentalHz, const juce::String& sourceName,
                                               juce::String& errorMessage)
{
    auto newSet = std::make_unique<WavetableSet>();
    if (!newSet->loadFromAudio(data, numSamples, sampleRate, fundamentalHz, errorMessage))
        return false;

    newSet->setSourceName(sourceName);

    auto* rawPointer = newSet.get();
    sampledWavetables.push_back(std::move(newSet));
    wavetableProvider.active.store(rawPointer, std::memory_order_release);
    return true;
}
// ---- Undo ------------------------------------------------------------------
void WaveLatheProcessor::recordUndoPoint(const juce::String& action)
{
    history.record(captureProject(action, false), getActiveWavetablePointer(), action);
}

bool WaveLatheProcessor::undo(PresetValues& outValues)
{
    UndoHistory::Snapshot snapshot;
    if (!history.undo(captureProject("Now", false), getActiveWavetablePointer(), snapshot))
        return false;

    setActiveWavetablePointer(snapshot.wavetable);
    outValues = snapshot.state;
    return true;
}

bool WaveLatheProcessor::redo(PresetValues& outValues)
{
    UndoHistory::Snapshot snapshot;
    if (!history.redo(captureProject("Now", false), getActiveWavetablePointer(), snapshot))
        return false;

    setActiveWavetablePointer(snapshot.wavetable);
    outValues = snapshot.state;
    return true;
}

// ---- Recovery --------------------------------------------------------------
juce::File WaveLatheProcessor::getRecoveryFile()
{
    return juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("WaveLathe")
        .getChildFile(juce::String("recovery.") + project::fileExtension);
}

void WaveLatheProcessor::saveRecoveryNow()
{
    // A plugin has a host to save its session, and one shared temp file that
    // every instance would fight over. Nothing to do.
    if (!ownsRecoveryFile())
        return;

    auto file = getRecoveryFile();
    file.getParentDirectory().createDirectory();

    // Written beside the real one and moved into place, so a crash caught
    // mid-write leaves the previous good copy rather than half a file.
    auto scratch = file.getSiblingFile(file.getFileName() + ".part");

    juce::String error;
    if (!FxpPreset::save(scratch, captureProject("Recovered project"), error))
        return;

    file.deleteFile();
    scratch.moveFileTo(file);
}

bool WaveLatheProcessor::loadRecoveredProject(PresetValues& outValues, juce::String& errorMessage)
{
    return FxpPreset::load(getRecoveryFile(), outValues, errorMessage);
}

void WaveLatheProcessor::discardRecovery()
{
    recoveryAvailable = false;
    if (ownsRecoveryFile())
        getRecoveryFile().deleteFile();
}

void WaveLatheProcessor::timerCallback()
{
    saveRecoveryNow();
}

int WaveLatheProcessor::getNumPrograms()
{
    // Never zero. A host divides by the step count to place the program dial,
    // and JUCE builds that count from this number minus one.
    return juce::jmax(1, (int) FactoryPresets::all().size());
}

const juce::String WaveLatheProcessor::getProgramName(int index)
{
    const auto& bank = FactoryPresets::all();

    if (! juce::isPositiveAndBelow(index, (int) bank.size()))
        return {};

    const auto& entry = bank[(size_t) index];

    // Category first, because a host shows one flat list with no room for the
    // browser's grouping. The bank is already in category order, so the prefix
    // does not sort anything - it labels the runs that are there anyway, and
    // turns sixty-five names into seven readable groups.
    return juce::String(FactoryPresets::categoryName((int) entry.category)) + ": " + entry.name;
}

void WaveLatheProcessor::setCurrentProgram(int index)
{
    const auto& bank = FactoryPresets::all();

    if (! juce::isPositiveAndBelow(index, (int) bank.size()))
        return;

    currentProgram = index;

    // The sound, and only the sound. No pattern: choosing a preset is not the
    // same act as replacing the bar somebody is writing, which is the line the
    // editor has drawn since 0.24.0 and which the host's way in honours too.
    // No tempo either - in a host the transport belongs to the host, and a
    // preset that moved it would be a preset nobody would dare audition.
    //
    // Every write below lands in an atomic, so this is safe from whichever
    // thread the host chose. VST3 carries programs as an automatable
    // parameter, so one can arrive inside a block.
    FxpPreset::applyToSynthParameters(bank[(size_t) index].values, parameters,
                                      FxpPreset::ApplyScope::synthPreset);

    ++programChanges;

    // The host still believes its own values for all sixty-three and would
    // write them back over the patch just loaded the next time it touched one.
    // The same failure 0.36.2 fixed for the editor's preset loads, arriving
    // through a different door. Posted rather than done here: telling the host
    // is message-thread work and this call may not be on it.
    triggerAsyncUpdate();
}

void WaveLatheProcessor::handleAsyncUpdate()
{
    notifyHostOfAllParameters();
}


bool WaveLatheProcessor::loadWavetableFromRaw(const std::vector<float>& rawData, const juce::String& sourceName,
                                               juce::String& errorMessage)
{
    auto newSet = std::make_unique<WavetableSet>();
    if (!newSet->loadFromRawTables(rawData, errorMessage))
        return false;

    newSet->setSourceName(sourceName);

    auto* rawPointer = newSet.get();
    sampledWavetables.push_back(std::move(newSet));
    wavetableProvider.active.store(rawPointer, std::memory_order_release);
    return true;
}

PresetValues WaveLatheProcessor::captureProject(const juce::String& name, bool includeWavetable) const
{
    auto values = FxpPreset::fromSynthParameters(parameters, name);
    values.sequencer = sequencer.captureState();
    values.drumSamples = drumSlots;

    for (int voice = 0; voice < project::numDrumVoices; ++voice)
        values.drumEngines[(size_t) voice] = getDrumEngine(voice);

    values.midiMap = midiMap.toString();
    values.keyboardChannel = juce::jlimit(0, 16, (int) std::round(parameters.keyboardChannel.load()));

    // A sampled wavetable is part of the project too: without it the pattern
    // would come back playing the wrong waveform.
    const auto& active = getActiveWavetables();
    if (includeWavetable && active.isCustom())
        values.wavetableData = active.getRawTables();

    return values;
}

void WaveLatheProcessor::restoreDrumSamples(const PresetValues& values)
{
    // The sample slots, on the same footing as the pattern: they are part of
    // the project rather than of the patch, and a state that arrived with none
    // of them clears whatever was loaded - otherwise opening a second project
    // would leave the first one's kick in place.
    //
    // Its own function because there are TWO paths that put a whole state back
    // and only one of them used to do this. Opening a project restored the
    // slots; undo, redo and crash recovery went through the editor, which
    // rebuilt the parameters and the pattern by hand and never touched them -
    // so undoing a sample load said "Undid kick sample" on the display and
    // left the sample exactly where it was. It went unnoticed while loading a
    // sample was a deliberate act several menus deep, and became easy to hit
    // the moment a browser could load one per keypress.
    for (int voice = 0; voice < project::numDrumVoices; ++voice)
    {
        setDrumSample(voice, values.drumSamples[(size_t) voice]);
        setDrumEngine(voice, values.drumEngines[(size_t) voice]);
    }
}

void WaveLatheProcessor::applyProject(const PresetValues& values)
{
    FxpPreset::applyToSynthParameters(values, parameters);

    if (!values.wavetableData.empty())
    {
        juce::String error;
        loadWavetableFromRaw(values.wavetableData, "Saved project", error);
    }

    sequencer.restoreState(values.sequencer);

    restoreDrumSamples(values);

    // A project is the session, so it brings its own map and the channel that
    // map was used on. Both are left alone when absent: a project saved before
    // v14 - or one saved before anything was learned - should not wipe the
    // assignments on the desk in front of you, and one saved before v15 knows
    // nothing about channels and must not widen a keyboard someone narrowed.
    if (values.midiMap.isNotEmpty())
    {
        midiMap.fromString(values.midiMap);
        AppSettings::getInstance().setMidiMap(values.midiMap);
    }

    if (values.keyboardChannel >= 0)
    {
        parameters.keyboardChannel = (float) values.keyboardChannel;
        AppSettings::getInstance().setKeyboardChannel(values.keyboardChannel);
    }

    // The host's own route in - a session being restored, or a project loaded.
    // NOT every path that loads a patch, which is what this comment claimed
    // when it was written and was simply false: five more live in the editor
    // and reach the parameters directly, and they tell the host through
    // adoptLoadedPatch instead. A DAW that was not told still believes its own
    // values for all sixty-three and writes them back over the patch just
    // loaded the next time it touches one.
    notifyHostOfAllParameters();
}

void WaveLatheProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    FxpPreset::writeToMemory(captureProject("Session"), destination);
}

void WaveLatheProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    PresetValues values;
    juce::String error;

    // A session that cannot be read is not worth failing over: the synth comes
    // up at its defaults rather than not at all.
    if (FxpPreset::readFromMemory(data, sizeInBytes, values, error))
        applyProject(values);
}

const WavetableSet* WaveLatheProcessor::getActiveWavetablePointer() const
{
    return wavetableProvider.active.load(std::memory_order_acquire);
}

void WaveLatheProcessor::setActiveWavetablePointer(const WavetableSet* set)
{
    wavetableProvider.active.store(set != nullptr ? set : &builtInWavetables, std::memory_order_release);
}

void WaveLatheProcessor::revertToBuiltInWavetables()
{
    wavetableProvider.active.store(&builtInWavetables, std::memory_order_release);
}

float WaveLatheProcessor::get(mastering::Control control) const
{
    using C = mastering::Control;

    switch (control)
    {
        case C::enabled:       return parameters.masteringEnabled.load();
        case C::satDrive:      return parameters.masterSatDrive.load();
        case C::satMix:        return parameters.masterSatMix.load();
        case C::compThreshold: return parameters.masterCompThresholdDb.load();
        case C::compRatio:     return parameters.masterCompRatio.load();
        case C::compAttack:    return parameters.masterCompAttackMs.load();
        case C::compRelease:   return parameters.masterCompReleaseMs.load();
        case C::compMakeup:    return parameters.masterCompMakeupDb.load();
        case C::compMix:       return parameters.masterCompMix.load();
        case C::limitCeiling:  return parameters.masterLimitCeilingDb.load();
        case C::limitRelease:  return parameters.masterLimitReleaseMs.load();
        case C::trim:          return parameters.masterFxGainDb.load();
        case C::count:         break;
    }

    // Every case above returns, and count is not a control. Listing them all
    // rather than writing a default means adding one to the enum stops
    // compiling here, which is the only place that would otherwise go quietly
    // on returning zero for a dial somebody had just added.
    return 0.0f;
}

void WaveLatheProcessor::set(mastering::Control control, float value)
{
    using C = mastering::Control;

    switch (control)
    {
        case C::enabled:       parameters.masteringEnabled = value; break;
        case C::satDrive:      parameters.masterSatDrive = value; break;
        case C::satMix:        parameters.masterSatMix = value; break;
        case C::compThreshold: parameters.masterCompThresholdDb = value; break;
        case C::compRatio:     parameters.masterCompRatio = value; break;
        case C::compAttack:    parameters.masterCompAttackMs = value; break;
        case C::compRelease:   parameters.masterCompReleaseMs = value; break;
        case C::compMakeup:    parameters.masterCompMakeupDb = value; break;
        case C::compMix:       parameters.masterCompMix = value; break;
        case C::limitCeiling:  parameters.masterLimitCeilingDb = value; break;
        case C::limitRelease:  parameters.masterLimitReleaseMs = value; break;
        case C::trim:          parameters.masterFxGainDb = value; break;
        case C::count:         break;
    }
}

void WaveLatheProcessor::updateMasteringParameters()
{
    fillMasteringParameters(parameters, masteringParameters);
}

void WaveLatheProcessor::fillMasteringParameters(const SynthParameters& from, mastering::Parameters& to)
{
    // Enabled here, bypassed there. The panel asks "is this on", the chain asks
    // "should I get out of the way", and the inversion belongs at the seam
    // rather than in either of them - a library whose off switch is called
    // Enabled would read backwards in the effect plugin that links it next.
    to.bypass.store(from.masteringEnabled.load() != 0.0f ? 0.0f : 1.0f);

    to.saturationDrive.store(from.masterSatDrive.load());
    to.saturationMix.store(from.masterSatMix.load());

    to.compressorThresholdDb.store(from.masterCompThresholdDb.load());
    to.compressorRatio.store(from.masterCompRatio.load());
    to.compressorAttackMs.store(from.masterCompAttackMs.load());
    to.compressorReleaseMs.store(from.masterCompReleaseMs.load());
    to.compressorMakeupDb.store(from.masterCompMakeupDb.load());
    to.compressorMix.store(from.masterCompMix.load());

    to.limiterCeilingDb.store(from.masterLimitCeilingDb.load());
    to.limiterReleaseMs.store(from.masterLimitReleaseMs.load());

    to.outputGainDb.store(from.masterFxGainDb.load());
}

void WaveLatheProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    synth.setCurrentPlaybackSampleRate(sampleRate);

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
            voice->prepare(sampleRate, samplesPerBlock);

    masterEffects.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    busEffects.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());
    masteringChain.prepare(sampleRate, samplesPerBlock, getTotalNumOutputChannels());

    // The chain oversamples and its limiter looks ahead, and both cost time. A
    // host that is not told drifts against everything else on the timeline -
    // silently, and the sequencer would get the blame for a lateness it did not
    // cause. Reported here rather than in the constructor because the number
    // depends on the sample rate, which is not known until now.
    //
    // Reported whether or not the mastering stage is switched on. Latency that
    // appeared when a control moved would make the host resynchronise in the
    // middle of a performance.
    setLatencySamples(masteringChain.getLatencySamples());

    arpeggiator.prepare(sampleRate);
    harmony.reset();
    latch.reset();
    monoVoice.reset();
    sequencer.prepare(sampleRate);
    drumKit.prepare(sampleRate, samplesPerBlock);
    tempoClock.prepare(sampleRate);
    musicalClock.prepare(sampleRate);
    outputTap.prepare(sampleRate);
}

void WaveLatheProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();

    // Before anything of ours touches the buffer, so what the readout reports
    // is what the HOST delivered rather than what survived our own filtering.
    // That matters here more than it looks: a keyboard narrowed to one channel
    // drops notes a few lines below, and a probe that could not see them would
    // report "Live sends nothing" for a setting of ours.
    //
    // It also means the keys on screen and the letter keys do NOT appear here:
    // those are merged in from the keyboard state further down, and they are
    // not something a host sent. Correct, and the first thing anyone tries - so
    // the readout says so when the monitor is switched on.
    //
    // Guarded once for the whole block rather than once per message, so a
    // monitor nobody switched on costs a single load.
    if (midiMonitor.isEnabled())
        for (const auto metadata : midiMessages)
            midiMonitor.capture(metadata.getMessage());

    // Tempo first: the arpeggiator, delay and LFOs all sync to it.
    tempoClock.processMidi(midiMessages, buffer.getNumSamples());
    double bpm = tempoClock.getBpm(parameters);
    bool externalTempo = tempoClock.getExternalClockBpm(parameters).has_value();

    // And the host outranks both, because in a DAW the transport is not ours to
    // have an opinion about. Without this the BPM dial governed the arpeggiator,
    // the sequencer, the synced delay and the synced LFOs while the song ran at
    // its own speed - so everything that syncs was in time with nothing. It
    // only shows in a host: the standalone has no playhead and never did.
    //
    // Above MIDI clock deliberately. Clock is how hardware drives the
    // standalone; a plugin is already being driven by the thing it sits inside.
    // Named hostPlayHead rather than playHead because AudioProcessor already
    // has a member of that name, and a local that hides a base member is a
    // /W4 warning (C4458). Harmless here - getPlayHead() returns that very
    // member - but one warning that is always there is how the next real one
    // goes unread.
    if (auto* hostPlayHead = getPlayHead())
    {
        if (auto position = hostPlayHead->getPosition())
        {
            if (auto hostBpm = position->getBpm())
            {
                if (*hostBpm > 0.0)
                {
                    bpm = *hostBpm;
                    externalTempo = true;
                }
            }
        }
    }

    // For the panel, which cannot ask the audio thread anything.
    effectiveBpm = bpm;
    tempoIsExternal = externalTempo;

    // Pressing play rewinds the shared clock, so a pattern always starts at
    // the top and every take is recorded against the same grid.
    bool sequencerPlaying = parameters.seqPlaying.load() > 0.5f;
    if (sequencerPlaying && !sequencerWasPlaying)
        musicalClock.reset();
    sequencerWasPlaying = sequencerPlaying;

    musicalClock.beginBlock(bpm, buffer.getNumSamples());

    const int notesFromChannel = juce::jlimit(0, 16, (int) std::round(parameters.keyboardChannel.load()));

    // Anything being held arrived on the old channel, and its release will
    // arrive on the old channel too - where the filter below would now drop it,
    // leaving the note sounding for good. So the change itself lets go of
    // everything rather than trusting a release that can no longer get through.
    if (notesFromChannel != lastNotesFromChannel)
    {
        lastNotesFromChannel = notesFromChannel;
        keyboardState.allNotesOff(0);
        synth.allNotesOff(0, true);
    }

    // Before anything reads the buffer - the keyboard state, the latch, the
    // harmony, the arpeggiator - so a dropped note is dropped everywhere rather
    // than leaving one of them holding a key nothing will ever release.
    //
    // The kit's notes come out here too, in the same pass and for the same
    // reason: taken any later and the arpeggiator would already have rewritten
    // the buffer out from under them.
    const int drumsFromChannel = juce::jlimit(0, 16, (int) std::round(parameters.drumChannel.load()));
    const auto struckDrums = splitIncomingNotes(midiMessages, drumsFromChannel, notesFromChannel);

    const int drumShift = juce::jlimit(-project::maxDrumTranspose, project::maxDrumTranspose,
                                       (int) std::round(parameters.drumTranspose.load()));

    for (const auto& hit : struckDrums)
    {
        // An armed learn eats the note and binds the slot to it. Before the
        // lookup, so the pad being taught does not also fire whatever it
        // currently strikes - which would be the loudest thing in the room at
        // the exact moment you are listening for whether the assignment took.
        if (drumNoteMap.captureLearn(hit.note))
            continue;

        const auto voice = drumNoteMap.voiceForNote(hit.note, drumShift);

        if (voice < 0)
            continue;

        // With the sample offset, so a pad played against the grid lands where
        // it was hit rather than at the top of the block. The sequencer's own
        // hits have had that since they existed and a played one deserves it
        // more: at a 512-sample buffer, the top of the block is up to twelve
        // milliseconds early, which is audible on a hat.
        drumKit.trigger(voice, hit.velocity, hit.velocity >= project::drumAccentVelocity,
                        hit.sampleOffset);
    }

    keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);

    // The wheel and key pressure are read here, before anything downstream
    // rewrites the buffer - the arpeggiator replaces what it is handed, so a
    // controller message read any later would be gone.
    for (const auto metadata : midiMessages)
    {
        auto message = metadata.getMessage();

        if (message.isController())
        {
            // Learned assignments are applied first and for every controller,
            // including CC 1. A wheel learned to a dial drives that dial AND
            // keeps its modulation duty below, because the two are different
            // jobs: one sets a parameter, the other is a live modulation
            // source that the Whl> dials scale.
            const int controller = message.getControllerNumber();

            if (midiMap.handleController(controller, message.getControllerValue(), parameters))
            {
                // Whatever it moved counts as held from here, so REC captures a
                // knob sweep the same way it captures a mouse one. Read after
                // the call, not before: on the message that BINDS an assignment
                // the parameter was not yet pointed at by this controller.
                const int id = midiMap.parameterFor(controller);
                if (id >= 0 && id < (int) controllerHoldSamples.size())
                    controllerHoldSamples[(size_t) id] =
                        (int) (controllerHoldSeconds * getSampleRate());
            }
        }

        if (message.isControllerOfType(1))
            parameters.modWheel = (float) message.getControllerValue() / 127.0f;
        else if (message.isChannelPressure())
            parameters.channelPressure = (float) message.getChannelPressureValue() / 127.0f;
        else if (message.isAftertouch())
            parameters.channelPressure = (float) message.getAfterTouchValue() / 127.0f;
    }


    // Anything the host wrote since the last block gets the same hold a learned
    // controller gets, by the same countdown below. Drained here rather than
    // set directly by the parameter object, because that is called from
    // whichever thread the DAW chose and the countdown is audio-thread-only -
    // and because a bit set without a countdown to run it down would never
    // clear, leaving the lane permanently locked out of that dial.
    if (const ParameterMask hostWrites = hostWroteParameters.exchangeZero(); hostWrites.any())
    {
        const int holdSamples = (int) (controllerHoldSeconds * getSampleRate());

        for (int id = 0; id < (int) controllerHoldSamples.size(); ++id)
            if (hostWrites.test(id))
                controllerHoldSamples[(size_t) id] = holdSamples;
    }

    // Run the controller holds down. Done after the controller loop has
    // refilled them, and before the sequencer records, so a knob still being
    // turned never falls out of the held set between the two.
    for (int id = 0; id < (int) controllerHoldSamples.size(); ++id)
    {
        int& remaining = controllerHoldSamples[(size_t) id];
        if (remaining <= 0)
            continue;

        remaining -= buffer.getNumSamples();

        if (remaining <= 0)
        {
            remaining = 0;
            sequencer.setParameterDrivenByController(id, false);
        }
        else
        {
            sequencer.setParameterDrivenByController(id, true);
        }
    }

    // Latch before anything else reads the notes, so everything downstream -
    // harmony, the arpeggiator - sees a held chord exactly as if fingers were
    // still on the keys.
    latch.process(midiMessages, parameters);

    // Harmony next: pull what was played into the key and open it into a
    // chord, so the arpeggiator downstream has real chord tones to pattern.
    harmony.process(midiMessages, parameters);

    // Sits between the keyboard and the voices: when on, the pattern it
    // generates replaces the notes being held.
    arpeggiator.process(midiMessages, buffer.getNumSamples(), parameters, musicalClock, bpm);

    // The sequencer is a second player rather than a filter on the first: its
    // line is added to whatever you are holding, and it moves the automation
    // lanes on as it goes.
    sequencer.process(midiMessages, buffer.getNumSamples(), parameters, musicalClock);

    // Last before the voices, so everything upstream - what you played, the
    // chord, the arpeggiator, the sequencer - is narrowed to a single line
    // when mono is on, rather than each of them having to know about it.
    monoVoice.process(midiMessages, parameters);

    for (int i = 0; i < synth.getNumVoices(); ++i)
        if (auto* voice = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
            voice->setBpm(bpm);

    synth.renderNextBlock(buffer, midiMessages, 0, buffer.getNumSamples());
    masterEffects.process(buffer, parameters, bpm);

    // The drums, AFTER the synth's own effects and before the mastering chain.
    //
    // Both halves of that are deliberate. The synth's delay and reverb belong
    // to the synth - a beat washed through the pad's reverb is not a mix
    // decision anybody made, and the drums get sends of their own in phase 5.
    // The mastering chain, on the other hand, is the mix bus: it has to catch
    // everything, or the limiter is holding a ceiling that the kit walks
    // straight through.
    renderDrums(buffer, bpm);

    // The Master page's bus effects: the first thing that hears synth and drums
    // TOGETHER, which is the whole reason to have a second copy of the chain.
    // Before mastering rather than after, so a reverb's tail and a delay's
    // repeats go through the limiter along with everything else instead of
    // arriving after it, uncaught.
    busEffects.process(buffer, busEffectSettingsFrom(parameters), bpm);

    updateMasteringParameters();
    masteringChain.process(buffer, masteringParameters);

    // The very last thing, so what is captured is what the interface receives
    // rather than a version of it from before the effects and the output gain -
    // which is exactly the difference a meter exists to show.
    outputTap.push(buffer);
}

// ---- Sample slots ----------------------------------------------------------
void WaveLatheProcessor::publishSampleBank()
{
    // A whole new bank each time rather than editing the one in use. The
    // audio thread holds the old one until its next read, and mutating a slot
    // under it would be a voice deciding mid-hit that it is now a different
    // drum.
    auto bank = std::make_unique<drums::SampleBank>();
    bool anyLoaded = false;

    for (int voice = 0; voice < project::numDrumVoices; ++voice)
    {
        const auto& slot = drumSlots[(size_t) voice];

        if (slot != nullptr && !slot->isEmpty())
        {
            bank->slots[(size_t) voice] = slot.get();
            anyLoaded = true;
        }
    }

    // An empty bank is published as no bank at all, so a kit with nothing
    // loaded costs the render path one null check rather than sixteen.
    auto* published = anyLoaded ? bank.get() : nullptr;

    // The outgoing bank goes into its grace period rather than being dropped
    // here: the audio thread may not have read the new pointer yet.
    if (liveBank != nullptr)
        retiringBanks.retire(std::move(liveBank), juce::Time::getMillisecondCounter());

    liveBank = std::move(bank);
    drumKit.setSampleBank(published);
}

void WaveLatheProcessor::setDrumSample(int voice, drums::SampleRef sample)
{
    if (voice < 0 || voice >= project::numDrumVoices)
        return;

    auto& slot = drumSlots[(size_t) voice];

    // Whatever was on the voice starts its grace period now. Kept even when
    // something else still owns it - an undo snapshot or a project being
    // saved - because the shared_ptr makes that free, and working out which
    // case this is would cost more than holding it.
    if (slot != nullptr && slot != sample)
        retiringSamples.retire(slot, juce::Time::getMillisecondCounter());

    slot = std::move(sample);
    publishSampleBank();
}

void WaveLatheProcessor::clearDrumSample(int voice)
{
    setDrumSample(voice, nullptr);
}

bool WaveLatheProcessor::loadDrumSample(int voice, const juce::File& file, juce::String& error)
{
    if (voice < 0 || voice >= project::numDrumVoices)
    {
        error = "There is no voice " + juce::String(voice) + ".";
        return false;
    }

    auto sample = drumsamples::load(file, error);

    if (sample == nullptr)
        return false;

    setDrumSample(voice, std::move(sample));
    return true;
}

drums::SampleRef WaveLatheProcessor::getDrumSample(int voice) const
{
    if (voice < 0 || voice >= project::numDrumVoices)
        return nullptr;

    return drumSlots[(size_t) voice];
}

juce::String WaveLatheProcessor::getDrumSampleName(int voice) const
{
    const auto slot = getDrumSample(voice);
    return slot != nullptr ? slot->name : juce::String();
}

void WaveLatheProcessor::setDrumEngine(int voice, int engine)
{
    if (voice < 0 || voice >= project::numDrumVoices)
        return;

    const auto valid = (engine >= 0 && engine < drums::numEngines) ? engine : drums::noEngine;
    drumParameters.voices[(size_t) voice].engine.store(valid);
}

int WaveLatheProcessor::getDrumEngine(int voice) const
{
    if (voice < 0 || voice >= project::numDrumVoices)
        return drums::noEngine;

    return drumParameters.voices[(size_t) voice].engine.load();
}

juce::String WaveLatheProcessor::getDrumSlotDescription(int voice) const
{
    // The sample wins when there is one, because that is what the slot is
    // actually playing - the engine underneath it is what it would go back to,
    // not what is sounding.
    const auto sample = getDrumSampleName(voice);

    if (sample.isNotEmpty())
        return sample;

    const auto engine = getDrumEngine(voice);

    if (engine == drums::noEngine)
        return "Empty";

    return drums::engineName(engine);
}

void WaveLatheProcessor::auditionDrumVoice(int voice, bool accent)
{
    if (voice < 0 || voice >= drums::numVoices)
        return;

    auto bits = juce::uint32(1) << voice;

    if (accent)
        bits |= juce::uint32(1) << (voice + auditionAccentShift);

    drumAuditions.fetch_or(bits);
}

void WaveLatheProcessor::updateDrumParameters(double bpm)
{
    using DC = project::DrumControl;
    using FX = project::DrumFxControl;

    const auto read = [this](int voice, DC control)
    {
        return parameters.drumControls[(size_t) project::drumControlIndex(voice, control)].load();
    };

    for (int voice = 0; voice < drums::numVoices; ++voice)
    {
        auto& target = drumParameters.voices[(size_t) voice];

        target.level.store(juce::jlimit(0.0f, 1.0f, read(voice, DC::level)));
        target.attack.store(juce::jlimit(0.0f, 1.0f, read(voice, DC::attack)));
        target.decay.store(juce::jlimit(0.0f, 1.0f, read(voice, DC::decay)));

        // Semitones to the kit's 0-1, where 0.5 is the voice's own pitch and
        // the ends are an octave either way. The registry's range is -12 to
        // +12 for the host's sake; this is the only place the two meet.
        target.tune.store(juce::jlimit(0.0f, 1.0f, read(voice, DC::tune) / 24.0f + 0.5f));

        // Left-to-right to the kit's 0-1, same idea.
        target.pan.store(juce::jlimit(0.0f, 1.0f, read(voice, DC::pan) * 0.5f + 0.5f));

        // A slot's unit is a choice wearing a dial, exactly as the filter type
        // is: the value is rounded HERE, once a block, rather than sixteen
        // times a block inside the kit. 2.4 was never anything but "the
        // distortion".
        const auto unitOf = [&read, voice](DC control)
        {
            return juce::jlimit(0, drums::numFxUnits,
                                juce::roundToInt(read(voice, control)));
        };

        target.send1.store(unitOf(DC::send1));
        target.send2.store(unitOf(DC::send2));

        target.send1Amount.store(juce::jlimit(0.0f, 1.0f, read(voice, DC::send1Amount)));
        target.send2Amount.store(juce::jlimit(0.0f, 1.0f, read(voice, DC::send2Amount)));
    }

    const auto fxValue = [this](FX control)
    {
        return parameters.drumFx[(size_t) control].load();
    };

    auto& fx = drumParameters.fx;

    fx.reverbSize.store(juce::jlimit(0.0f, 1.0f, fxValue(FX::reverbSize)));
    fx.reverbDamp.store(juce::jlimit(0.0f, 1.0f, fxValue(FX::reverbDamp)));
    fx.reverbLevel.store(juce::jlimit(0.0f, 1.0f, fxValue(FX::reverbMix)));

    fx.driveAmount.store(juce::jlimit(0.0f, 1.0f, fxValue(FX::driveAmount)));
    fx.driveTone.store(juce::jlimit(0.0f, 1.0f, fxValue(FX::driveTone)));
    fx.driveLevel.store(juce::jlimit(0.0f, 1.0f, fxValue(FX::driveMix)));

    // Decibels on the panel, a multiplier in the kit. The same split as
    // everything else here: a person reads "+4 dB" and the DSP multiplies.
    fx.eqLowGain.store(juce::Decibels::decibelsToGain(juce::jlimit(-12.0f, 12.0f, fxValue(FX::eqLow))));
    fx.eqHighGain.store(juce::Decibels::decibelsToGain(juce::jlimit(-12.0f, 12.0f, fxValue(FX::eqHigh))));
    fx.eqLevel.store(juce::jlimit(0.0f, 1.0f, fxValue(FX::eqLevel)));

    // A division index into seconds, which is the one conversion here that
    // needs to know the tempo. A drum echo that is not in time is a different
    // effect from the one anybody was reaching for.
    const auto division = juce::jlimit(tempo::nearestDivisionForBeats(project::drumDelaySlowestBeats),
                                       tempo::getNumDivisions() - 1,
                                       juce::roundToInt(fxValue(FX::delayTime)));

    fx.delaySeconds.store((float) (tempo::divisionToMs(division, bpm) / 1000.0));
    fx.delayFeedback.store(juce::jlimit(0.0f, 0.95f, fxValue(FX::delayFeedback)));
    fx.delayLevel.store(juce::jlimit(0.0f, 1.0f, fxValue(FX::delayMix)));
}

void WaveLatheProcessor::renderDrums(juce::AudioBuffer<float>& buffer, double bpm)
{
    updateDrumParameters(bpm);

    for (int i = 0; i < sequencer.getNumDrumHits(); ++i)
    {
        const auto hit = sequencer.getDrumHit(i);
        drumKit.trigger(hit.voice, hit.velocity, hit.accent, hit.sampleOffset);
    }

    // Auditions land at the top of the block rather than at a sample offset,
    // because a click has no musical position to keep. Taken and cleared in
    // one operation so a second click during this block is heard next block
    // rather than lost.
    const auto auditions = drumAuditions.exchange(0);

    for (int voice = 0; voice < drums::numVoices; ++voice)
    {
        if ((auditions & (juce::uint32(1) << voice)) == 0)
            continue;

        const bool accent = (auditions & (juce::uint32(1) << (voice + auditionAccentShift))) != 0;

        drumKit.trigger(voice, 1.0f, accent, 0);
    }

    drumKit.renderAdding(buffer, 0, buffer.getNumSamples(), drumParameters);
}

bool WaveLatheProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    auto outputSet = layouts.getMainOutputChannelSet();
    return outputSet == juce::AudioChannelSet::mono() || outputSet == juce::AudioChannelSet::stereo();
}



double WaveLatheProcessor::getTailLengthSeconds() const
{
    // Worked out beside the chain that does the ringing, where MasterFxTest can
    // measure it against a real render rather than take its word for it.
    auto seconds = tailLengthSeconds(parameters);

    // A crash struck on the last bar is still sounding after the transport
    // stops, and a host that has not been told truncates the bounce at the
    // last note. The longer of the two rather than their sum: the effects tail
    // and the drum tail ring at the same time, not one after the other.
    //
    // Only when the pattern actually uses a drum. The kit CAN ring for half a
    // minute with Decay at the top, and putting that on the end of every
    // render of a project with no drums in it would be a tax on everyone for
    // the benefit of nobody.
    if (sequencer.getNumUsedDrumVoices() > 0)
        seconds = juce::jmax(seconds, drums::longestTailSeconds(drumParameters, drumKit.getSampleBank()));

    // The bus rings on after BOTH of those have stopped, so its tail is added
    // rather than raced against them: its reverb only starts decaying once the
    // last thing going into it has gone quiet. Nothing at all while every bus
    // effect is dry, which is how a project that never touched it comes out.
    seconds += effectsTailSeconds(busEffectSettingsFrom(parameters));

    return seconds;
}
HostParameter* WaveLatheProcessor::getHostParameter(int registryId) const
{
    if (registryId < 0 || registryId >= (int) hostParameters.size())
        return nullptr;

    return hostParameters[(size_t) registryId];
}

void WaveLatheProcessor::notifyHostOfAllParameters()
{
    for (auto* parameter : hostParameters)
        if (parameter != nullptr)
            parameter->reportToHost();
}

void WaveLatheProcessor::noteHostWroteParameter(int registryId)
{
    if (registryId < 0 || registryId >= (int) hostParameters.size())
        return;

    hostWroteParameters.set(registryId, true);
}
juce::AudioProcessorEditor* WaveLatheProcessor::createEditor()
{
    return new WaveLatheEditor(*this);
}
} // namespace wavelathe

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new wavelathe::WaveLatheProcessor();
}
