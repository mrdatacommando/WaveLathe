// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include "ProjectState.h"

#include <array>
#include <atomic>

namespace wavelathe
{
// Live-adjustable parameters shared between the editor and all synth voices.
// Voices read these once per block, so plain atomics (no locking) are enough.
struct SynthParameters
{
    std::atomic<float> wavePosition{0.0f};

    std::atomic<float> attack{0.02f};
    std::atomic<float> decay{0.15f};
    std::atomic<float> sustain{0.8f};
    std::atomic<float> release{0.2f};

    std::atomic<float> filterCutoffHz{4000.0f};
    std::atomic<float> filterResonance{0.7f};
    // 0 = LP12, 1 = LP24, 2 = HP12, 3 = BP12, 4 = Notch
    std::atomic<float> filterType{0.0f};

    // Oscillator mix. Osc 1 and Osc 2 share the wavetable set but have their
    // own position and tuning; the sub and noise sources sit underneath.
    std::atomic<float> osc1Level{1.0f};
    std::atomic<float> osc2Level{0.0f};
    std::atomic<float> osc2WavePosition{0.0f};
    std::atomic<float> osc2Semitones{0.0f}; // -24..+24
    std::atomic<float> osc2Fine{0.0f};      // -100..+100 cents

    std::atomic<float> subLevel{0.0f};
    std::atomic<float> subWave{0.0f};   // 0 = sine, 1 = square
    std::atomic<float> subOctave{1.0f}; // 1 or 2 octaves below

    std::atomic<float> noiseLevel{0.0f};
    std::atomic<float> noiseColour{0.0f}; // 0 = white, 1 = pink

    std::atomic<float> lfoRateHz{2.0f};
    std::atomic<float> lfoDepth{0.0f};    // 0..1, scales cutoff modulation
    std::atomic<float> lfoAmpDepth{0.0f}; // 0..1, scales amplitude modulation
    std::atomic<float> lfoToWave{0.0f};   // 0..1, LFO 1 sweeps the wavetable

    // A second envelope, free to be routed rather than hardwired to level.
    // Routing this to wavetable position is the characteristic wavetable
    // gesture, and routing it to cutoff is the classic filter sweep - neither
    // of which the synth could express while those were static per note.
    std::atomic<float> modEnvAttack{0.01f};
    std::atomic<float> modEnvDecay{0.4f};
    std::atomic<float> modEnvSustain{0.3f};
    std::atomic<float> modEnvRelease{0.4f};
    std::atomic<float> modEnvToWave{0.0f};   // -1..1, bipolar
    std::atomic<float> modEnvToCutoff{0.0f}; // -1..1, bipolar, in octaves

    // A second LFO, for movement independent of the first.
    std::atomic<float> lfo2RateHz{0.5f};
    std::atomic<float> lfo2ToWave{0.0f};   // 0..1
    std::atomic<float> lfo2ToCutoff{0.0f}; // 0..1

    std::atomic<float> unisonVoices{1.0f};      // 1..7, rounded
    std::atomic<float> unisonDetuneCents{0.0f}; // 0..50, spread across voices
    std::atomic<float> unisonWidth{0.0f};       // 0..1, stereo spread

    std::atomic<float> driveAmount{0.0f}; // 0..1, per-voice pre-filter grit

    // When on, the LFO runs a quarter-cycle apart on left and right, so its
    // movement sweeps across the stereo field instead of pumping both
    // channels together.
    std::atomic<float> stereoLfo{0.0f}; // 0 = mono LFO, 1 = stereo

    // Master effects, applied once to the mixed output rather than per voice.
    std::atomic<float> fxDistortion{0.0f};   // 0..1
    std::atomic<float> delayTimeMs{320.0f};  // 10..1500
    std::atomic<float> delayFeedback{0.35f}; // 0..0.95
    std::atomic<float> delayMix{0.0f};       // 0..1
    std::atomic<float> reverbSize{0.5f};     // 0..1
    std::atomic<float> reverbMix{0.0f};      // 0..1

    // Movement and tone shaping. Chorus and phaser both work by mixing the
    // signal with a moving copy of itself - chorus delays it, the phaser shifts
    // its phase - which is why both are wet/dry mixes rather than switches.
    std::atomic<float> chorusRate{0.6f};     // Hz
    std::atomic<float> chorusDepth{0.35f};   // 0..1
    std::atomic<float> chorusMix{0.0f};      // 0..1
    std::atomic<float> phaserRate{0.4f};     // Hz
    std::atomic<float> phaserFeedback{0.4f}; // 0..0.9
    std::atomic<float> phaserMix{0.0f};      // 0..1

