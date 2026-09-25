// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "ParameterRegistry.h"

#include <array>
#include <cmath>
#include <memory>
#include <string>

namespace wavelathe
{
namespace paramreg
{
namespace
{
// The section each parameter belongs to, in the order the panel reads. Kept
// here rather than in the UI because the registry is the one place that knows
// what every id IS - and because more than one list needs the grouping now
// that there are sixty of them rather than thirty.
const char* const oscillator = "Oscillators";
const char* const filter = "Filter & Drive";
const char* const envelope = "Envelopes";
const char* const lfo = "LFOs";
const char* const effects = "Effects";
const char* const playing = "Playing";
const char* const arp = "Arpeggiator";

// Names for the positions of the four parameters that have positions rather
// than a range. These are the panel's own words, kept here so that the dial and
// the host's automation lane say the same thing about the same step.
const char* const filterTypeLabels[] = {"LP 12", "LP 24", "HP 12", "BP 12", "Notch"};
const char* const subOctaveLabels[] = {"-1 oct", "-2 oct"};

// The drum mixer's names, kept alive for as long as the registry is.
//
// A Descriptor holds a const char*, and the hundred and eight drum names are built
// rather than written as literals, so something has to own the characters.
// This does, for the life of the program - which is the same life the registry
// itself has, since it is a function-local static.
const char* ownedName(const juce::String& text)
{
    static std::vector<std::unique_ptr<std::string>> owned;
    owned.push_back(std::make_unique<std::string>(text.toStdString()));
    return owned.back()->c_str();
}

// Nine groups of twelve rather than twelve of nine.
//
// Both readings are defensible - a mixer is voice-major, and these ids are
// voice-major so a strip is nine in a row. But the lists this grouping feeds
// are the add-lane menu and the MIDI map, and what somebody wants there is
// nearly always a LEVEL. One hop to "Drum Level" and then the voice beats
// scanning twelve submenus for the one control type you were after.
//
// One group per control rather than per pair, which is why the selector and
// its amount are "Drum FX 1" and "Drum Amt 1" rather than both under FX 1.
// Pairing them would make one group of twenty-four in which every voice
// appears twice, and the first hop is meant to halve the problem.
const char* groupForDrumControl(project::DrumControl control)
{
    switch (control)
    {
        case project::DrumControl::level:       return "Drum Level";
        case project::DrumControl::tune:        return "Drum Tune";
        case project::DrumControl::attack:      return "Drum Attack";
        case project::DrumControl::decay:       return "Drum Decay";
        case project::DrumControl::pan:         return "Drum Pan";
        case project::DrumControl::send1:       return "Drum FX 1";
        case project::DrumControl::send1Amount: return "Drum Amt 1";
        case project::DrumControl::send2:       return "Drum FX 2";
        case project::DrumControl::send2Amount: return "Drum Amt 2";
    }

    // Unreachable while the switch is exhaustive, which is the point of not
    // having a default: a tenth control is a compiler error here rather than
    // twelve dials quietly filed under Level.
    return "Drum Level";
}

// What the five positions of a send selector are called, in the layout a
// Descriptor wants: a flat array indexed from minValue, which is 0 and is Off.
//
// Built from project::drumSendName rather than written out again, so the dial,
// the host's automation lane and the rack down the side of the Drums page
// cannot end up disagreeing about which unit position 3 is.
const char* const* drumSendLabels()
{
    static const auto labels = []
    {
        std::array<const char*, project::numDrumSendDestinations> built{};

        for (int i = 0; i < project::numDrumSendDestinations; ++i)
            built[(size_t) i] = project::drumSendName(i);

        return built;
    }();

    return labels.data();
}

void appendDrumMixer(std::vector<Descriptor>& registry)
{
    using DC = project::DrumControl;

    // The range a control is STORED in, which is the range the dial on the
    // panel uses and the range the host sees. Four of the nine are not 0 to 1:
    // Tune is in semitones, Pan runs left to right, and the two selectors count
    // units. SynthParameters::drumControls holds real values rather than
    // normalised ones, so these are the numbers actually in the array - and the
    // kit's own 0-to-1 Tune and Pan are converted where they are read, which is
    // WaveLatheProcessor::updateDrumParameters and nowhere else.
    struct Range
    {
        float minValue, maxValue;
        int steps = 0;
        const char* const* stepLabels = nullptr;
    };

    const auto rangeOf = [](DC control) -> Range
    {
        switch (control)
        {
            case DC::level:       return { 0.0f, 1.0f };
            case DC::attack:      return { 0.0f, 1.0f };
            case DC::decay:       return { 0.0f, 1.0f };
            case DC::send1Amount: return { 0.0f, 1.0f };
            case DC::send2Amount: return { 0.0f, 1.0f };

            // An octave either way, which is what tuneRatioFor reaches.
            case DC::tune:        return { -12.0f, 12.0f };

            case DC::pan:         return { -1.0f, 1.0f };

            // A choice wearing a dial, exactly as Filter Type is: the engine
            // rounds it at the point of use, so a host drawing a ramp from
            // Reverb to Delay would be drawing through a value that was never
            // anything but one of the two. Counting units rather than a 4
            // written here, so a fifth unit in the rack is not a selector that
            // can reach only four of them.
            case DC::send1:
            case DC::send2:
                return { 0.0f,
                         (float) (project::numDrumSendDestinations - 1),
                         project::numDrumSendDestinations,
                         drumSendLabels() };
        }

        return { 0.0f, 1.0f };
    };

    // Generated rather than written out, because the array they point at, the
    // copy into the kit and these rows are three orderings that must agree -
    // and three hand-written lists of a hundred and eight is three chances to
    // put the cowbell's decay on the clave. One loop over one index function
    // instead.
    const auto firstIndex = registry.size();

    // The assertion below is the only reader, and it compiles away in Release.
    juce::ignoreUnused(firstIndex);

    for (int voice = 0; voice < project::numDrumVoices; ++voice)
    {
        for (int c = 0; c < project::numDrumControls; ++c)
        {
            const auto control = (DC) c;

            // The id this row is about to take, worked out the way every
            // caller works it out, against the position it actually lands in.
            //
            // It reads as a tautology now that one function answers both, and
            // it is not quite one: the left side is where push_back is about to
            // put this row, and the right side is what drumParameterId will
            // tell a caller months from now. They agree only while this loop
            // walks the controls in enum order and skips none of them. Get it
            // wrong and every dial on the panel drives its neighbour - a fault
            // that looks like a UI bug and is not one.
            jassert((int) (registry.size() - firstIndex)
                    == project::drumControlIndex(voice, control));

            const auto range = rangeOf(control);

            Descriptor d{};
            d.name = ownedName(juce::String(project::drumVoiceName(voice)) + " "
                               + project::drumControlName(control));
            d.member = nullptr;
            d.minValue = range.minValue;
            d.maxValue = range.maxValue;
            d.logarithmic = false;
            d.group = groupForDrumControl(control);
            d.steps = range.steps;
            d.stepLabels = range.stepLabels;
            d.drumControl = project::drumControlIndex(voice, control);

            registry.push_back(d);
        }
    }
}

std::vector<Descriptor> buildRegistry()
{
    using P = SynthParameters;

    std::vector<Descriptor> registry = {
        {"Wave",        &P::wavePosition,      0.0f,     1.0f,     false, oscillator},
        {"Voices",      &P::unisonVoices,      1.0f,     7.0f,     false, oscillator, 7},
        {"Detune",      &P::unisonDetuneCents, 0.0f,     50.0f,    false, oscillator},
        {"Width",       &P::unisonWidth,       0.0f,     1.0f,     false, oscillator},
        {"Osc1 Level",  &P::osc1Level,         0.0f,     1.0f,     false, oscillator},

        {"Osc2 Level",  &P::osc2Level,         0.0f,     1.0f,     false, oscillator},
        {"Osc2 Wave",   &P::osc2WavePosition,  0.0f,     1.0f,     false, oscillator},
        {"Osc2 Semi",   &P::osc2Semitones,    -24.0f,    24.0f,    false, oscillator},
        {"Osc2 Fine",   &P::osc2Fine,         -100.0f,   100.0f,   false, oscillator},

        {"Sub",         &P::subLevel,          0.0f,     1.0f,     false, oscillator},
        {"Sub Wave",    &P::subWave,           0.0f,     1.0f,     false, oscillator},
        {"Noise",       &P::noiseLevel,        0.0f,     1.0f,     false, oscillator},
        {"Colour",      &P::noiseColour,       0.0f,     1.0f,     false, oscillator},

        {"Cutoff",      &P::filterCutoffHz,    20.0f,    20000.0f, true,  filter},
        {"Reso",        &P::filterResonance,   0.1f,     1.0f,     false, filter},
        {"Drive",       &P::driveAmount,       0.0f,     1.0f,     false, filter},

        {"Attack",      &P::attack,            0.001f,   3.0f,     true,  envelope},
        {"Decay",       &P::decay,             0.001f,   3.0f,     true,  envelope},
        {"Sustain",     &P::sustain,           0.0f,     1.0f,     false, envelope},
        {"Release",     &P::release,           0.001f,   5.0f,     true,  envelope},

        {"LFO Rate",    &P::lfoRateHz,         0.05f,    20.0f,    true,  lfo},
        {"To Filter",   &P::lfoDepth,          0.0f,     1.0f,     false, lfo},
        {"To Amp",      &P::lfoAmpDepth,       0.0f,     1.0f,     false, lfo},
        {"To Wave",     &P::lfoToWave,         0.0f,     1.0f,     false, lfo},

        {"LFO2 Rate",   &P::lfo2RateHz,        0.05f,    20.0f,    true,  lfo},
        {"LFO2 Wave",   &P::lfo2ToWave,        0.0f,     1.0f,     false, lfo},
        {"LFO2 Cutoff", &P::lfo2ToCutoff,      0.0f,     1.0f,     false, lfo},

        {"Distortion",  &P::fxDistortion,      0.0f,     1.0f,     false, effects},
        {"Delay Mix",   &P::delayMix,          0.0f,     1.0f,     false, effects},
        {"Delay Fbk",   &P::delayFeedback,     0.0f,     0.95f,    false, effects},
        {"Reverb Mix",  &P::reverbMix,         0.0f,     1.0f,     false, effects},
        {"Gain",        &P::masterGain,        0.0f,     1.0f,     false, effects},

        // ---- Appended for MIDI learn (ids 32+) -----------------------------
        // Everything below was reachable on the panel but not by id, so it
        // could be neither automated nor learned. Nothing above this line may
        // move: the group a parameter is shown under is free to change, but
        // its POSITION is the id a saved automation lane points at.
        {"Glide",       &P::glideTimeMs,       0.0f,     2000.0f,  false, oscillator},
        {"Sub Oct",     &P::subOctave,         1.0f,     2.0f,     false, oscillator, 2, subOctaveLabels},
        {"Filter Type", &P::filterType,        0.0f,     4.0f,     false, filter, 5, filterTypeLabels},

        {"Mod Atk",     &P::modEnvAttack,      0.001f,   3.0f,     true,  envelope},
        {"Mod Dec",     &P::modEnvDecay,       0.001f,   3.0f,     true,  envelope},
        {"Mod Sus",     &P::modEnvSustain,     0.0f,     1.0f,     false, envelope},
        {"Mod Rel",     &P::modEnvRelease,     0.001f,   5.0f,     true,  envelope},
        {"Mod>Wave",    &P::modEnvToWave,     -1.0f,     1.0f,     false, envelope},
        {"Mod>Cutoff",  &P::modEnvToCutoff,   -1.0f,     1.0f,     false, envelope},

        {"Delay Time",  &P::delayTimeMs,       10.0f,    1500.0f,  true,  effects},
        {"Reverb Size", &P::reverbSize,        0.0f,     1.0f,     false, effects},

        {"Chorus Rate", &P::chorusRate,        0.05f,    8.0f,     true,  effects},
        {"Chorus Dep",  &P::chorusDepth,       0.0f,     1.0f,     false, effects},
        {"Chorus Mix",  &P::chorusMix,         0.0f,     1.0f,     false, effects},

        {"Phaser Rate", &P::phaserRate,        0.05f,    8.0f,     true,  effects},
        {"Phaser Fbk",  &P::phaserFeedback,    0.0f,     0.9f,     false, effects},
        {"Phaser Mix",  &P::phaserMix,         0.0f,     1.0f,     false, effects},

        {"EQ Low",      &P::eqLowGain,        -12.0f,    12.0f,    false, effects},
        {"EQ Mid",      &P::eqMidGain,        -12.0f,    12.0f,    false, effects},
        {"EQ High",     &P::eqHighGain,       -12.0f,    12.0f,    false, effects},

        {"Vel>Cut",     &P::velocityToCutoff,  0.0f,     1.0f,     false, playing},
        {"Vel>Wave",    &P::velocityToWave,    0.0f,     1.0f,     false, playing},
        {"Vel>Amp",     &P::velocityToAmp,     0.0f,     1.0f,     false, playing},
        {"Vel>Res",     &P::velocityToResonance, 0.0f,   1.0f,     false, playing},
        {"Accent",      &P::accentAmount,      0.0f,     1.0f,     false, playing},

        {"Whl>Cut",     &P::wheelToCutoff,     0.0f,     1.0f,     false, playing},
        {"Whl>Wave",    &P::wheelToWave,       0.0f,     1.0f,     false, playing},
        {"Press>Cut",   &P::pressureToCutoff,  0.0f,     1.0f,     false, playing},

        {"Arp Octaves", &P::arpOctaves,        1.0f,     3.0f,     false, arp, 3},
        {"Arp Gate",    &P::arpGate,           0.05f,    1.0f,     false, arp},
        {"Arp Swing",   &P::arpSwing,          0.0f,     0.75f,    false, arp}
    };

    // Appended after the synth's sixty-three, which is what keeps those
    // sixty-three ids where they have always been. A hundred and eight drum
    // controls makes 171 of the 192 the cap allows.
    appendDrumMixer(registry);

    return registry;
}
} // namespace

const std::vector<Descriptor>& all()
{
    static const std::vector<Descriptor> registry = buildRegistry();
    return registry;
}

int count()
{
    return juce::jmin(maxParameters, (int) all().size());
}

const char* group(int id)
{
    if (id < 0 || id >= count())
        return "";

    return all()[(size_t) id].group;
}

const char* name(int id)
{
    if (id < 0 || id >= count())
        return "";

    return all()[(size_t) id].name;
}


int steps(int id)
{
    if (id < 0 || id >= count())
        return 0;

    return all()[(size_t) id].steps;
}

const char* stepLabel(int id, int step)
{
    if (id < 0 || id >= count())
        return nullptr;

    const auto& d = all()[(size_t) id];

    if (d.stepLabels == nullptr || step < 0 || step >= d.steps)
        return nullptr;

    return d.stepLabels[(size_t) step];
}
std::atomic<float>& valueOf(SynthParameters& params, int id)
{
    // A scratch atomic for an id that does not name anything. Writing into it
    // goes nowhere, which is the point: a stale lane or a host writing an id
    // from a newer build must not land on a real dial, and returning a
    // reference means every caller does not have to check.
    static std::atomic<float> nowhere{0.0f};

    if (id < 0 || id >= count())
        return nowhere;

    const auto& d = all()[(size_t) id];

    if (d.drumControl >= 0 && d.drumControl < project::numDrumStoredControls)
        return params.drumControls[(size_t) d.drumControl];

    if (d.member == nullptr)
        return nowhere;

    return params.*(d.member);
}

const std::atomic<float>& valueOf(const SynthParameters& params, int id)
{
    // The const overload defers to the other rather than repeating it. Casting
    // away const to call it is safe here because nothing on that path writes,
    // and the result is handed back const.
    return valueOf(const_cast<SynthParameters&>(params), id);
}

int firstDrumParameterId()
{
    for (int id = 0; id < count(); ++id)
        if (all()[(size_t) id].drumControl >= 0)
            return id;

    return count();
}

int drumParameterId(int voice, project::DrumControl control)
{
    if (voice < 0 || voice >= project::numDrumVoices)
        return -1;

    return firstDrumParameterId() + project::drumControlIndex(voice, control);
}

bool isDrumParameter(int id)
{
    if (id < 0 || id >= count())
        return false;

    return all()[(size_t) id].drumControl >= 0;
}

float readNormalised(const SynthParameters& params, int id)
{
    if (id < 0 || id >= count())
        return 0.0f;

    const auto& d = all()[(size_t) id];
    float value = valueOf(params, id).load();

    if (d.logarithmic)
    {
        // Ratios, not differences: halfway along a cutoff lane should be a
        // musical halfway, which is the geometric mean of the two ends.
        float low = std::log(juce::jmax(1.0e-6f, d.minValue));
        float high = std::log(juce::jmax(1.0e-6f, d.maxValue));
        float here = std::log(juce::jlimit(d.minValue, d.maxValue, value));
        return juce::jlimit(0.0f, 1.0f, (here - low) / (high - low));
    }

    return juce::jlimit(0.0f, 1.0f, (value - d.minValue) / (d.maxValue - d.minValue));
}

void writeNormalised(SynthParameters& params, int id, float normalised)
{
    if (id < 0 || id >= count())
        return;

    const auto& d = all()[(size_t) id];
    float clamped = juce::jlimit(0.0f, 1.0f, normalised);

    float value;
    if (d.logarithmic)
    {
        float low = std::log(juce::jmax(1.0e-6f, d.minValue));
        float high = std::log(juce::jmax(1.0e-6f, d.maxValue));
        value = std::exp(low + clamped * (high - low));
    }
    else
    {
        value = d.minValue + clamped * (d.maxValue - d.minValue);
    }

    valueOf(params, id).store(juce::jlimit(d.minValue, d.maxValue, value));
}

int idForName(const juce::String& wanted)
{
    for (int id = 0; id < count(); ++id)
        if (wanted == all()[(size_t) id].name)
            return id;

    return -1;
}
} // namespace paramreg
} // namespace wavelathe
