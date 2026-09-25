// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "SynthVoice.h"
#include "SynthSound.h"
#include "TempoSync.h"

namespace wavelathe
{
SynthVoice::SynthVoice(const WavetableProvider& tableProvider, const SynthParameters& sharedParams)
    : params(sharedParams), provider(tableProvider)
{
    filterLeft.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
    filterRight.setType(juce::dsp::StateVariableTPTFilterType::lowpass);
}

void SynthVoice::prepare(double sampleRate, int maximumBlockSize)
{
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = (juce::uint32) maximumBlockSize;
    spec.numChannels = 1; // one mono filter per channel, so each side can be modulated separately

    for (auto* f : {&filterLeft, &filterRight, &filterLeft2, &filterRight2})
    {
        f->prepare(spec);
        f->reset();
    }

    for (auto& osc : oscillators)
        osc.setSampleRate(sampleRate);
    for (auto& osc : oscillators2)
        osc.setSampleRate(sampleRate);

    adsr.setSampleRate(sampleRate);
    modEnvelope.setSampleRate(sampleRate);
}

float SynthVoice::softClip(float x, float driveAmount)
{
    if (driveAmount <= 0.0f)
        return x;

    float driveGain = 1.0f + driveAmount * 7.0f;
    float shaped = std::tanh(x * driveGain) / std::tanh(driveGain);
    return x + driveAmount * (shaped - x);
}

void SynthVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int /*pitchWheelPos*/)
{
    noteVelocity = velocity;
    lfoPhase = 0.0f;
    lfo2Phase = 0.0f;
    targetFrequencyHz = (float) juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);

    // A glide starts from the pitch THIS voice last played.
    //
    // It used to start from the pitch the synth last played, held in a single
    // value on the shared parameters that every voice read and then overwrote.
    // For a monophonic line that is the same thing, but for a chord it was not:
    // playing C-E-G, C read whatever came before and stored C, then E read C and
    // slid up from it, then G read E. Each note slid from its neighbour, so a
    // chord smeared into existence from the bottom up instead of each note
    // sliding from its own previous pitch.
    //
    // Per voice, a chord's notes each slide from where that voice was.
    //
    // An earlier version of this comment claimed mono was unaffected because
    // "there is only one voice". That is not what mono does: MonoVoice rewrites
    // the MIDI so one note SOUNDS at a time, and the Synthesiser still holds
    // eight voices and still cycles them while old ones release. Mono was in
    // fact the worst case of all, because with legato off the note-off and the
    // note-on land on the same sample, so the new note ALWAYS finds a voice with
    // no history.
    //
    // A voice with no history of its own falls back to where the LINE was, which
    // is the case poly allocation creates constantly: the first free voice takes
    // each note and a voice stays busy through its release tail, so a line
    // played faster than its own release walks along the voice list and lands
    // on fresh voices every time. Measured without this fallback, such a line
    // did not glide once.
    //
    // lastReleasedHz is only set when a note STOPS, and is consumed here, so
    // exactly one note can use it: the one that follows a release. A chord's
    // notes follow each other with nothing released in between, so they find it
    // empty and start on their own pitches, which is the behaviour the shared
    // origin used to get wrong.
    float glideFrom = lastNoteFrequencyHz;
    if (glideFrom < 20.0f)
        glideFrom = params.lastReleasedHz.load();

    params.lastReleasedHz.store(0.0f);

    noteIsReleasing = false;

    bool gliding = params.glideTimeMs.load() > 1.0f && glideFrom > 20.0f;
    baseFrequencyHz = gliding ? glideFrom : targetFrequencyHz;
    glideStartedAtHz = gliding ? glideFrom : 0.0f;
    lastNoteFrequencyHz = targetFrequencyHz;

    for (int i = 0; i < maxUnisonVoices; ++i)
    {
        oscillators[(size_t) i].resetPhase((float) i / (float) maxUnisonVoices);
        oscillators2[(size_t) i].resetPhase((float) i / (float) maxUnisonVoices);
    }
    subPhase = 0.0f;
    pinkState = 0.0f;

    if (repeatableNoise)
        noiseRandom.setSeed(noiseSeed);

    adsr.setParameters({params.attack.load(), params.decay.load(), params.sustain.load(), params.release.load()});
    adsr.noteOn();

    modEnvelope.setParameters({params.modEnvAttack.load(), params.modEnvDecay.load(), params.modEnvSustain.load(),
                                params.modEnvRelease.load()});
    modEnvelope.noteOn();
}