    // A three-band tilt over the finished sound, in decibels. Flat at zero, so
    // a patch that never touches it is unaffected.
    std::atomic<float> eqLowGain{0.0f};      // -12..+12 dB
    std::atomic<float> eqMidGain{0.0f};
    std::atomic<float> eqHighGain{0.0f};

    // ---- Mastering -----------------------------------------------------------
    // The chain on the Master page: saturation, compressor, limiter. It runs
    // after everything above, on the finished stereo mix.
    //
    // OFF by default, and that is not timidity. Sixty-five factory presets were
    // voiced without it, and a stage that engaged itself would change every one
    // of them the first time this version opened an old patch.
    //
    // None of these are in ParameterRegistry, so none are automatable from a
    // host or recordable by the sequencer. That is a deliberate spend of a
    // budget that has one slot left in it, not an oversight: the registry is
    // capped at 64 by the atomic word the sequencer reads, twelve dials would
    // need twelve of them, and the separate effect plugin these same controls
    // are heading for gets a fresh sixty-four of its own. They are saved with
    // the preset like every other unregistered parameter here.
    std::atomic<float> masteringEnabled{0.0f};

    std::atomic<float> masterSatDrive{0.0f};        // 0..1
    std::atomic<float> masterSatMix{1.0f};          // 0..1

    std::atomic<float> masterCompThresholdDb{0.0f}; // -48..0
    std::atomic<float> masterCompRatio{2.0f};       // 1..20
    std::atomic<float> masterCompAttackMs{10.0f};   // 0.1..100
    std::atomic<float> masterCompReleaseMs{100.0f}; // 10..1000
    std::atomic<float> masterCompMakeupDb{0.0f};    // 0..24
    std::atomic<float> masterCompMix{1.0f};         // 0..1

    std::atomic<float> masterLimitCeilingDb{-0.3f}; // -12..0
    std::atomic<float> masterLimitReleaseMs{50.0f}; // 1..500

    // Trim into the limiter rather than a final volume. It has to sit before
    // the limiter, because anything after it could push the signal back over
    // the ceiling the limiter just held.
    std::atomic<float> masterFxGainDb{0.0f};        // -24..+24

    // Tempo everything syncs to. midiClockSync slaves it to incoming MIDI
    // clock when one is present, falling back to the local BPM otherwise.
    std::atomic<float> bpm{120.0f};
    std::atomic<float> midiClockSync{0.0f};

    // Per-feature sync. When on, that control's rate snaps to the nearest
    // musical division of the tempo instead of running free.
    std::atomic<float> arpSync{1.0f};
    std::atomic<float> delaySync{0.0f};
    std::atomic<float> lfo1Sync{0.0f};
    std::atomic<float> lfo2Sync{0.0f};

    // Arpeggiator: a performance control that replaces held notes with a
    // repeating pattern before they reach the voices.
    // Two independent axes, the way hardware arpeggiators separate them: the
    // mode decides which note comes next, the pattern decides the rhythm it is
    // played in - which steps rest, tie, accent, ratchet or jump an octave.
    std::atomic<float> arpEnabled{0.0f};
    std::atomic<float> arpMode{0.0f};    // index into Arpeggiator::Mode (see Arpeggiator.h)
    std::atomic<float> arpPattern{0.0f}; // index into the rhythm pattern table
    // Rate is a musical division when synced, the way every synced arpeggiator
    // states it, and free-running steps per second only when sync is off.
    std::atomic<float> arpRateHz{8.0f};    // steps per second, used when sync is off
    std::atomic<float> arpDivision{11.0f}; // 1/16 in the tempo division table
    // How the rate relates to the sequencer's: a clock divider, so the two land
    // on the same grid. Default is the sequencer's own rate.
    std::atomic<float> arpRateLink{2.0f};  // index into the rate link table
    std::atomic<float> arpOctaves{1.0f}; // 1..3
    std::atomic<float> arpGate{0.6f};    // fraction of each step the note sounds
    std::atomic<float> arpSwing{0.0f};   // 0 straight, up to 0.75 for a hard shuffle

    // How playing itself shapes the sound. Without these, velocity only makes a
    // note louder and the wheel does nothing at all - so an accented step in a
    // pattern is just a loud one, which is not what an accent is.
    std::atomic<float> velocityToCutoff{0.0f};  // 0..1, opens the filter, in octaves
    std::atomic<float> velocityToWave{0.0f};    // 0..1, pushes the wavetable position
    std::atomic<float> velocityToAmp{1.0f};     // 0..1, how much it sets the level
    // Resonance is the other half of what an accent does. Opening the filter
    // alone makes an accented note brighter; pushing the resonance with it is
    // what makes it bite, which is the sound the step sequencer's accents are
    // reaching for.
    std::atomic<float> velocityToResonance{0.0f}; // 0..1, adds resonance with velocity

