// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "FxpPreset.h"
#include "ParameterRegistry.h"

#include <algorithm>
#include <cmath>

namespace wavelathe
{
namespace
{
constexpr int32_t v1ChunkDataSize = 48; // "WFpr" tag(4) + version(4) + 10 floats(40)
constexpr int32_t v2ChunkDataSize = 68; // "WFpr" tag(4) + version(4) + 15 floats(60)
// v3 adds a wavetable-sample-count int, then that many floats of table data.
constexpr int32_t v3HeaderSize = 72; // v2 fields + wavetable count(4)
// v4 adds 7 master-effect fields before the wavetable count.
constexpr int32_t v4HeaderSize = v3HeaderSize + 7 * 4;
// v5 adds 10 modulation fields (mod envelope, LFO 2, routing depths).
constexpr int32_t v5HeaderSize = v4HeaderSize + 10 * 4;
// v6 adds filter type plus the osc 2, sub and noise sources.
constexpr int32_t v6HeaderSize = v5HeaderSize + 11 * 4;
// v7 adds the arpeggiator, the tempo and its sync switches, and the key the
// patch is played in - all of which used to be lost on save.
constexpr int32_t v7HeaderSize = v6HeaderSize + 16 * 4;
// v8 adds the rates and how they are linked, and then the sequencer itself -
// its pattern and automation lanes - written after the wavetable data, so a
// saved project is the whole of what you built rather than just the synth.
constexpr int32_t v8HeaderSize = v7HeaderSize + 7 * 4;
// v9 adds glide, mono and legato, and the chorus, phaser and tone controls.
constexpr int32_t v9HeaderSize = v8HeaderSize + 12 * 4;
// v10 adds velocity, mod wheel and aftertouch as modulation sources.
constexpr int32_t v10HeaderSize = v9HeaderSize + 6 * 4;
// v11 measures note lengths in ticks rather than whole steps, so a note can be
// a fraction of a step long. Nothing was added to the header - only the meaning
// of the length field changed, which is why older projects are converted on the
// way in rather than simply read.
// v12 adds a per-step slide flag, written with the steps rather than in the
// header, so the header size is unchanged from v10.
// v13 adds accent: velocity to resonance, and how hard an accented step hits.
constexpr int32_t v13HeaderSize = v10HeaderSize + 2 * 4;
// v16 adds the mastering chain: saturation, compressor, limiter, and the trim
// into the limiter. Twelve floats, appended to the header like every version
// before it, so a v15 file still reads and simply gets the defaults - which
// have the whole stage switched off.
constexpr int32_t v16HeaderSize = v13HeaderSize + 12 * 4;

// v17 adds the drum pattern. Nothing in the header: it is a variable-length
// block appended at the very end, like the sequencer and the MIDI map before
// it, so a v16 file reads exactly as it always did and simply arrives with no
// drums on it.
//
// v18 splits each drum step into project::drumSubdivisions cells, so a hit can
// land between one beat and the next, and gives every cell a nudge in ticks so
// it can sit slightly off that. A row is 128 entries rather than 64 and each
// one carries a fifth field.
//
// v17 rows are read and spread: entry i was a whole step, so it becomes cell
// i * drumSubdivisions with no nudge. That lands every old hit exactly where it
// used to be, with the new positions between them empty - which is what a
// pattern written before 32nds existed means.
// v19 adds the drum mixer: sixteen voices of Level, Tune, Decay and Pan, as
// exactly sixty-four floats. Fixed rather than variable-length because the
// mixer has a value for every voice whether or not that voice has a hit on it,
// so there is nothing for "absent" to mean that "default" does not cover.
//
// At the TAIL, past the drum pattern, rather than in the header - which is the
// arrangement v17 chose and this tried the other way round first. Sixty-four
// floats in the middle of the payload left v16HeaderSize wrong, and broke the
// byte-exact downgrade helpers in DrumPatternTest, which patch a real file's
// tail to make an older one. At the tail there is no offset for anything to
// learn: the header is frozen where v16 left it, and a downgrade strips a
// block instead of splicing one out of the middle.
//
// A v18 file reads as it always did and arrives with defaultDrumControls(),
// which is an untouched mixer - which is what a project written before the
// mixer existed had.
//
// v20 adds the two sends per voice and the rack they feed: thirty-two floats
// and then twelve more, in a block of its own AFTER v19's.
//
// A block of its own, and that is the whole point. The mixer grew from four
// controls per voice to six, so the obvious move was to widen v19's block from
// sixty-four floats to ninety-six - which would have changed what the bytes at
// a given offset mean, in a file that already exists. Every version here is
// additive precisely so that never happens: v19's sixty-four floats are still
// v19's sixty-four floats, in the order v19 wrote them, and the two new
// controls are appended past them where nothing has to be reinterpreted.
//
// The cost is that one array is now written in two pieces, which the two write
// loops below say out loud rather than hide behind a single range.
//
// v21 adds the two send AMOUNTS per voice - thirty-two more floats, in a third
// block, for the third time and for the same reason. The two slots became a
// chain rather than two parallel sends, so each one grew a wet/dry mix beside
// its selector; the selectors themselves are exactly where v20 put them and
// mean exactly what they meant.
//
// Three blocks for one array is starting to look like a cost, and it is. The
// alternative is worse: a single block whose layout changes with the code
// would make every saved project a guess about which build wrote it.
//
// v22 adds Attack: one float per voice, sixteen in all. Zero for anything
// older, which is not merely the default but the correct reading - a kit
// written before there was an attack struck instantly, and zero is instant.
//
// v23 embeds the sample slots: the audio itself, not a path to it.
//
// Referenced-by-path was the alternative and it loses the argument in one
// line: a project you send to somebody else has to still play. The cost is
// that a project carrying sixteen slots is measured in megabytes rather than
// kilobytes, which is why the loader caps a slot at drums::maxSampleSeconds
// and sums to mono - the format's size is decided there, not here.
//
// Sparse: a count, then only the slots that have anything in them. A project
// with one sample pays for one, and the common case of none costs a single
// zeroed int.
//
// Floats rather than 16-bit, and that is a real choice. Halving it was
// available and the audio is already normalised into float on the way in;
// storing what is actually in memory means a save and a load are exact, and
// a format that quietly requantises every time a project is reopened is a
// format that degrades work.
//
// v24 is the first version where the kit is a DIFFERENT SIZE, and that is a
// kind of change none of the twenty-three before it made.
//
// Every one of those was additive: a block appended at the tail, with older
// files reading exactly as they always had. This one changes how many times
// the v19-v22 loops go round - from sixteen to twelve - which would silently
// re-interpret bytes in files that already exist. So the count is frozen per
// version alongside the control lists (v19DrumVoices), the old blocks are read
// at their own size, and what does not fit is dropped and NAMED rather than
// discarded in silence.
//
// The block v24 itself adds is which drum each slot plays, since a slot's
// sound is no longer decided by its position. It is written with a leading
// count, so the next time the kit changes size this block can be fitted
// without a second frozen constant.
//
// v25 adds the Master page's bus effects: seventeen floats in
// project::BusFxControl order, behind a count of them for the same reason
// v24's block has one. Additive again, and older files read as a dry bus -
// which is not a guess at what they meant but exactly what they sounded like.
constexpr int currentPayloadVersion = 25;

// The four controls v19 wrote, in the order it wrote them. Frozen: this is a
// description of bytes already on disk, not of what a voice has.
constexpr project::DrumControl v19Controls[] = {
    project::DrumControl::level, project::DrumControl::tune,
    project::DrumControl::decay, project::DrumControl::pan
};

// The two v20 adds, likewise in the order it writes them.
constexpr project::DrumControl v20Controls[] = {
    project::DrumControl::send1, project::DrumControl::send2
};

// And the two v21 adds.
constexpr project::DrumControl v21Controls[] = {
    project::DrumControl::send1Amount, project::DrumControl::send2Amount
};

// And v22's one.
constexpr project::DrumControl v22Controls[] = {
    project::DrumControl::attack
};

// How many voices the kit had when versions 19 to 23 were written.
//
// FROZEN, exactly like the control lists above and for exactly the same
// reason: this describes bytes already on disk rather than what a kit has.
// numDrumVoices is twelve now, and reading a v23 file with twelve would take
// three quarters of every block and then read the NEXT block's bytes as the
// rest of this one. The file would load. The kit would be made of numbers
// that came from somewhere else, and nothing would say so.
//
// A file written from v24 on carries whatever numDrumVoices is, which is why
// there is no v24 entry here: the current count is the current count.
constexpr int v19DrumVoices = 16;
static_assert(v19DrumVoices >= project::numDrumVoices,
              "The kit has grown past what the frozen readers expect. A version that wrote "
              "MORE voices than v19-v23 did needs its own frozen count, not this one.");

// How many of an old file's voices this kit still has room for. The rest are
// read - they have to be, to keep the stream aligned - and then dropped.
constexpr int droppedV19Voices = v19DrumVoices - project::numDrumVoices;

static_assert(sizeof(v19Controls) / sizeof(v19Controls[0])
                  + sizeof(v20Controls) / sizeof(v20Controls[0])
                  + sizeof(v21Controls) / sizeof(v21Controls[0])
                  + sizeof(v22Controls) / sizeof(v22Controls[0])
                  == (size_t) project::numDrumControls,
              "Every drum control has to be written by exactly one version's block - a tenth "
              "that no block names would be saved nowhere and silently reset on every load.");

// Guards against a corrupt count asking for an enormous allocation.
// One per parameter id, which is what caps it - a file claiming more lanes
// than there are parameters is corrupt whatever else it says.
constexpr int32_t maxSavedLanes = paramreg::maxParameters;

// The same guard for an embedded sample. A file claiming a slot longer than
// the loader will ever produce is corrupt or hostile, and either way the
// answer is to stop rather than to allocate what it asked for.
//
// Against the highest rate anything is likely to be recorded at rather than
// against the rate in the file, because the rate in the file is equally
// untrusted.
constexpr int32_t maxSavedSampleFrames = (int32_t) (drums::maxSampleSeconds * 192000.0);

// The same guard for the drum pattern. A file claiming more voices than exist,
// or a row longer than the grid, is corrupt whatever else it says.
// The frozen count, not the current one. A project written when the kit had
// sixteen rows is not corrupt for saying sixteen, and rejecting it outright
// would be the harshest possible reading of "this file is from before".
// Rows past what the kit still has are dropped on the way in, and named.
constexpr int32_t maxSavedDrumVoices = v19DrumVoices;
constexpr int32_t maxSavedDrumCells = project::maxDrumCells;
// CcnK(4)+byteSize(4)+FPCh(4)+version(4)+fxID(4)+fxVersion(4)+numParams(4)+prgName(28)+chunkSize(4) + our chunk data
constexpr int minimumValidFxpSize = 4 + 4 + 4 + 4 + 4 + 4 + 4 + 28 + 4 + v1ChunkDataSize;

// Guard against a corrupt/hostile count causing a huge allocation.
constexpr int32_t maxWavetableFloats = 64 * 4096;

// Reads one version's block of per-voice controls, at the size that version
// wrote it, and keeps what this kit has room for.
//
// The voices past the end are still READ - the stream has to stay aligned, and
// skipping them would leave every later block reading somebody else's bytes -
// and then thrown away. Their MIXER settings are not worth reporting: a level
// and a pan on a row with no hits on it is not work anybody did, and saying
// "you have lost the Maraca's pan" about a row nobody touched would be noise.
// What is worth reporting is hits and samples, and those are handled where
// they are read.
void readVoiceControls(juce::InputStream& in, PresetValues& v, int voicesInFile,
                       const project::DrumControl* controls, size_t numControls)
{
    for (int voice = 0; voice < voicesInFile; ++voice)
    {
        for (size_t c = 0; c < numControls; ++c)
        {
            const auto value = in.readFloatBigEndian();

            if (voice < project::numDrumVoices)
                v.drumControls[(size_t) project::drumControlIndex(voice, controls[c])] = value;
        }
    }
}

template <size_t N>
void readVoiceControls(juce::InputStream& in, PresetValues& v, int voicesInFile,
                       const project::DrumControl (&controls)[N])
{
    readVoiceControls(in, v, voicesInFile, controls, N);
}

void writeTag(juce::OutputStream& os, const char* fourCC) { os.write(fourCC, 4); }

juce::String readTag(juce::InputStream& is)
{
    char buf[4] = {};
    is.read(buf, 4);
    return juce::String(buf, 4);
}

void writePrgName(juce::OutputStream& os, const juce::String& name)
{
    char buf[28] = {};
    auto utf8 = name.toRawUTF8();
    auto len = juce::jmin((size_t) 27, std::strlen(utf8));
    std::memcpy(buf, utf8, len);
    os.write(buf, sizeof(buf));
}

juce::String readPrgName(juce::InputStream& is)
{
    char buf[29] = {};
    is.read(buf, 28);
    buf[28] = 0;
    return juce::String(juce::CharPointer_UTF8(buf));
}
} // namespace

PresetValues FxpPreset::fromSynthParameters(const SynthParameters& params, const juce::String& name)
{
    PresetValues v;
    v.name = name;
    v.wavePosition = params.wavePosition.load();
    v.attack = params.attack.load();
    v.decay = params.decay.load();
    v.sustain = params.sustain.load();
    v.release = params.release.load();
    v.filterCutoffHz = params.filterCutoffHz.load();
    v.filterResonance = params.filterResonance.load();
    v.lfoRateHz = params.lfoRateHz.load();
    v.lfoDepth = params.lfoDepth.load();
    v.lfoAmpDepth = params.lfoAmpDepth.load();
    v.unisonVoices = params.unisonVoices.load();
    v.unisonDetuneCents = params.unisonDetuneCents.load();
    v.unisonWidth = params.unisonWidth.load();
    v.driveAmount = params.driveAmount.load();
    v.masterGain = params.masterGain.load();
    v.stereoLfo = params.stereoLfo.load();
    v.fxDistortion = params.fxDistortion.load();
    v.delayTimeMs = params.delayTimeMs.load();
    v.delayFeedback = params.delayFeedback.load();
    v.delayMix = params.delayMix.load();
    v.reverbSize = params.reverbSize.load();
    v.reverbMix = params.reverbMix.load();
    v.lfoToWave = params.lfoToWave.load();
    v.modEnvAttack = params.modEnvAttack.load();
    v.modEnvDecay = params.modEnvDecay.load();
    v.modEnvSustain = params.modEnvSustain.load();
    v.modEnvRelease = params.modEnvRelease.load();
    v.modEnvToWave = params.modEnvToWave.load();
    v.modEnvToCutoff = params.modEnvToCutoff.load();
    v.lfo2RateHz = params.lfo2RateHz.load();
    v.lfo2ToWave = params.lfo2ToWave.load();
    v.lfo2ToCutoff = params.lfo2ToCutoff.load();
    v.filterType = params.filterType.load();
    v.osc1Level = params.osc1Level.load();
    v.osc2Level = params.osc2Level.load();
    v.osc2WavePosition = params.osc2WavePosition.load();
    v.osc2Semitones = params.osc2Semitones.load();
    v.osc2Fine = params.osc2Fine.load();
    v.subLevel = params.subLevel.load();
    v.subWave = params.subWave.load();
    v.subOctave = params.subOctave.load();
    v.noiseLevel = params.noiseLevel.load();
    v.noiseColour = params.noiseColour.load();

    v.arpEnabled = params.arpEnabled.load();
    v.arpMode = params.arpMode.load();
    v.arpPattern = params.arpPattern.load();
    v.arpRateHz = params.arpRateHz.load();
    v.arpOctaves = params.arpOctaves.load();
    v.arpGate = params.arpGate.load();
    v.arpSwing = params.arpSwing.load();
    v.arpSync = params.arpSync.load();
    v.delaySync = params.delaySync.load();
    v.lfo1Sync = params.lfo1Sync.load();
    v.lfo2Sync = params.lfo2Sync.load();
    v.bpm = params.bpm.load();
    v.midiClockSync = params.midiClockSync.load();

    v.masteringEnabled = params.masteringEnabled.load();
    v.masterSatDrive = params.masterSatDrive.load();
    v.masterSatMix = params.masterSatMix.load();
    v.masterCompThresholdDb = params.masterCompThresholdDb.load();
    v.masterCompRatio = params.masterCompRatio.load();
    v.masterCompAttackMs = params.masterCompAttackMs.load();
    v.masterCompReleaseMs = params.masterCompReleaseMs.load();
    v.masterCompMakeupDb = params.masterCompMakeupDb.load();
    v.masterCompMix = params.masterCompMix.load();
    v.masterLimitCeilingDb = params.masterLimitCeilingDb.load();
    v.masterLimitReleaseMs = params.masterLimitReleaseMs.load();
    v.masterFxGainDb = params.masterFxGainDb.load();

    for (size_t i = 0; i < v.drumControls.size(); ++i)
        v.drumControls[i] = params.drumControls[i].load();

    for (size_t i = 0; i < v.drumFx.size(); ++i)
        v.drumFx[i] = params.drumFx[i].load();

    for (size_t i = 0; i < v.busFx.size(); ++i)
        v.busFx[i] = params.busFx[i].load();

    v.velocityToCutoff = params.velocityToCutoff.load();
    v.velocityToResonance = params.velocityToResonance.load();
    v.accentAmount = params.accentAmount.load();
    v.velocityToWave = params.velocityToWave.load();
    v.velocityToAmp = params.velocityToAmp.load();
    v.wheelToCutoff = params.wheelToCutoff.load();
    v.wheelToWave = params.wheelToWave.load();
    v.pressureToCutoff = params.pressureToCutoff.load();

    v.glideTimeMs = params.glideTimeMs.load();
    v.monoMode = params.monoMode.load();
    v.legatoMode = params.legatoMode.load();
    v.chorusRate = params.chorusRate.load();
    v.chorusDepth = params.chorusDepth.load();
    v.chorusMix = params.chorusMix.load();
    v.phaserRate = params.phaserRate.load();
    v.phaserFeedback = params.phaserFeedback.load();
    v.phaserMix = params.phaserMix.load();
    v.eqLowGain = params.eqLowGain.load();
    v.eqMidGain = params.eqMidGain.load();
    v.eqHighGain = params.eqHighGain.load();

    v.arpDivision = params.arpDivision.load();
    v.arpRateLink = params.arpRateLink.load();
    v.seqDivision = params.seqDivision.load();
    v.seqLength = params.seqLength.load();
    v.seqRootNote = params.seqRootNote.load();
    v.seqRecordNotes = params.seqRecordNotes.load();
    v.seqQuantise = params.seqQuantise.load();

    v.musicalKey = params.musicalKey.load();
    v.musicalScale = params.musicalScale.load();
    v.chordMode = params.chordMode.load();
    return v;
}

void FxpPreset::applyToSynthParameters(const PresetValues& values, SynthParameters& params,
                                       ApplyScope scope)
{
    params.wavePosition = values.wavePosition;
    params.attack = values.attack;
    params.decay = values.decay;
    params.sustain = values.sustain;
    params.release = values.release;
    params.filterCutoffHz = values.filterCutoffHz;
    params.filterResonance = values.filterResonance;
    params.lfoRateHz = values.lfoRateHz;
    params.lfoDepth = values.lfoDepth;
    params.lfoAmpDepth = values.lfoAmpDepth;
    params.unisonVoices = values.unisonVoices;
    params.unisonDetuneCents = values.unisonDetuneCents;
    params.unisonWidth = values.unisonWidth;
    params.driveAmount = values.driveAmount;
    params.masterGain = values.masterGain;
    params.stereoLfo = values.stereoLfo;
    params.fxDistortion = values.fxDistortion;
    params.delayTimeMs = values.delayTimeMs;
    params.delayFeedback = values.delayFeedback;
    params.delayMix = values.delayMix;
    params.reverbSize = values.reverbSize;
    params.reverbMix = values.reverbMix;
    params.lfoToWave = values.lfoToWave;
    params.modEnvAttack = values.modEnvAttack;
    params.modEnvDecay = values.modEnvDecay;
    params.modEnvSustain = values.modEnvSustain;
    params.modEnvRelease = values.modEnvRelease;
    params.modEnvToWave = values.modEnvToWave;
    params.modEnvToCutoff = values.modEnvToCutoff;
    params.lfo2RateHz = values.lfo2RateHz;
    params.lfo2ToWave = values.lfo2ToWave;
    params.lfo2ToCutoff = values.lfo2ToCutoff;
    params.filterType = values.filterType;
    params.osc1Level = values.osc1Level;
    params.osc2Level = values.osc2Level;
    params.osc2WavePosition = values.osc2WavePosition;
    params.osc2Semitones = values.osc2Semitones;
    params.osc2Fine = values.osc2Fine;
    params.subLevel = values.subLevel;
    params.subWave = values.subWave;
    params.subOctave = values.subOctave;
    params.noiseLevel = values.noiseLevel;
    params.noiseColour = values.noiseColour;

    params.arpEnabled = values.arpEnabled;
    params.arpMode = values.arpMode;
    params.arpPattern = values.arpPattern;
    params.arpRateHz = values.arpRateHz;
    params.arpOctaves = values.arpOctaves;
    params.arpGate = values.arpGate;
    params.arpSwing = values.arpSwing;
    params.arpSync = values.arpSync;
    params.delaySync = values.delaySync;
    params.lfo1Sync = values.lfo1Sync;
    params.lfo2Sync = values.lfo2Sync;
    const bool project = scope == ApplyScope::project;

    // The tempo and the mastering chain are the project's. Both used to come
    // back with every patch: the tempo because in the standalone the patch
    // once WAS the session, and the mastering chain because it was added when
    // it still was. Choosing a lead sound should not move the transport or
    // reset the Master page, and neither does now.
    if (project)
    {
        params.bpm = values.bpm;
        params.midiClockSync = values.midiClockSync;

        params.masteringEnabled = values.masteringEnabled;
        params.masterSatDrive = values.masterSatDrive;
        params.masterSatMix = values.masterSatMix;
        params.masterCompThresholdDb = values.masterCompThresholdDb;
        params.masterCompRatio = values.masterCompRatio;
        params.masterCompAttackMs = values.masterCompAttackMs;
        params.masterCompReleaseMs = values.masterCompReleaseMs;
        params.masterCompMakeupDb = values.masterCompMakeupDb;
        params.masterCompMix = values.masterCompMix;
        params.masterLimitCeilingDb = values.masterLimitCeilingDb;
        params.masterLimitReleaseMs = values.masterLimitReleaseMs;
        params.masterFxGainDb = values.masterFxGainDb;
    }

    params.velocityToCutoff = values.velocityToCutoff;
    params.velocityToResonance = values.velocityToResonance;
    params.accentAmount = values.accentAmount;
    params.velocityToWave = values.velocityToWave;
    params.velocityToAmp = values.velocityToAmp;
    params.wheelToCutoff = values.wheelToCutoff;
    params.wheelToWave = values.wheelToWave;
    params.pressureToCutoff = values.pressureToCutoff;

    params.glideTimeMs = values.glideTimeMs;
    params.monoMode = values.monoMode;
    params.legatoMode = values.legatoMode;
    params.chorusRate = values.chorusRate;
    params.chorusDepth = values.chorusDepth;
    params.chorusMix = values.chorusMix;
    params.phaserRate = values.phaserRate;
    params.phaserFeedback = values.phaserFeedback;
    params.phaserMix = values.phaserMix;
    params.eqLowGain = values.eqLowGain;
    params.eqMidGain = values.eqMidGain;
    params.eqHighGain = values.eqHighGain;

    params.arpDivision = values.arpDivision;
    params.arpRateLink = values.arpRateLink;

    // Only a project, and only when it actually brings a pattern with it.
    // StepSequencer::restoreState already refuses an empty state, so a file
    // with no pattern would otherwise leave your bars where they were but reset
    // the five settings that decide how they play: a 32-step pattern came back
    // 64 steps long, at the struct's default rate and root note. Half a
    // sequencer restored is worse than none, so the two halves travel together
    // or not at all.
    if (project && ! values.sequencer.isEmpty())
    {
        params.seqDivision = values.seqDivision;
        params.seqLength = values.seqLength;
        params.seqRootNote = values.seqRootNote;
        params.seqRecordNotes = values.seqRecordNotes;
        params.seqQuantise = values.seqQuantise;
    }

    // The drum mixer, with a project only - but NOT gated on the pattern being
    // non-empty the way the sequencer settings are. Those five are meaningless
    // without bars to apply them to; a kit tuned before a single hit has been
    // drawn is an ordinary thing to have saved, and dropping it would be the
    // surprise, not the protection.
    if (project)
    {
        for (size_t i = 0; i < values.drumControls.size(); ++i)
            params.drumControls[i].store(values.drumControls[i]);

        for (size_t i = 0; i < values.drumFx.size(); ++i)
            params.drumFx[i].store(values.drumFx[i]);

        // The mix bus, on the project flag with the kit. The bus is where the
        // drums and the synth meet, so a patch that rewrote it would be
        // reaching into the beat - "Bright Lead" flattening the room the kit
        // is in is the same surprise the drum rack is gated against.
        for (size_t i = 0; i < values.busFx.size(); ++i)
            params.busFx[i].store(values.busFx[i]);
    }

    // Play in Key, with the sound - see ApplyScope for why this side.
    params.musicalKey = values.musicalKey;
    params.musicalScale = values.musicalScale;
    params.chordMode = values.chordMode;
}

PresetValues FxpPreset::synthPresetOnly(const PresetValues& everything)
{
    // Everything, then the project's part of it put back to a fresh file's.
    // Listed here field by field rather than derived, and held to
    // applyToSynthParameters's own list by DrumPatternTest - a field that ends
    // up on one list and not the other is either a synth preset carrying a
    // piece of your project, or a project load that forgets something.
    PresetValues synth = everything;
    const PresetValues fresh;

    // The transport.
    synth.bpm = fresh.bpm;
    synth.midiClockSync = fresh.midiClockSync;

    // The Master page's mastering chain.
    synth.masteringEnabled = fresh.masteringEnabled;
    synth.masterSatDrive = fresh.masterSatDrive;
    synth.masterSatMix = fresh.masterSatMix;
    synth.masterCompThresholdDb = fresh.masterCompThresholdDb;
    synth.masterCompRatio = fresh.masterCompRatio;
    synth.masterCompAttackMs = fresh.masterCompAttackMs;
    synth.masterCompReleaseMs = fresh.masterCompReleaseMs;
    synth.masterCompMakeupDb = fresh.masterCompMakeupDb;
    synth.masterCompMix = fresh.masterCompMix;
    synth.masterLimitCeilingDb = fresh.masterLimitCeilingDb;
    synth.masterLimitReleaseMs = fresh.masterLimitReleaseMs;
    synth.masterFxGainDb = fresh.masterFxGainDb;

    // The Master page's bus effects.
    synth.busFx = fresh.busFx;

    // The Sequencer page: the pattern, its automation, and how it plays.
    synth.sequencer = fresh.sequencer;
    synth.seqDivision = fresh.seqDivision;
    synth.seqLength = fresh.seqLength;
    synth.seqRootNote = fresh.seqRootNote;
    synth.seqRecordNotes = fresh.seqRecordNotes;
    synth.seqQuantise = fresh.seqQuantise;

    // The Drums page: mixer, rack, which drum is on each slot, and the samples
    // - which are what would make a synth preset megabytes.
    synth.drumControls = fresh.drumControls;
    synth.drumFx = fresh.drumFx;
    synth.drumEngines = fresh.drumEngines;
    synth.drumSamples = fresh.drumSamples;
    synth.droppedDrumRows = fresh.droppedDrumRows;

    // The desk.
    synth.midiMap = fresh.midiMap;
    synth.keyboardChannel = fresh.keyboardChannel;

    return synth;
}

void FxpPreset::writeToMemory(const PresetValues& values, juce::MemoryBlock& destination)
{
    juce::MemoryOutputStream body;

    // -- our own opaque preset payload --
    writeTag(body, "WFpr");
    body.writeIntBigEndian(currentPayloadVersion);

    for (float f : {values.wavePosition, values.attack, values.decay, values.sustain, values.release,
                     values.filterCutoffHz, values.filterResonance, values.lfoRateHz, values.lfoDepth,
                     values.masterGain, values.lfoAmpDepth, values.unisonVoices, values.unisonDetuneCents,
                     values.unisonWidth, values.driveAmount})
        body.writeFloatBigEndian(f);

    for (float f : {values.stereoLfo, values.fxDistortion, values.delayTimeMs, values.delayFeedback,
                     values.delayMix, values.reverbSize, values.reverbMix})
        body.writeFloatBigEndian(f);

    for (float f : {values.lfoToWave, values.modEnvAttack, values.modEnvDecay, values.modEnvSustain,
                     values.modEnvRelease, values.modEnvToWave, values.modEnvToCutoff, values.lfo2RateHz,
                     values.lfo2ToWave, values.lfo2ToCutoff})
        body.writeFloatBigEndian(f);

    for (float f : {values.filterType, values.osc1Level, values.osc2Level, values.osc2WavePosition,
                     values.osc2Semitones, values.osc2Fine, values.subLevel, values.subWave, values.subOctave,
                     values.noiseLevel, values.noiseColour})
        body.writeFloatBigEndian(f);

    for (float f : {values.arpEnabled, values.arpMode, values.arpPattern, values.arpRateHz, values.arpOctaves,
                     values.arpGate, values.arpSwing, values.arpSync, values.delaySync, values.lfo1Sync,
                     values.lfo2Sync, values.bpm, values.midiClockSync, values.musicalKey, values.musicalScale,
                     values.chordMode})
        body.writeFloatBigEndian(f);

    for (float f : {values.arpDivision, values.arpRateLink, values.seqDivision, values.seqLength,
                     values.seqRootNote, values.seqRecordNotes, values.seqQuantise})
        body.writeFloatBigEndian(f);

    for (float f : {values.glideTimeMs, values.monoMode, values.legatoMode, values.chorusRate,
                     values.chorusDepth, values.chorusMix, values.phaserRate, values.phaserFeedback,
                     values.phaserMix, values.eqLowGain, values.eqMidGain, values.eqHighGain})
        body.writeFloatBigEndian(f);

    for (float f : {values.velocityToCutoff, values.velocityToWave, values.velocityToAmp,
                     values.wheelToCutoff, values.wheelToWave, values.pressureToCutoff})
        body.writeFloatBigEndian(f);

    for (float f : {values.velocityToResonance, values.accentAmount})
        body.writeFloatBigEndian(f);

    // v16: the mastering chain, in the order the signal meets it.
    for (float f : {values.masteringEnabled,
                    values.masterSatDrive, values.masterSatMix,
                    values.masterCompThresholdDb, values.masterCompRatio,
                    values.masterCompAttackMs, values.masterCompReleaseMs,
                    values.masterCompMakeupDb, values.masterCompMix,
                    values.masterLimitCeilingDb, values.masterLimitReleaseMs,
                    values.masterFxGainDb})
        body.writeFloatBigEndian(f);

    body.writeIntBigEndian((int) values.wavetableData.size());
    for (float f : values.wavetableData)
        body.writeFloatBigEndian(f);

    jassert(body.getDataSize() == (size_t) (v16HeaderSize + values.wavetableData.size() * sizeof(float)));

    // The sequencer goes last, after the wavetable, because it is the only
    // part whose length is not known from the header alone.
    body.writeIntBigEndian((int) values.sequencer.steps.size());
    for (const auto& step : values.sequencer.steps)
    {
        body.writeIntBigEndian(step.active);
        body.writeIntBigEndian(step.degree);
        body.writeIntBigEndian(step.octave);
        body.writeIntBigEndian(step.length);
        body.writeIntBigEndian(step.tickOffset);
        body.writeIntBigEndian(step.locked);
        body.writeFloatBigEndian(step.velocity);
        body.writeFloatBigEndian(step.gate);
        body.writeFloatBigEndian(step.probability);
        body.writeIntBigEndian(step.slide);
        body.writeIntBigEndian(step.accent);
    }

    body.writeIntBigEndian((int) values.sequencer.lanes.size());
    for (const auto& lane : values.sequencer.lanes)
    {
        body.writeIntBigEndian(lane.parameterId);
        body.writeIntBigEndian((int) lane.values.size());
        for (float f : lane.values)
            body.writeFloatBigEndian(f);
    }


    // v14: which hardware control moves which dial. Length-prefixed rather
    // than terminated, so an empty map is an explicit zero and the reader never
    // has to guess where it ended.
    auto mapText = values.midiMap.toStdString();
    body.writeIntBigEndian((int) mapText.size());
    if (!mapText.empty())
        body.write(mapText.data(), mapText.size());

    // v15: the channel the keyboard was listening on. Alongside the map above
    // rather than in the header, because the two are one thing - which MIDI
    // reached the synth - and they are offered together when a project opens.
    body.writeIntBigEndian(values.keyboardChannel);

    // v17: the drum pattern, as a count of voices and then one row each. Only
    // voices with something on them are here, so a project with no beat in it
    // writes a single zero.
    //
    // Each row carries its own voice index rather than being positional. A row
    // is identified by the voice it belongs to, and a file that listed rows in
    // order would repoint the whole pattern the first time a voice was skipped.
    body.writeIntBigEndian((int) values.sequencer.drumVoices.size());
    for (const auto& voice : values.sequencer.drumVoices)
    {
        body.writeIntBigEndian(voice.voice);
        body.writeIntBigEndian((int) voice.cells.size());

        for (const auto& cell : voice.cells)
        {
            body.writeIntBigEndian(cell.active);
            body.writeFloatBigEndian(cell.velocity);
            body.writeFloatBigEndian(cell.probability);
            body.writeIntBigEndian(cell.accent);
            body.writeIntBigEndian(cell.nudge); // v18
        }
    }

    // v19: the drum mixer's original four controls per voice, voice-major.
    // Always exactly sixty-four floats, so there is no count in front of it.
    for (int voice = 0; voice < project::numDrumVoices; ++voice)
        for (const auto control : v19Controls)
            body.writeFloatBigEndian(
                values.drumControls[(size_t) project::drumControlIndex(voice, control)]);

    // v20: the two sends per voice, then the rack. Thirty-two floats and
    // twelve, both fixed for the same reason v19's sixty-four are.
    for (int voice = 0; voice < project::numDrumVoices; ++voice)
        for (const auto control : v20Controls)
            body.writeFloatBigEndian(
                values.drumControls[(size_t) project::drumControlIndex(voice, control)]);

    for (float f : values.drumFx)
        body.writeFloatBigEndian(f);

    // v21: the two send amounts per voice.
    for (int voice = 0; voice < project::numDrumVoices; ++voice)
        for (const auto control : v21Controls)
            body.writeFloatBigEndian(
                values.drumControls[(size_t) project::drumControlIndex(voice, control)]);

    // v22: Attack.
    for (int voice = 0; voice < project::numDrumVoices; ++voice)
        for (const auto control : v22Controls)
            body.writeFloatBigEndian(
                values.drumControls[(size_t) project::drumControlIndex(voice, control)]);

    // v23: the sample slots, audio and all. Sparse - a count and then only
    // the slots with something in them.
    {
        int loadedSlots = 0;

        for (const auto& slot : values.drumSamples)
            if (slot != nullptr && !slot->isEmpty())
                ++loadedSlots;

        body.writeIntBigEndian(loadedSlots);

        for (int voice = 0; voice < project::numDrumVoices; ++voice)
        {
            const auto& slot = values.drumSamples[(size_t) voice];

            if (slot == nullptr || slot->isEmpty())
                continue;

            body.writeIntBigEndian(voice);
            body.writeString(slot->name);
            body.writeDoubleBigEndian(slot->sourceRate);
            body.writeIntBigEndian(slot->length());

            for (const auto value : slot->data)
                body.writeFloatBigEndian(value);
        }
    }

    // v24: which drum each slot is set to.
    //
    // A count first, then that many ints, rather than a bare run of
    // numDrumVoices. The count is what lets a kit that changes size again read
    // this block without a third frozen constant: whatever is here can be read
    // and then fitted, exactly as v19-v23's blocks are being fitted now.
    body.writeIntBigEndian(project::numDrumVoices);

    for (const auto engine : values.drumEngines)
        body.writeIntBigEndian(engine);

    // v25: the Master page's bus effects. Count-prefixed, as v24 is, so a
    // control added later is one more float after these rather than a frozen
    // constant and a version check to go with it.
    body.writeIntBigEndian(project::numBusFxControls);

    for (const auto value : values.busFx)
        body.writeFloatBigEndian(value);

    juce::MemoryOutputStream fxpBody;
    writeTag(fxpBody, "FPCh");
    fxpBody.writeIntBigEndian(1);     // version
    writeTag(fxpBody, "WvF1");        // fxID: identifies WaveLathe's own presets
    fxpBody.writeIntBigEndian(1);     // fxVersion
    fxpBody.writeIntBigEndian(0);     // numParams (unused for chunk-based presets)
    writePrgName(fxpBody, values.name);
    fxpBody.writeIntBigEndian((int) body.getDataSize());
    fxpBody.write(body.getData(), body.getDataSize());

    juce::MemoryOutputStream whole(destination, false);
    writeTag(whole, "CcnK");
    whole.writeIntBigEndian((int) fxpBody.getDataSize());
    whole.write(fxpBody.getData(), fxpBody.getDataSize());
}

bool FxpPreset::save(const juce::File& file, const PresetValues& values, juce::String& errorMessage)
{
    juce::MemoryBlock data;
    writeToMemory(values, data);

    if (file.existsAsFile() && !file.deleteFile())
    {
        errorMessage = "Could not overwrite existing file.";
        return false;
    }

    juce::FileOutputStream out(file);
    if (!out.openedOk())
    {
        errorMessage = "Could not open file for writing.";
        return false;
    }

    out.write(data.getData(), data.getSize());
    out.flush();

    return true;
}

namespace
{
// Everything past opening the source, so a preset reads the same whether it
// came off disk or out of a host's saved session.
bool parseFxp(juce::InputStream& in, PresetValues& outValues, juce::String& errorMessage)
{
    if (readTag(in) != "CcnK")
    {
        errorMessage = "Not a valid FXP file (missing CcnK chunk header).";
        return false;
    }

    in.readIntBigEndian(); // byteSize, not needed for parsing

    auto fxMagic = readTag(in);
    if (fxMagic == "FxCk")
    {
        errorMessage = "This .fxp uses the plain-parameter format; WaveLathe only reads chunk-based presets.";
        return false;
    }
    if (fxMagic != "FPCh")
    {
        errorMessage = "Unrecognized .fxp format.";
        return false;
    }

    in.readIntBigEndian(); // version

    if (readTag(in) != "WvF1")
    {
        errorMessage = "This preset was not created by WaveLathe, so its internal data can't be decoded.";
        return false;
    }

    in.readIntBigEndian(); // fxVersion
    in.readIntBigEndian(); // numParams

    auto name = readPrgName(in);
    auto chunkSize = in.readIntBigEndian();

    if (chunkSize < v1ChunkDataSize)
    {
        errorMessage = "Unrecognized WaveLathe preset payload size.";
        return false;
    }

    if (readTag(in) != "WFpr")
    {
        errorMessage = "Corrupt WaveLathe preset payload.";
        return false;
    }

    auto payloadVersion = in.readIntBigEndian();
    if (payloadVersion < 1 || payloadVersion > currentPayloadVersion)
    {
        errorMessage = "This preset was saved by a newer version of WaveLathe.";
        return false;
    }
    if ((payloadVersion == 1 && chunkSize != v1ChunkDataSize)
        || (payloadVersion == 2 && chunkSize != v2ChunkDataSize)
        || (payloadVersion == 3 && chunkSize < v3HeaderSize)
        || (payloadVersion == 4 && chunkSize < v4HeaderSize)
        || (payloadVersion == 5 && chunkSize < v5HeaderSize)
        || (payloadVersion == 6 && chunkSize < v6HeaderSize)
        || (payloadVersion == 7 && chunkSize < v7HeaderSize)
        || (payloadVersion == 8 && chunkSize < v8HeaderSize)
        || (payloadVersion == 9 && chunkSize < v9HeaderSize)
        || (payloadVersion == 10 && chunkSize < v10HeaderSize)
        || (payloadVersion == 11 && chunkSize < v10HeaderSize)
        // v12 added only per-step data, so it is still a v10-sized header. It
        // was missed when v12 went in, which left v12 files as the one version
        // with no size check at all.
        || (payloadVersion == 12 && chunkSize < v10HeaderSize)
        || (payloadVersion == 13 && chunkSize < v13HeaderSize)
        // v14 added the MIDI map, which lives past the header with the pattern,
        // so the fixed part it must contain is still a v13 header.
        || (payloadVersion == 14 && chunkSize < v13HeaderSize)
        || (payloadVersion == 15 && chunkSize < v13HeaderSize)
        || (payloadVersion == 16 && chunkSize < v16HeaderSize)
        // v17 and v18 added the drum pattern and then its sub-step cells, both
        // past the header, so the fixed part they must contain is still v16's.
        || (payloadVersion == 17 && chunkSize < v16HeaderSize)
        || (payloadVersion == 18 && chunkSize < v16HeaderSize)
        || (payloadVersion == 19 && chunkSize < v16HeaderSize)
        || (payloadVersion == 20 && chunkSize < v16HeaderSize)
        || (payloadVersion == 21 && chunkSize < v16HeaderSize)
        || (payloadVersion == 22 && chunkSize < v16HeaderSize)
        || (payloadVersion == 23 && chunkSize < v16HeaderSize))
    {
        errorMessage = "Corrupt WaveLathe preset payload (size/version mismatch).";
        return false;
    }

    PresetValues v;
    v.name = name;
    v.wavePosition = in.readFloatBigEndian();
    v.attack = in.readFloatBigEndian();
    v.decay = in.readFloatBigEndian();
    v.sustain = in.readFloatBigEndian();
    v.release = in.readFloatBigEndian();
    v.filterCutoffHz = in.readFloatBigEndian();
    v.filterResonance = in.readFloatBigEndian();
    v.lfoRateHz = in.readFloatBigEndian();
    v.lfoDepth = in.readFloatBigEndian();
    v.masterGain = in.readFloatBigEndian();

    if (payloadVersion >= 2)
    {
        v.lfoAmpDepth = in.readFloatBigEndian();
        v.unisonVoices = in.readFloatBigEndian();
        v.unisonDetuneCents = in.readFloatBigEndian();
        v.unisonWidth = in.readFloatBigEndian();
        v.driveAmount = in.readFloatBigEndian();
    }

    if (payloadVersion >= 4)
    {
        v.stereoLfo = in.readFloatBigEndian();
        v.fxDistortion = in.readFloatBigEndian();
        v.delayTimeMs = in.readFloatBigEndian();
        v.delayFeedback = in.readFloatBigEndian();
        v.delayMix = in.readFloatBigEndian();
        v.reverbSize = in.readFloatBigEndian();
        v.reverbMix = in.readFloatBigEndian();
    }

    if (payloadVersion >= 5)
    {
        v.lfoToWave = in.readFloatBigEndian();
        v.modEnvAttack = in.readFloatBigEndian();
        v.modEnvDecay = in.readFloatBigEndian();
        v.modEnvSustain = in.readFloatBigEndian();
        v.modEnvRelease = in.readFloatBigEndian();
        v.modEnvToWave = in.readFloatBigEndian();
        v.modEnvToCutoff = in.readFloatBigEndian();
        v.lfo2RateHz = in.readFloatBigEndian();
        v.lfo2ToWave = in.readFloatBigEndian();
        v.lfo2ToCutoff = in.readFloatBigEndian();
    }

    if (payloadVersion >= 6)
    {
        v.filterType = in.readFloatBigEndian();
        v.osc1Level = in.readFloatBigEndian();
        v.osc2Level = in.readFloatBigEndian();
        v.osc2WavePosition = in.readFloatBigEndian();
        v.osc2Semitones = in.readFloatBigEndian();
        v.osc2Fine = in.readFloatBigEndian();
        v.subLevel = in.readFloatBigEndian();
        v.subWave = in.readFloatBigEndian();
        v.subOctave = in.readFloatBigEndian();
        v.noiseLevel = in.readFloatBigEndian();
        v.noiseColour = in.readFloatBigEndian();
    }

    if (payloadVersion >= 7)
    {
        v.arpEnabled = in.readFloatBigEndian();
        v.arpMode = in.readFloatBigEndian();
        v.arpPattern = in.readFloatBigEndian();
        v.arpRateHz = in.readFloatBigEndian();
        v.arpOctaves = in.readFloatBigEndian();
        v.arpGate = in.readFloatBigEndian();
        v.arpSwing = in.readFloatBigEndian();
        v.arpSync = in.readFloatBigEndian();
        v.delaySync = in.readFloatBigEndian();
        v.lfo1Sync = in.readFloatBigEndian();
        v.lfo2Sync = in.readFloatBigEndian();
        v.bpm = in.readFloatBigEndian();
        v.midiClockSync = in.readFloatBigEndian();
        v.musicalKey = in.readFloatBigEndian();
        v.musicalScale = in.readFloatBigEndian();
        v.chordMode = in.readFloatBigEndian();
    }

    if (payloadVersion >= 8)
    {
        v.arpDivision = in.readFloatBigEndian();
        v.arpRateLink = in.readFloatBigEndian();
        v.seqDivision = in.readFloatBigEndian();
        v.seqLength = in.readFloatBigEndian();
        v.seqRootNote = in.readFloatBigEndian();
        v.seqRecordNotes = in.readFloatBigEndian();
        v.seqQuantise = in.readFloatBigEndian();
    }

    if (payloadVersion >= 9)
    {
        v.glideTimeMs = in.readFloatBigEndian();
        v.monoMode = in.readFloatBigEndian();
        v.legatoMode = in.readFloatBigEndian();
        v.chorusRate = in.readFloatBigEndian();
        v.chorusDepth = in.readFloatBigEndian();
        v.chorusMix = in.readFloatBigEndian();
        v.phaserRate = in.readFloatBigEndian();
        v.phaserFeedback = in.readFloatBigEndian();
        v.phaserMix = in.readFloatBigEndian();
        v.eqLowGain = in.readFloatBigEndian();
        v.eqMidGain = in.readFloatBigEndian();
        v.eqHighGain = in.readFloatBigEndian();
    }

    if (payloadVersion >= 10)
    {
        v.velocityToCutoff = in.readFloatBigEndian();
        v.velocityToWave = in.readFloatBigEndian();
        v.velocityToAmp = in.readFloatBigEndian();
        v.wheelToCutoff = in.readFloatBigEndian();
        v.wheelToWave = in.readFloatBigEndian();
        v.pressureToCutoff = in.readFloatBigEndian();
    }

    if (payloadVersion >= 13)
    {
        v.velocityToResonance = in.readFloatBigEndian();
        v.accentAmount = in.readFloatBigEndian();
    }

    // Anything older simply keeps the defaults in PresetValues, which have the
    // mastering stage switched off - so a patch written before this existed
    // sounds afterwards exactly as it sounded before.
    if (payloadVersion >= 16)
    {
        v.masteringEnabled = in.readFloatBigEndian();
        v.masterSatDrive = in.readFloatBigEndian();
        v.masterSatMix = in.readFloatBigEndian();
        v.masterCompThresholdDb = in.readFloatBigEndian();
        v.masterCompRatio = in.readFloatBigEndian();
        v.masterCompAttackMs = in.readFloatBigEndian();
        v.masterCompReleaseMs = in.readFloatBigEndian();
        v.masterCompMakeupDb = in.readFloatBigEndian();
        v.masterCompMix = in.readFloatBigEndian();
        v.masterLimitCeilingDb = in.readFloatBigEndian();
        v.masterLimitReleaseMs = in.readFloatBigEndian();
        v.masterFxGainDb = in.readFloatBigEndian();
    }

    if (payloadVersion >= 3)
    {
        auto wavetableFloats = in.readIntBigEndian();
        if (wavetableFloats < 0 || wavetableFloats > maxWavetableFloats)
        {
            errorMessage = "Preset declares an implausible wavetable size.";
            return false;
        }

        int headerSize = v3HeaderSize;
        if (payloadVersion >= 16)     headerSize = v16HeaderSize;
        else if (payloadVersion >= 13) headerSize = v13HeaderSize;
        else if (payloadVersion >= 10) headerSize = v10HeaderSize;
        else if (payloadVersion >= 9) headerSize = v9HeaderSize;
        else if (payloadVersion >= 8) headerSize = v8HeaderSize;
        else if (payloadVersion >= 7) headerSize = v7HeaderSize;
        else if (payloadVersion >= 6) headerSize = v6HeaderSize;
        else if (payloadVersion >= 5) headerSize = v5HeaderSize;
        else if (payloadVersion >= 4) headerSize = v4HeaderSize;

        auto expectedChunkSize = headerSize + wavetableFloats * (int) sizeof(float);

        // From v8 the sequencer follows the wavetable, so the header and the
        // wavetable are a minimum rather than the whole of it.
        bool sizeIsWrong = payloadVersion >= 8 ? chunkSize < expectedChunkSize
                                               : chunkSize != expectedChunkSize;
        if (sizeIsWrong)
        {
            errorMessage = "Corrupt WaveLathe preset payload (wavetable size mismatch).";
            return false;
        }

        v.wavetableData.resize((size_t) wavetableFloats);
        for (int i = 0; i < wavetableFloats; ++i)
            v.wavetableData[(size_t) i] = in.readFloatBigEndian();
    }

    if (payloadVersion >= 8)
    {
        int numSteps = in.readIntBigEndian();
        if (numSteps < 0 || numSteps > 1024)
        {
            errorMessage = "Preset declares an implausible sequencer length.";
            return false;
        }

        v.sequencer.steps.resize((size_t) numSteps);
        for (auto& step : v.sequencer.steps)
        {
            step.active = in.readIntBigEndian();
            step.degree = in.readIntBigEndian();
            step.octave = in.readIntBigEndian();
            step.length = in.readIntBigEndian();
            step.tickOffset = in.readIntBigEndian();
            step.locked = in.readIntBigEndian();
            step.velocity = in.readFloatBigEndian();
            step.gate = in.readFloatBigEndian();
            step.probability = in.readFloatBigEndian();

            // Slide arrived in v12. An older pattern has no slides in it, and
            // reading a field that was never written would take the stream out
            // of step with every note after it.
            step.slide = payloadVersion >= 12 ? in.readIntBigEndian() : 0;
            step.accent = payloadVersion >= 13 ? in.readIntBigEndian() : 0;

            // Before v11 a length counted whole steps. Scaling it up keeps an
            // older pattern sounding exactly as it did.
            if (payloadVersion < 11)
                step.length *= project::ticksPerStep;
        }

        int numLanes = in.readIntBigEndian();
        if (numLanes < 0 || numLanes > maxSavedLanes)
        {
            errorMessage = "Preset declares an implausible number of automation lanes.";
            return false;
        }

        v.sequencer.lanes.resize((size_t) numLanes);
        for (auto& lane : v.sequencer.lanes)
        {
            lane.parameterId = in.readIntBigEndian();
            int numValues = in.readIntBigEndian();
            if (numValues < 0 || numValues > maxWavetableFloats)
            {
                errorMessage = "Preset declares an implausible automation lane.";
                return false;
            }

            lane.values.resize((size_t) numValues);
            for (auto& value : lane.values)
                value = in.readFloatBigEndian();
        }
    }


    if (payloadVersion >= 14)
    {
        int mapLength = in.readIntBigEndian();
        if (mapLength < 0 || mapLength > 8192)
        {
            errorMessage = "Preset declares an implausible MIDI map.";
            return false;
        }

        if (mapLength > 0)
        {
            std::vector<char> bytes((size_t) mapLength + 1, '\0');
            in.read(bytes.data(), mapLength);
            v.midiMap = juce::String::fromUTF8(bytes.data(), mapLength);
        }
    }

    if (payloadVersion >= 15)
    {
        int channel = in.readIntBigEndian();

        // Anything outside 0-16 reads as "not recorded" rather than as omni,
        // so a damaged field cannot quietly widen a keyboard someone narrowed.
        v.keyboardChannel = (channel >= 0 && channel <= 16) ? channel : -1;
    }

    if (payloadVersion >= 17)
    {
        int numVoices = in.readIntBigEndian();
        if (numVoices < 0 || numVoices > maxSavedDrumVoices)
        {
            errorMessage = "Preset declares an implausible number of drum voices.";
            return false;
        }

        v.sequencer.drumVoices.resize((size_t) numVoices);
        for (auto& voice : v.sequencer.drumVoices)
        {
            voice.voice = in.readIntBigEndian();

            const int numEntries = in.readIntBigEndian();
            if (numEntries < 0 || numEntries > maxSavedDrumCells)
            {
                errorMessage = "Preset declares an implausible drum pattern length.";
                return false;
            }

            // A v17 row has one entry per STEP and no nudge field; a v18 row has
            // one per cell and five fields. Read into a flat list first, then
            // place them, so the two versions differ in one place rather than
            // throughout.
            std::vector<SequencerState::DrumCellData> entries((size_t) numEntries);
            for (auto& entry : entries)
            {
                entry.active = in.readIntBigEndian();
                entry.velocity = in.readFloatBigEndian();
                entry.probability = in.readFloatBigEndian();
                entry.accent = in.readIntBigEndian();
                entry.nudge = payloadVersion >= 18 ? in.readIntBigEndian() : 0;
            }

            if (payloadVersion >= 18)
            {
                voice.cells = std::move(entries);
            }
            else
            {
                // Spread: what was step i is now cell i * drumSubdivisions, and
                // the positions between the old steps are empty. Every hit lands
                // exactly where it used to, which is what a pattern written
                // before 32nds existed means.
                voice.cells.assign((size_t) project::maxDrumCells, {});

                for (size_t i = 0; i < entries.size(); ++i)
                {
                    const size_t cell = i * (size_t) project::drumSubdivisions;
                    if (cell < voice.cells.size())
                        voice.cells[cell] = entries[i];
                }
            }
        }

        // Rows the kit no longer has, dropped AFTER the whole block was read
        // so the stream stays aligned, and named on the way out.
        //
        // Only rows with a hit on them are worth naming. A project from the
        // sixteen-row kit has a row for Clave whether or not anybody ever put
        // anything on it, and "you have lost the Clave" about an empty row
        // would cry wolf on every single old project - which is the fastest
        // way to teach somebody to ignore the message that matters.
        const auto keep = [&v](const SequencerState::DrumVoiceData& row)
        {
            if (row.voice >= 0 && row.voice < project::numDrumVoices)
                return true;

            const auto used = std::any_of(row.cells.begin(), row.cells.end(),
                                          [](const SequencerState::DrumCellData& cell)
                                          { return cell.active != 0; });

            if (used)
                v.droppedDrumRows.addIfNotAlreadyThere(row.voice);

            return false;
        };

        auto& rows = v.sequencer.drumVoices;
        rows.erase(std::remove_if(rows.begin(), rows.end(),
                                  [&keep](const SequencerState::DrumVoiceData& row)
                                  { return !keep(row); }),
                   rows.end());
    }

    // v19: the drum mixer, last of all. Anything older keeps
    // defaultDrumControls(), which is an untouched mixer - which is what a
    // project written before the mixer existed had.
    //
    // The version guard is doing real work here, unlike the one on the drum
    // pattern: a stream past its end returns zero rather than failing, so
    // reading this from a v18 file would not error - it would quietly set
    // every level, tune, decay and pan to zero. Silent, and it would look
    // like the kit had stopped working.
    //
    // v19DrumVoices, not numDrumVoices: this loop reads bytes that are already
    // on disk, and there are sixteen voices' worth of them in every file from
    // v19 to v23 however many rows the kit has today.
    const auto voicesInFile = payloadVersion >= 24 ? project::numDrumVoices : v19DrumVoices;

    if (payloadVersion >= 19)
        readVoiceControls(in, v, voicesInFile, v19Controls);

    // v20: the sends and the rack. The same guard doing the same real work -
    // a v19 file read without it would arrive with every send pointed at Off
    // by accident rather than by default, and with a rack of zeroes: no
    // reverb return, no drive, and a delay time of nothing at all.
    if (payloadVersion >= 20)
    {
        readVoiceControls(in, v, voicesInFile, v20Controls);

        for (auto& control : v.drumFx)
            control = in.readFloatBigEndian();
    }

    // v21: the send amounts. Anything older keeps the default of zero, which
    // is not an accident of the stream returning zeros past its end - it is
    // the right answer twice over. A project written before the chain existed
    // had no effects on any voice, and zero is exactly "no effect".
    if (payloadVersion >= 21)
        readVoiceControls(in, v, voicesInFile, v21Controls);

    // v22: Attack, zero for anything older - and zero is the instant strike
    // those kits actually had, not a value chosen because the stream returns
    // zeros past its end.
    //
    // Unlike the v19 guard, this one currently has no observable effect, and
    // that was established by breaking it rather than assumed. Loosen it to
    // `>= 21` and every test still passes: a v21 file would read four bytes
    // past its end per voice, get zeros, and zero is the right answer anyway.
    // It is kept because it stops being harmless the moment a v23 block is
    // appended behind this one - at which point a file that skipped or
    // over-read here would misalign everything after it. Tightening it to
    // `>= 23` DOES fail, immediately and loudly, which is the direction that
    // can actually corrupt a load.
    if (payloadVersion >= 22)
        readVoiceControls(in, v, voicesInFile, v22Controls);

    // v23: the embedded samples. Every field is checked before it is used,
    // because every one of them came out of a file that may not have been
    // written by this program - a count of two billion frames is an
    // allocation request, not a project.
    if (payloadVersion >= 23)
    {
        const auto loadedSlots = in.readIntBigEndian();

        // Against what the FILE could hold, not what the kit has. A v23
        // project may legitimately carry sixteen samples.
        if (loadedSlots < 0 || loadedSlots > voicesInFile)
        {
            errorMessage = "This project claims " + juce::String(loadedSlots)
                           + " drum samples, which cannot be right.";
            return false;
        }

        for (int i = 0; i < loadedSlots; ++i)
        {
            const auto voice = in.readIntBigEndian();
            const auto slotName = in.readString();
            const auto rate = in.readDoubleBigEndian();
            const auto frames = in.readIntBigEndian();

            // Past the end of the FILE's kit is corrupt and is refused. Past
            // the end of THIS kit, but inside the file's, is an old project
            // carrying a sample on a row that has since gone - which is not
            // corruption and must not be treated as it. The audio is read
            // (the stream has to stay aligned) and then dropped, and the row
            // is named so the load can say what went.
            if (voice < 0 || voice >= voicesInFile)
            {
                errorMessage = "This project has a drum sample on voice " + juce::String(voice)
                               + ", which does not exist.";
                return false;
            }

            if (frames < 0 || frames > maxSavedSampleFrames)
            {
                errorMessage = "This project claims a drum sample of " + juce::String(frames)
                               + " samples, which cannot be right.";
                return false;
            }

            if (rate < 1000.0 || rate > 768000.0)
            {
                errorMessage = "This project claims a drum sample recorded at "
                               + juce::String(rate, 0) + " Hz, which cannot be right.";
                return false;
            }

            auto sample = std::make_shared<drums::Sample>();
            sample->name = slotName;
            sample->sourceRate = rate;
            sample->data.resize((size_t) frames);

            for (auto& value : sample->data)
                value = in.readFloatBigEndian();

            if (sample->isEmpty())
                continue;

            if (voice >= project::numDrumVoices)
            {
                v.droppedDrumRows.addIfNotAlreadyThere(voice);
                continue;
            }

            // A slot that read back empty is dropped rather than published.
            // An empty Sample and no Sample mean the same thing to the kit,
            // and carrying one around is a null check somebody has to keep
            // remembering.
            v.drumSamples[(size_t) voice] = std::move(sample);
        }
    }

    // v24: which drum each slot plays. Anything older gets the default
    // mapping, which is what those files meant - a slot WAS its position then,
    // and the first eleven engines are still at the positions they were at.
    if (payloadVersion >= 24)
    {
        const auto savedEngines = in.readIntBigEndian();

        if (savedEngines < 0 || savedEngines > drums::numEngines * 4)
        {
            errorMessage = "This project claims " + juce::String(savedEngines)
                           + " drum slots, which cannot be right.";
            return false;
        }

        for (int slot = 0; slot < savedEngines; ++slot)
        {
            const auto engine = in.readIntBigEndian();

            if (slot >= project::numDrumVoices)
                continue;

            // An engine this build does not have reads as blank rather than as
            // whatever happens to sit at that index. A file from a build with
            // more engines must not silently point a slot at the wrong drum.
            v.drumEngines[(size_t) slot]
                = (engine >= 0 && engine < drums::numEngines) ? engine : drums::noEngine;
        }
    }

    // v25: the bus effects. Anything older keeps the dry, flat, unity bus the
    // struct starts with, which is what a project with no bus sounded like.
    if (payloadVersion >= 25)
    {
        const auto savedControls = in.readIntBigEndian();

        // Generous, because a later build may add controls - but not a count
        // somebody could use to have this read a gigabyte of floats.
        if (savedControls < 0 || savedControls > project::numBusFxControls * 8)
        {
            errorMessage = "This project claims " + juce::String(savedControls)
                           + " bus effect settings, which cannot be right.";
            return false;
        }

        for (int i = 0; i < savedControls; ++i)
        {
            const auto value = in.readFloatBigEndian();

            // A control this build does not know is read and skipped, so the
            // stream stays aligned for whatever is written after it. And a
            // value that is not a number is left at its default rather than
            // handed to a DSP chain that will spread it through every sample.
            if (i < project::numBusFxControls && std::isfinite(value))
                v.busFx[(size_t) i] = value;
        }
    }

    outValues = v;
    return true;
}
} // namespace

bool FxpPreset::load(const juce::File& file, PresetValues& outValues, juce::String& errorMessage)
{
    if (!file.existsAsFile() || file.getSize() < minimumValidFxpSize)
    {
        errorMessage = "File is missing or too small to be a valid WaveLathe .fxp preset.";
        return false;
    }

    juce::FileInputStream in(file);
    if (!in.openedOk())
    {
        errorMessage = "Could not open file for reading.";
        return false;
    }

    return parseFxp(in, outValues, errorMessage);
}

bool FxpPreset::readFromMemory(const void* data, int sizeInBytes, PresetValues& outValues,
                               juce::String& errorMessage)
{
    if (data == nullptr || sizeInBytes < minimumValidFxpSize)
    {
        errorMessage = "Saved state is missing or too small to be a WaveLathe project.";
        return false;
    }

    juce::MemoryInputStream in(data, (size_t) sizeInBytes, false);
    return parseFxp(in, outValues, errorMessage);
}
} // namespace wavelathe