void SynthVoice::stopNote(float /*velocity*/, bool allowTailOff)
{
    // Where the line just was, for whichever voice takes the next note. Set on
    // both paths: the key is up either way, and it is the key going up that
    // makes the next note a continuation of a line rather than part of a chord.
    //
    // Only from a voice that has actually played something, though. allNotesOff
    // calls stopNote on EVERY voice whether it is playing or not - and JUCE
    // calls allNotesOff when the sample rate is set, as does a DAW's panic
    // button - so without this guard a rack of untouched voices would each
    // publish their default 440 Hz, and the next note played would slide in from
    // an A out of nowhere. That is exactly what happened, and the test caught it.
    //
    // Guarded on this voice's own history rather than on isVoiceActive, because
    // that flag is set by Synthesiser::startVoice rather than by startNote, so it
    // reads false for a voice driven directly - which is how half of this file
    // drives them.
    //
    // And only the FIRST stop counts, because stopNote arrives twice. Before
    // starting a note, Synthesiser::noteOn stops any voice already playing that
    // same note - and in mono, letting go of the newer key to fall back to one
    // still held sends a note-off and a note-on for the older note in the same
    // instant, while that older note's original voice is still finishing its
    // release with the same note number on it. Without this guard that stale
    // voice republished its own pitch over the one the line had just left, and
    // the fall-back arrived with nothing to slide from: measured starting at
    // 130.8 Hz, its own note, instead of sliding down from the 523.3 it had been
    // playing. A voice already releasing is not a key going up.
    if (! noteIsReleasing && lastNoteFrequencyHz > 20.0f)
        params.lastReleasedHz.store(targetFrequencyHz);

    noteIsReleasing = true;

    if (allowTailOff)
    {
        adsr.noteOff();
        modEnvelope.noteOff();
    }
    else
    {
        clearCurrentNote();
        adsr.reset();
        modEnvelope.reset();
        filterLeft.reset();
        filterRight.reset();
        filterLeft2.reset();
        filterRight2.reset();
    }
}

void SynthVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (!adsr.isActive())
        return;

    // Pick up the current wavetable set once per block, so a newly sampled
    // table takes effect without locking the audio thread.
    const WavetableSet* activeTables = provider.active.load(std::memory_order_acquire);
    if (activeTables == nullptr)
        return;

    for (auto& osc : oscillators)
        osc.setTableSet(activeTables);
    for (auto& osc : oscillators2)
        osc.setTableSet(activeTables);

    // ---- Pitch: the legato target, then the glide toward it ---------------
    // In a legato mono line the note that is sounding follows whichever key is
    // held newest, so the target is read here rather than being set once when
    // the note started.
    //
    // Only while the key is still down, though. This runs on every ACTIVE
    // voice, and a voice stays active all the way through its release tail -
    // so without the guard a note that had been let go went on following the
    // line it was no longer part of. Play a note, let it go, play another
    // before the first has faded, and the first one's tail slid up after it:
    // measured leaving C3 at 130.8 Hz and reading 253.8 Hz six blocks later, an
    // octave of bend on a note nobody was holding. On any patch with a tail
    // that is not an edge case, it is every note played faster than the release.
    if (! noteIsReleasing && params.monoMode.load() > 0.5f && params.legatoMode.load() > 0.5f)
    {
        int targetNote = (int) std::round(params.monoTargetNote.load());
        if (targetNote >= 0 && targetNote <= 127)
            targetFrequencyHz = (float) juce::MidiMessage::getMidiNoteInHertz(targetNote);
    }

    // The slide itself, worked out in octaves rather than in hertz so that a
    // slide of an octave takes the same time wherever on the keyboard it
    // happens - in hertz the top of the keyboard would rush and the bottom
    // would crawl.
    float glideMs = juce::jlimit(0.0f, 2000.0f, params.glideTimeMs.load());
    if (glideMs > 1.0f && std::abs(baseFrequencyHz - targetFrequencyHz) > 0.01f)
    {
        double blockSeconds = (double) numSamples / juce::jmax(1.0, spec.sampleRate);
        auto coefficient = (float) (1.0 - std::exp(-blockSeconds / (glideMs * 0.001)));
        float current = std::log2(juce::jmax(1.0f, baseFrequencyHz));
        float destination = std::log2(juce::jmax(1.0f, targetFrequencyHz));
        baseFrequencyHz = std::pow(2.0f, current + (destination - current) * coefficient);
    }
    else
    {
        baseFrequencyHz = targetFrequencyHz;
    }

    int numVoices = juce::jlimit(1, maxUnisonVoices, (int) std::round(params.unisonVoices.load()));
    float detuneSpread = params.unisonDetuneCents.load();
    float unisonWidth = juce::jlimit(0.0f, 1.0f, params.unisonWidth.load());
    float baseWavePosition = params.wavePosition.load();

    auto osc1Level = juce::jlimit(0.0f, 1.0f, params.osc1Level.load());
    auto osc2Level = juce::jlimit(0.0f, 1.0f, params.osc2Level.load());
    auto osc2WavePosition = juce::jlimit(0.0f, 1.0f, params.osc2WavePosition.load());
    auto subLevel = juce::jlimit(0.0f, 1.0f, params.subLevel.load());
    auto subWave = juce::jlimit(0.0f, 1.0f, params.subWave.load());
    auto noiseLevel = juce::jlimit(0.0f, 1.0f, params.noiseLevel.load());
    auto noiseColour = juce::jlimit(0.0f, 1.0f, params.noiseColour.load());

    // Osc 2 is offset in pitch from osc 1; coarse in semitones, fine in cents.
    float osc2Ratio = std::pow(2.0f, params.osc2Semitones.load() / 12.0f + params.osc2Fine.load() / 1200.0f);
    int subOctaves = juce::jlimit(1, 2, (int) std::round(params.subOctave.load()));
    float subFrequency = baseFrequencyHz / (float) (1 << subOctaves);
    float subIncrement = (float) (subFrequency / spec.sampleRate);

    for (int v = 0; v < numVoices; ++v)
    {
        float detuneCents = numVoices == 1 ? 0.0f : detuneSpread * ((float) v / (float) (numVoices - 1) - 0.5f);
        float freq = baseFrequencyHz * std::pow(2.0f, detuneCents / 1200.0f);
        oscillators[(size_t) v].setFrequency(freq);
        oscillators2[(size_t) v].setFrequency(freq * osc2Ratio);
    }

    float invSqrtVoices = 1.0f / std::sqrt((float) numVoices);

    int filterType = juce::jlimit(0, 4, (int) std::round(params.filterType.load()));
    auto svfType = juce::dsp::StateVariableTPTFilterType::lowpass;
    if (filterType == 2)
        svfType = juce::dsp::StateVariableTPTFilterType::highpass;
    else if (filterType == 3 || filterType == 4)
        svfType = juce::dsp::StateVariableTPTFilterType::bandpass;

    for (auto* f : {&filterLeft, &filterRight, &filterLeft2, &filterRight2})
        f->setType(svfType);

    adsr.setParameters({params.attack.load(), params.decay.load(), params.sustain.load(), params.release.load()});
    modEnvelope.setParameters({params.modEnvAttack.load(), params.modEnvDecay.load(), params.modEnvSustain.load(),
                                params.modEnvRelease.load()});

    auto baseCutoff = params.filterCutoffHz.load();

    // Velocity pushes the resonance as well as the cutoff. Brightness alone
    // makes a hard-struck note merely open; it is the resonance moving with it
    // that gives the note an edge, which is what an accent is reaching for.
    //
    // Per voice, from the velocity this voice was started with, so it belongs
    // to the note rather than to whatever was played most recently.
    auto resonance = juce::jlimit(0.0f, 0.95f,
                                  params.filterResonance.load()
                                      + juce::jlimit(0.0f, 1.0f, params.velocityToResonance.load())
                                            * noteVelocity * 0.6f);
    auto lfoRate = params.lfoRateHz.load();
    auto lfoDepth = params.lfoDepth.load();
    auto lfoAmpDepth = juce::jlimit(0.0f, 1.0f, params.lfoAmpDepth.load());
    auto driveAmount = juce::jlimit(0.0f, 1.0f, params.driveAmount.load());
    auto masterGain = params.masterGain.load();

    auto lfoToWave = juce::jlimit(0.0f, 1.0f, params.lfoToWave.load());
    auto modEnvToWave = juce::jlimit(-1.0f, 1.0f, params.modEnvToWave.load());
    auto modEnvToCutoff = juce::jlimit(-1.0f, 1.0f, params.modEnvToCutoff.load());
    auto lfo2Rate = params.lfo2RateHz.load();
    auto lfo2ToWave = juce::jlimit(0.0f, 1.0f, params.lfo2ToWave.load());
    auto lfo2ToCutoff = juce::jlimit(0.0f, 1.0f, params.lfo2ToCutoff.load());

    // Expression: how hard the note was struck, and where the wheel and the key
    // pressure are now. These are read every block rather than latched at
    // note-on, so the wheel and aftertouch move the sound while it sounds -
    // velocity, being a property of the strike, does not change after it.
    auto velocityToCutoff = juce::jlimit(0.0f, 1.0f, params.velocityToCutoff.load());
    auto velocityToWave = juce::jlimit(0.0f, 1.0f, params.velocityToWave.load());
    auto velocityToAmp = juce::jlimit(0.0f, 1.0f, params.velocityToAmp.load());
    auto wheelToCutoff = juce::jlimit(0.0f, 1.0f, params.wheelToCutoff.load());
    auto wheelToWave = juce::jlimit(0.0f, 1.0f, params.wheelToWave.load());
    auto pressureToCutoff = juce::jlimit(0.0f, 1.0f, params.pressureToCutoff.load());

    float wheel = juce::jlimit(0.0f, 1.0f, params.modWheel.load());
    float pressure = juce::jlimit(0.0f, 1.0f, params.channelPressure.load());

    // Four octaves at full depth, which is enough for a hard strike to change
    // the character of a sound rather than just its loudness.
    constexpr float expressionCutoffOctaves = 4.0f;
    float expressionOctaves = velocityToCutoff * noteVelocity * expressionCutoffOctaves
                              + wheelToCutoff * wheel * expressionCutoffOctaves
                              + pressureToCutoff * pressure * expressionCutoffOctaves;
    float expressionCutoffMultiplier = std::pow(2.0f, expressionOctaves);
    float expressionWave = velocityToWave * noteVelocity + wheelToWave * wheel;

    // At zero, velocity leaves the level alone entirely; at one it sets it, as
    // it always did - so a patch that never touches this sounds unchanged.
    float velocityGain = 1.0f - velocityToAmp + velocityToAmp * noteVelocity;

    // Either LFO can be locked to the tempo grid independently.
    if (params.lfo1Sync.load() > 0.5f)
        lfoRate = (float) tempo::divisionToHz(tempo::nearestDivisionForHz(lfoRate, currentBpm), currentBpm);
    if (params.lfo2Sync.load() > 0.5f)
        lfo2Rate = (float) tempo::divisionToHz(tempo::nearestDivisionForHz(lfo2Rate, currentBpm), currentBpm);

    auto lfoIncrement = (float) (lfoRate / spec.sampleRate);
    auto lfo2Increment = (float) (lfo2Rate / spec.sampleRate);
    bool isStereoOut = outputBuffer.getNumChannels() >= 2;

    // How far the mod envelope can push the cutoff, in octaves at full depth.
    constexpr float modEnvCutoffOctaves = 5.0f;

    // A quarter-cycle offset between the channels turns the LFO's movement
    // into a sweep across the stereo field rather than both sides pumping
    // together.
    bool stereoLfo = params.stereoLfo.load() > 0.5f;
    float rightPhaseOffset = stereoLfo ? 0.25f : 0.0f;
    float clampedResonance = juce::jlimit(0.1f, 1.0f, resonance);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        auto lfoLeft = std::sin(juce::MathConstants<float>::twoPi * lfoPhase);
        auto lfoRight = stereoLfo
                            ? std::sin(juce::MathConstants<float>::twoPi * (lfoPhase + rightPhaseOffset))
                            : lfoLeft;

        lfoPhase += lfoIncrement;
        if (lfoPhase >= 1.0f)
            lfoPhase -= 1.0f;

        auto lfo2Value = std::sin(juce::MathConstants<float>::twoPi * lfo2Phase);
        lfo2Phase += lfo2Increment;
        if (lfo2Phase >= 1.0f)
            lfo2Phase -= 1.0f;

        auto modEnvValue = modEnvelope.getNextSample();

        // Wavetable position moves during the note - this is what makes the
        // frames a sweep rather than a static blend point. Unipolar LFO
        // contributions so they push in one direction from the dial setting.
        float wavePosition = baseWavePosition + modEnvToWave * modEnvValue
                             + lfoToWave * (lfoLeft * 0.5f + 0.5f) + lfo2ToWave * (lfo2Value * 0.5f + 0.5f)
                             + expressionWave;
        wavePosition = juce::jlimit(0.0f, 1.0f, wavePosition);

        // Envelope-to-cutoff is exponential (in octaves), which is how filter
        // sweeps are normally felt; the LFOs stay proportional on top.
        float envCutoffMultiplier = std::pow(2.0f, modEnvToCutoff * modEnvValue * modEnvCutoffOctaves);
        float lfo2CutoffLeft = 1.0f + lfo2Value * lfo2ToCutoff;

        float cutoffL = juce::jlimit(20.0f, 20000.0f, baseCutoff * envCutoffMultiplier
                                                          * expressionCutoffMultiplier
                                                          * (1.0f + lfoLeft * lfoDepth) * lfo2CutoffLeft);
        float cutoffR = juce::jlimit(20.0f, 20000.0f, baseCutoff * envCutoffMultiplier
                                                          * expressionCutoffMultiplier
                                                          * (1.0f + lfoRight * lfoDepth) * lfo2CutoffLeft);

        filterLeft.setCutoffFrequency(cutoffL);
        filterRight.setCutoffFrequency(cutoffR);
        filterLeft.setResonance(clampedResonance);
        filterRight.setResonance(clampedResonance);

        if (filterType == 1)
        {
            filterLeft2.setCutoffFrequency(cutoffL);
            filterRight2.setCutoffFrequency(cutoffR);
            filterLeft2.setResonance(clampedResonance);
            filterRight2.setResonance(clampedResonance);
        }

        auto ampLfoGainLeft = 1.0f - lfoAmpDepth * (1.0f - lfoLeft) * 0.5f;
        auto ampLfoGainRight = 1.0f - lfoAmpDepth * (1.0f - lfoRight) * 0.5f;

        // Osc 2 follows the same wavetable sweep, offset by its own position.
        float osc2Position = juce::jlimit(0.0f, 1.0f, wavePosition + osc2WavePosition);

        float sumL = 0.0f, sumR = 0.0f;
        for (int v = 0; v < numVoices; ++v)
        {
            float panPos = numVoices == 1 ? 0.0f : unisonWidth * (2.0f * (float) v / (float) (numVoices - 1) - 1.0f);
            float angle = (panPos + 1.0f) * juce::MathConstants<float>::halfPi * 0.5f;
            float gainL = std::cos(angle);
            float gainR = std::sin(angle);

            float voiceSample = 0.0f;
            if (osc1Level > 0.0f)
            {
                oscillators[(size_t) v].setWavePosition(wavePosition);
                voiceSample += oscillators[(size_t) v].getNextSample() * osc1Level;
            }
            if (osc2Level > 0.0f)
            {
                oscillators2[(size_t) v].setWavePosition(osc2Position);
                voiceSample += oscillators2[(size_t) v].getNextSample() * osc2Level;
            }

            voiceSample *= invSqrtVoices;
            sumL += voiceSample * gainL;
            sumR += voiceSample * gainR;
        }

        // Sub oscillator: sine blended toward square, an octave or two down.
        if (subLevel > 0.0f)
        {
            float subSine = std::sin(juce::MathConstants<float>::twoPi * subPhase);
            float subSquare = subPhase < 0.5f ? 1.0f : -1.0f;
            float subSample = (subSine + subWave * (subSquare - subSine)) * subLevel;
            sumL += subSample;
            sumR += subSample;

            subPhase += subIncrement;
            if (subPhase >= 1.0f)
                subPhase -= 1.0f;
        }

        // Noise, white through to pink via a one-pole tilt.
        if (noiseLevel > 0.0f)
        {
            float white = noiseRandom.nextFloat() * 2.0f - 1.0f;
            pinkState += 0.02f * (white - pinkState);
            float pink = pinkState * 4.0f;
            float noiseSample = (white + noiseColour * (pink - white)) * noiseLevel * 0.5f;
            sumL += noiseSample;
            sumR += noiseSample;
        }

        auto envelopeValue = adsr.getNextSample();

        auto filteredL = filterLeft.processSample(0, sumL);
        auto filteredR = filterRight.processSample(0, sumR);

        if (filterType == 1) // 24 dB/octave: a second cascaded stage
        {
            filteredL = filterLeft2.processSample(0, filteredL);
            filteredR = filterRight2.processSample(0, filteredR);
        }
        else if (filterType == 4) // notch: the band removed from the input
        {
            filteredL = sumL - filteredL;
            filteredR = sumR - filteredR;
        }

        auto drivenL = softClip(filteredL, driveAmount);
        auto drivenR = softClip(filteredR, driveAmount);

        auto gainStage = envelopeValue * velocityGain * masterGain;
        auto finalL = drivenL * gainStage * ampLfoGainLeft;
        auto finalR = drivenR * gainStage * ampLfoGainRight;

        if (isStereoOut)
        {
            outputBuffer.addSample(0, startSample + sample, finalL);
            outputBuffer.addSample(1, startSample + sample, finalR);
        }
        else
        {
            outputBuffer.addSample(0, startSample + sample, (finalL + finalR) * 0.5f);
        }

        if (!adsr.isActive())
        {
            clearCurrentNote();
            modEnvelope.reset();
            filterLeft.reset();
            filterRight.reset();
            filterLeft2.reset();
            filterRight2.reset();
            break;
        }
    }
}
} // namespace wavelathe