    // How hard an accented step hits, as a velocity. One dial for the whole
    // pattern, the way the machines this borrows from have one ACCENT knob -
    // the per-step flag says WHICH notes are accented, this says by how much.
    //
    // Deliberately a velocity rather than a switch the voice reads: velocity is
    // the only per-note channel between the sequencer and the synth, so an
    // accent expressed as anything else would have to be shared state that
    // every voice reads at once, which is the shape of every voice bug this
    // synth has had.
    std::atomic<float> accentAmount{0.95f};     // 0..1, velocity an accented step plays at
    std::atomic<float> wheelToCutoff{0.0f};     // 0..1, mod wheel to filter
    std::atomic<float> wheelToWave{0.0f};       // 0..1, mod wheel to wavetable
    std::atomic<float> pressureToCutoff{0.0f};  // 0..1, aftertouch to filter

    // Runtime, not settings: where the wheel and the key pressure are right
    // now. Read from the incoming MIDI, not saved with a patch.
    // Which MIDI channel plays notes: 0 for every channel, 1-16 for one.
    //
    // Deliberately NOT saved in a preset, for the same reason the MIDI learn
    // map is not: it describes what is plugged in, not what the patch sounds
    // like, and a patch that silently rerouted your keyboard would be a patch
    // you could not trust to load. It lives in the application's settings.
    //
    // Filters notes only. Controllers pass on every channel whatever this says,
    // so narrowing the keyboard to one channel does not disconnect a knob that
    // sends on another - which is the whole point of being able to narrow it.
    std::atomic<float> keyboardChannel{0.0f};

    // Which MIDI channel strikes the drum kit: 0 for none, 1-16 for one.
    //
    // A separate channel and not a note range, because the kit and the synth
    // are two instruments and a pad controller is usually a second device. On
    // one channel the two would be fighting over the same octaves - a GM kick
    // is C1, which is a note somebody wants to PLAY.
    //
    // Zero by default, so nothing changes for anybody who has not asked for
    // it. Ten is the obvious value to pick, being what General MIDI reserves
    // for drums, but picking it here would start routing notes the first time
    // this build opened an existing session.
    //
    // Notes on this channel never reach the synth, and notes on the keyboard's
    // channel never reach the kit, even when the two are set to the same
    // number - see the note on splitIncomingNotes. That is what makes it a
    // split rather than a layer.
    std::atomic<float> drumChannel{0.0f};

    // How far the kit's General MIDI note map is shifted, in semitones.
    //
    // Here rather than only in AppSettings because the audio thread reads it on
    // every drum note, and the desk setting is the message thread's copy. The
    // same arrangement drumChannel has, and for the same reason.
    //
    // It moves the GM map only. A slot taught its own note by DrumNoteMap keeps
    // that note, because the note was learned by playing the pad and shifting
    // it afterwards would move the slot off the pad it was just shown.
    std::atomic<float> drumTranspose{0.0f};

    std::atomic<float> modWheel{0.0f};
    std::atomic<float> channelPressure{0.0f};

    // Key and scale every played note is pulled into, and the chord built on
    // it. Scale 0 (chromatic) and chord mode 0 (off) leave playing untouched.
    std::atomic<float> musicalKey{0.0f};   // 0 = C .. 11 = B
    std::atomic<float> musicalScale{0.0f}; // index into music::Scale
    std::atomic<float> chordMode{0.0f};    // index into music::ChordMode

    // How a played line moves between notes. Glide slides the pitch instead of
    // jumping; mono keeps one note sounding at a time; legato then lets an
    // overlapping note take over without starting the envelope again, which is
    // what makes a bassline join up rather than stutter.
    std::atomic<float> glideTimeMs{0.0f};  // 0 = off, up to 2000
    std::atomic<float> monoMode{0.0f};
    std::atomic<float> legatoMode{1.0f};   // only does anything in mono

    // Runtime, not a setting: the note a legato line is currently reaching for.
    // Not saved - it describes what is happening right now, not how the patch is
    // set up.
    std::atomic<float> monoTargetNote{-1.0f};

    // The pitch of the note that most recently STOPPED.
    //
    // A glide starts from the pitch the same voice last played, which is right
    // whenever that voice is available. In poly it often is not: the allocator
    // hands each note to the first FREE voice and a voice stays busy all through
    // its release tail, so a line played faster than its own release walks along
    // the voice list and every note lands somewhere with no history at all.
    // Measured, such a line lost its glide completely - alternating C3 and C5
    // put C3 on voice 0 and C5 on voice 1 every time, each voice repeating its
    // own pitch, nothing ever sliding.
    //
    // This is the fallback for that, and unlike the shared origin it replaced it
    // is genuinely global: it says where the LINE was, not where any one voice
    // was. It is consumed by the next note to start, so only a note that follows
    // a release can use it - the notes of a chord follow each other with nothing
    // released in between, so they still start on their own pitches.
    //
    // Mutable because a voice sees the settings as const: it is not changing how
    // the patch is set up, only leaving a note of where the line just was.
    mutable std::atomic<float> lastReleasedHz{0.0f};

    // Latch: while on, note-offs are held back so everything played keeps
    // sounding, and tapping a held note lets that one go.
    std::atomic<float> holdNotes{0.0f};

    // Step sequencer transport and shape.
    std::atomic<float> seqPlaying{0.0f};
    std::atomic<float> seqRecord{0.0f};
    std::atomic<float> seqLength{64.0f};
    std::atomic<float> seqRecordNotes{1.0f}; // REC also captures what you play
    std::atomic<float> seqQuantise{1.0f};    // snap captured notes to the grid
    std::atomic<float> seqDivision{11.0f}; // 1/16 in the tempo division table
    std::atomic<float> seqRootNote{48.0f};

    std::atomic<float> masterGain{0.7f};

    // ---- The drum mixer ----------------------------------------------------
    //
    // Twelve voices of nine controls, as one flat array rather than a hundred
    // and eight named members.
    //
    // Named members would have meant three lists of a hundred and eight that
    // all had to agree - the members here, the registry rows that point at
    // them, and the copy into drums::KitParameters - and three hand-written
    // lists that long is three chances to put Cowbell's decay on the Clave. An
    // array lets all three be loops over project::drumControlIndex instead, so
    // there is one ordering and it is written down once.
    //
    // The cost is that a registry Descriptor can no longer just be a
    // pointer-to-member; see the drumControl field there.
    //
    // Defaults match drums::VoiceParameters, in the registry's units: Level,
    // Attack and Decay 0-1, Tune in semitones, Pan -1 to 1, each send a
    // destination 0-4. Those units are the STORED ones and two of them are not
    // the kit's own - the kit wants Tune and Pan as 0-1, and
    // WaveLatheProcessor::updateDrumParameters is the one place that converts.
    //
    // Every control here has a registry row since 0.44.0, so the array and the
    // kit's block of ids are the same shape. They were not while four of the
    // nine were registered, and the array was this shape then too: where a
    // value lives is this array's business and which ones are registered is the
    // registry's, and splitting the storage to match would have meant Tune and
    // Pan needing a second home for no reason.
    std::array<std::atomic<float>, project::numDrumStoredControls> drumControls;

    // The four shared units the sends feed. Unregistered, saved with the
    // project, in the same units the panel's dials read - see
    // project::DrumFxControl.
    std::array<std::atomic<float>, project::numDrumFxControls> drumFx;

    // The mix bus on the Master page: a second copy of the effects above, run
    // over synth and drums together before the mastering chain. Unregistered
    // and saved with the project - see project::BusFxControl.
    std::array<std::atomic<float>, project::numBusFxControls> busFx;

    SynthParameters()
    {
        for (int i = 0; i < project::numBusFxControls; ++i)
            busFx[(size_t) i].store(project::defaultBusFxValue((project::BusFxControl) i));

        for (int voice = 0; voice < project::numDrumVoices; ++voice)
        {
            using DC = project::DrumControl;
            drumControls[(size_t) project::drumControlIndex(voice, DC::level)].store(0.8f);
            drumControls[(size_t) project::drumControlIndex(voice, DC::tune)].store(0.0f);
            drumControls[(size_t) project::drumControlIndex(voice, DC::attack)].store(0.0f);
            drumControls[(size_t) project::drumControlIndex(voice, DC::decay)].store(0.5f);
            drumControls[(size_t) project::drumControlIndex(voice, DC::pan)].store(0.0f);
            drumControls[(size_t) project::drumControlIndex(voice, DC::send1)].store(0.0f);
            drumControls[(size_t) project::drumControlIndex(voice, DC::send1Amount)].store(0.0f);
            drumControls[(size_t) project::drumControlIndex(voice, DC::send2)].store(0.0f);
            drumControls[(size_t) project::drumControlIndex(voice, DC::send2Amount)].store(0.0f);
        }

        for (int i = 0; i < project::numDrumFxControls; ++i)
            drumFx[(size_t) i].store(project::defaultDrumFxValue((project::DrumFxControl) i));
    }
};
} // namespace wavelathe
