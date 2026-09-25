// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "DrumPanel.h"

#include "AppSettings.h"
#include "ParameterRegistry.h"
#include "SampleBrowser.h"
#include "TempoSync.h"

#include <algorithm>
#include <cmath>

namespace wavelathe
{
namespace
{
// How the controls read. Units a person can act on rather than the 0-1 the kit
// stores - "+3.0 st" says what turning the dial did, "0.62" does not.
juce::String formatLevel(double value)
{
    return juce::String(juce::roundToInt(value * 100.0)) + "%";
}

juce::String formatTune(double value)
{
    // Half a step of the dial's own resolution, not an arbitrary small number.
    // A ParameterKnob is 128 positions, so over a two-octave range one step is
    // about 0.19 of a semitone and dead centre lands 0.09 away from zero - a
    // dial sitting exactly where it was built read "+0.1 st", which is both
    // wrong and impossible to correct, since there is no position between.
    if (std::abs(value) < 24.0 / 127.0 * 0.5)
        return "0 st";

    return (value > 0.0 ? "+" : "") + juce::String(value, 1) + " st";
}

// The attack in real milliseconds, which needs the voice: the dial's reach is
// the drum's own, so 60% is six milliseconds on a rim shot and two hundred on
// a crash. A percentage would be the same number for both and tell you nothing
// about either.
//
// The curve comes from drums::attackSecondsFor rather than being repeated
// here, because a second copy of it in the UI is a second copy that can
// disagree with the one making the sound.
juce::String formatAttack(double value, int engine, float decay)
{
    // A blank slot has no drum whose attack this could be measured against, so
    // it reads as a fraction rather than claiming milliseconds it cannot know.
    // Put a drum on it and the number becomes real.
    if (engine == drums::noEngine)
        return value <= 0.0 ? "Off" : juce::String(juce::roundToInt(value * 100.0)) + "%";

    // Takes DECAY as well, because the attack's reach is a quarter of however
    // long the drum currently rings. Turning Decay down shortens what Attack
    // can do, and this readout follows it - which is the point of the change:
    // the same dial position meant 9% of a long kick and 86% of a short one,
    // and the panel said nothing about which.
    const auto milliseconds
        = drums::attackSecondsFor(drums::engineSpec(engine), (float) value, decay) * 1000.0f;

    // "Off" is reserved for exactly nothing. It read "Off" up to half a
    // millisecond at first, which on a rim shot - ten milliseconds of reach
    // in total - meant the first third of the dial moved and said nothing had
    // changed. A control that visibly moves while its readout insists it has
    // not is a control that looks broken.
    if (milliseconds <= 0.0f)
        return "Off";

    // A decimal under ten milliseconds, because that is the whole of a rim
    // shot's range and whole numbers there would be four settings.
    if (milliseconds < 9.95f)
        return juce::String(milliseconds, 1) + " ms";

    return juce::String(juce::roundToInt(milliseconds)) + " ms";
}

juce::String formatDecay(double value)
{
    // A multiple rather than a percentage, because that is what Decay is: the
    // range runs a third to three times the voice's own length, and "x1.0"
    // says "as the kit designer set it" in a way "50%" cannot.
    const auto factor = std::pow(3.0, value * 2.0 - 1.0);
    return "x" + juce::String(factor, factor < 1.0 ? 2 : 1);
}

juce::String formatPan(double value)
{
    const auto amount = juce::roundToInt(std::abs(value) * 100.0);

    if (amount < 3)
        return "C";

    return (value < 0.0 ? "L" : "R") + juce::String(amount);
}

// Which unit a slot runs the voice through. Rounded, because the dial has 128
// positions and five things to say - which is also what gives the dial its
// five tick marks, since ParameterKnob finds them by walking its positions and
// watching where this text changes.
juce::String formatSend(double value)
{
    return project::drumSendName(juce::roundToInt(value));
}

// How much of that unit there is. "Dry" rather than "0%" at the bottom,
// because zero here is not a quiet effect - it is the voice untouched, and
// saying so is the difference between a control that looks broken and one
// that looks off.
juce::String formatAmount(double value)
{
    const auto percent = juce::roundToInt(value * 100.0);
    return percent == 0 ? "Dry" : juce::String(percent) + "%";
}

juce::String formatPercent(double value)
{
    return juce::String(juce::roundToInt(value * 100.0)) + "%";
}

juce::String formatDecibels(double value)
{
    if (std::abs(value) < 24.0 / 127.0 * 0.5)
        return "0 dB";

    return (value > 0.0 ? "+" : "") + juce::String(value, 1) + " dB";
}

juce::String formatDivision(double value)
{
    return tempo::divisionName(juce::roundToInt(value));
}

ui::IconType iconFor(project::DrumControl control)
{
    switch (control)
    {
        case project::DrumControl::level:       return ui::IconType::gain;
        case project::DrumControl::tune:        return ui::IconType::pitch;
        case project::DrumControl::attack:      return ui::IconType::attack;
        case project::DrumControl::decay:       return ui::IconType::decay;
        case project::DrumControl::pan:         return ui::IconType::width;
        case project::DrumControl::send1:       return ui::IconType::filterType;
        case project::DrumControl::send1Amount: return ui::IconType::delayMix;
        case project::DrumControl::send2:       return ui::IconType::filterType;
        case project::DrumControl::send2Amount: return ui::IconType::delayMix;
    }

    return ui::IconType::gain;
}

// Takes a way to ask what is on the slot, because one of the nine needs it -
// Attack reads in the drum's own milliseconds rather than in percent, and
// which drum that is can be changed now. A function rather than the engine
// itself: the formatter is built once when the dial is and has to keep telling
// the truth after somebody swaps the sound on it.
std::function<juce::String(double)> formatterFor(project::DrumControl control,
                                                 std::function<int()> engineOf,
                                                 std::function<float()> decayOf)
{
    switch (control)
    {
        case project::DrumControl::level:       return formatLevel;
        case project::DrumControl::tune:        return formatTune;
        case project::DrumControl::decay:       return formatDecay;
        case project::DrumControl::pan:         return formatPan;
        case project::DrumControl::send1:       return formatSend;
        case project::DrumControl::send2:       return formatSend;
        case project::DrumControl::send1Amount: return formatAmount;
        case project::DrumControl::send2Amount: return formatAmount;

        case project::DrumControl::attack:
            return [engineOf = std::move(engineOf), decayOf = std::move(decayOf)](double value)
            { return formatAttack(value, engineOf(), decayOf()); };
    }

    return formatLevel;
}

// The line under each strip's name that says which note strikes it.
//
// Small, because it is a reference rather than a control: you read it once
// when setting a controller up and then never again. Worth the thirteen pixels
// it takes off the dials all the same - "which pad is the cowbell on" is
// otherwise a question with no answer anywhere in the application.
constexpr int noteHeight = 13;

// The dial names' size, and - see paintTriggerNotes - the dial names' colour.
constexpr float noteFontHeight = 11.0f;

constexpr int headerHeight = 22;
constexpr int nameHeight = 20;
constexpr int knobGap = 2;

// ---- The lamp beside each Level dial ---------------------------------------

// How often the lamps are looked at. Thirty a second is the rate a flash needs
// to read as a flash; the pattern-usage check that shares this timer does not
// need anything like it and is counted down to roughly four a second below.
constexpr int ledRefreshHz = 30;

// How fast the light falls away once the peak does. Per tick, so at thirty a
// second this is about a sixth of a second to fade out - long enough to see,
// short enough that two hits in quick succession read as two.
constexpr float ledDecayPerTick = 0.62f;

// What carries an ordinary hit to the top of the lamp's range.
//
// Measured rather than guessed: a kick at the default 80% Level, with the kit
// master at 80% and the pan centred, peaks at about 0.45 - and the square root
// of that is 0.67, which composites to (158,160,166) against the unlit
// (43,49,61). That is a mid grey, not the white a flash is supposed to be.
// This lifts a normal hit to the top while leaving a quiet one visibly dimmer:
// a tenth of full scale still only reaches about a half.
constexpr float ledGain = 1.5f;

// Past full scale. Not "nearly", because 1.0 is the largest value that can be
// represented rather than a warning sign, and a lamp that lights on a signal
// which is merely loud is a lamp people learn to ignore. Reachable: a
// full-scale recording, hard panned, at full Level and master reads exactly
// 1.0, which is why this compares with >= rather than >.
constexpr float ledClipLevel = 1.0f;

// How long a clip stays lit after the sample that caused it. Long enough to
// catch your eye when you were looking at the pattern rather than the mixer.
constexpr juce::uint32 ledClipHoldMs = 1200;

// The lamp has this many visible steps. Brightness is quantised to them before
// deciding whether a repaint is needed, so a decay that is still mathematically
// moving stops costing anything once it is below what the eye can tell apart.
constexpr int ledBrightnessSteps = 24;

// Below this the lamp is off. A tail that has decayed to a thousandth of full
// scale is inaudible, and a lamp still glowing for it says the voice is doing
// something when it is not.
constexpr float ledFloor = 0.001f;

// The lamp's column and the dot in it. Narrow, because a strip is a twelfth
// of the page wide and every pixel taken here comes off the Level dial - the
// widest control on the strip, and the one whose size says it is the most
// important.
constexpr int ledColumnWidth = 12;
constexpr int ledSize = 8;

// Down from the top of the row rather than centred in it, so the lamp sits
// beside the dial's NAME rather than halfway down its side, which is where
// the eye already is when it reads the strip.
constexpr int ledTopInset = 3;

// How wide the rack is. Fixed rather than a share of the width, because three
// dials side by side need the same room whatever the window is doing - and a
// proportional column would be the first thing to become unusable when
// somebody makes the window narrow.
constexpr int rackWidth = 190;

// How the strip divides vertically, in arbitrary units that are normalised
// against whatever height there turns out to be.
//
// Level gets a full row and the other eight pair off across four half-width
// rows. Which is how nine controls fit in a column that started with four.
//
// The pairing is not only for space: each pair is one decision with two parts.
// Attack beside Decay is an envelope, the way every synth has ever drawn one;
// "FX 1" beside "Amt 1" is a stage of the chain and how much of it there is.
//
// Decay lost its full-width dial to make room, and that is a real cost -
// it is used more often than Attack. But an envelope split across a big dial
// and a small one reads as two unrelated controls, and the pair is worth more
// than the size.
constexpr float bigRowWeight = 1.0f;
constexpr float smallRowWeight = 0.72f;
constexpr float totalRowWeight = bigRowWeight + smallRowWeight * 4.0f;
} // namespace

DrumPanel::DrumPanel(WaveLatheProcessor& processor, ui::LcdDisplay& lcdToUse)
    : processorRef(processor), lcd(lcdToUse)
{
    addAndMakeVisible(mixerSection);
    mixerSection.setStyle(ui::SectionStyle::standalone);

    buildStrips();
    buildRack();
    refresh();
    refreshHeaders();

    // At the lamps' rate. It used to run at four a second, which is all the
    // one thing it watched needed - which voices the pattern uses, edited on
    // another page. A lamp that flashes on a hit needs far more than that, so
    // the timer runs fast and the slow question is counted down to inside it.
    startTimerHz(ledRefreshHz);
}

DrumPanel::~DrumPanel()
{
    stopTimer();
}

void DrumPanel::buildStrips()
{
    const auto& params = processorRef.getParameters();

    for (int voice = 0; voice < project::numDrumVoices; ++voice)
    {
        auto& strip = strips[(size_t) voice];
        strip.voice = voice;

        // The name is the audition button, which is the same gesture the grid
        // uses: click a voice's name to hear it. Two places doing the same
        // thing the same way beats one place doing it and the other not.
        strip.audition = std::make_unique<VoiceButton>(project::drumVoiceName(voice));
        strip.audition->onClick = [this, voice]
        {
            processorRef.auditionDrumVoice(voice);
            touchVoice(voice);
            lcd.printTransient(juce::String(project::drumVoiceName(voice)).toUpperCase());
        };
        strip.audition->onRightClick = [this, voice] { showSlotMenu(voice); };
        addAndMakeVisible(*strip.audition);

        strip.led = std::make_unique<VoiceLed>();

        // Takes the mouse but does nothing with it, which is what lets the
        // tooltip appear. Safe only because the lamp has a column of its own
        // beside the Level dial rather than sitting on top of it - over the
        // dial this would swallow drags and make it feel broken in one corner.
        strip.led->setInterceptsMouseClicks(true, false);
        strip.led->setTooltip("White when this voice sounds, red when it goes past full"
                              " scale. Red is measured after both sends, so a drive that"
                              " is too hot lights it even when Level is not.");
        addAndMakeVisible(*strip.led);

        for (int c = 0; c < project::numDrumControls; ++c)
        {
            const auto control = (project::DrumControl) c;
            const auto id = paramreg::drumParameterId(voice, control);
            strip.ids[(size_t) c] = id;

            // Every one of the nine has a registry row since 0.44.0, so every
            // dial takes its range from the registry and a dial cannot disagree
            // with its own automation lane about what the ends mean. There used
            // to be a second table of ranges here for the five the host could
            // not see, which is exactly the kind of second list that drifts.
            jassert(id >= 0);
            const auto& descriptor = paramreg::all()[(size_t) id];
            const juce::NormalisableRange<double> range {(double) descriptor.minValue,
                                                         (double) descriptor.maxValue};

            const auto storage = (size_t) project::drumControlIndex(voice, control);

            auto knob = std::make_unique<ui::ParameterKnob>(
                project::drumControlName(control), iconFor(control), range,
                (double) params.drumControls[storage].load(),
                formatterFor(control,
                             [this, voice] { return processorRef.getDrumEngine(voice); },
                             [this, voice]
                             {
                                 const auto at = (size_t) project::drumControlIndex(
                                     voice, project::DrumControl::decay);
                                 return processorRef.getParameters().drumControls[at].load();
                             }));

            knob->onValueChanged = [this, id, storage, voice, control](double realValue,
                                                                       juce::String text)
            {
                // Written to the storage array directly rather than through
                // paramreg::valueOf, because two of the six have no id to look
                // one up by. One path for all six beats a branch that only
                // exists because of where a value happens to be published.
                processorRef.getParameters().drumControls[storage].store((float) realValue);

                // Turning a dial on a slot is working on that slot, which is
                // what points the Waveform and Envelope at it.
                touchVoice(voice);

                // Attack reads in milliseconds off the CURRENT decay, so
                // moving Decay changes what the Attack dial says without
                // Attack having moved. Nothing else would repaint it, and a
                // readout that is quietly out of date is worse than one that
                // was never there.
                if (control == project::DrumControl::decay)
                    if (auto* attackKnob = strips[(size_t) voice]
                                               .knobs[(size_t) project::DrumControl::attack]
                                               .get())
                        attackKnob->repaint();

                lcd.printTransient(juce::String(project::drumVoiceName(voice)).toUpperCase() + " "
                                   + juce::String(project::drumControlName(control)).toUpperCase()
                                   + ": " + text);

                // The same reasoning written beside the editor's own knob
                // factory: a host that is never told a control moved cannot
                // record it. Nothing to tell for Tune and Pan, which is what
                // the null id means.
                if (id >= 0)
                    if (auto* parameter = processorRef.getHostParameter(id))
                        parameter->reportToHost();
            };

            knob->onGestureStart = [this, id, voice, control]
            {
                processorRef.recordUndoPoint(juce::String(project::drumVoiceName(voice)) + " "
                                             + project::drumControlName(control));

                if (id >= 0)
                    if (auto* parameter = processorRef.getHostParameter(id))
                        parameter->beginChangeGesture();
            };

            knob->onGestureEnd = [this, id]
            {
                // A gesture begun and never ended holds a host's automation
                // off that parameter for good, so this is paired with the
                // above even though only four of the six have anything to end.
                if (id >= 0)
                    if (auto* parameter = processorRef.getHostParameter(id))
                        parameter->endChangeGesture();
            };

            addAndMakeVisible(*knob);
            strip.knobs[(size_t) c] = std::move(knob);
        }
    }
}

void DrumPanel::buildRack()
{
    using FX = project::DrumFxControl;

    // Titles in the order the sends number them, so "Send 1: Dist" and the
    // second box down are obviously the same thing. Off is not a unit, so the
    // rack starts at destination 1.
    struct UnitSpec
    {
        const char* title;
        juce::Colour tint;
        std::array<FX, 3> controls;
        std::array<ui::IconType, 3> icons;
        std::array<juce::NormalisableRange<double>, 3> ranges;
        std::array<std::function<juce::String(double)>, 3> formatters;
    };

    const juce::NormalisableRange<double> unit{0.0, 1.0};
    const juce::NormalisableRange<double> decibels{-12.0, 12.0};

    // A quarter note and shorter, not the whole table. The chain is per voice
    // so each of the twelve owns a delay line, and they are a second long -
    // which is a quarter note at 60bpm. Offering "1/1" on a dial that would
    // be clamped to a quarter of it is offering a setting that lies.
    const juce::NormalisableRange<double> divisions{
        (double) tempo::nearestDivisionForBeats(project::drumDelaySlowestBeats),
        (double) (tempo::getNumDivisions() - 1)};

    const UnitSpec specs[drums::numFxUnits] = {
        {"Reverb 1", ui::colours::accent,
         {FX::reverbSize, FX::reverbDamp, FX::reverbMix},
         {ui::IconType::reverbSize, ui::IconType::cutoff, ui::IconType::reverbMix},
         {unit, unit, unit},
         {formatPercent, formatPercent, formatPercent}},

        {"Distortion", ui::colours::accent,
         {FX::driveAmount, FX::driveTone, FX::driveMix},
         {ui::IconType::drive, ui::IconType::cutoff, ui::IconType::delayMix},
         {unit, unit, unit},
         {formatPercent, formatPercent, formatPercent}},

        {"EQ", ui::colours::accent,
         {FX::eqLow, FX::eqHigh, FX::eqLevel},
         {ui::IconType::subOsc, ui::IconType::wave, ui::IconType::gain},
         {decibels, decibels, unit},
         {formatDecibels, formatDecibels, formatPercent}},

        {"Delay", ui::colours::accent,
         {FX::delayTime, FX::delayFeedback, FX::delayMix},
         {ui::IconType::delayTime, ui::IconType::delayFeedback, ui::IconType::delayMix},
         {divisions, unit, unit},
         {formatDivision, formatPercent, formatPercent}}
    };

    const auto& params = processorRef.getParameters();

    for (int u = 0; u < drums::numFxUnits; ++u)
    {
        const auto& spec = specs[u];
        auto& fxUnit = rack[(size_t) u];

        fxUnit.section = std::make_unique<ui::FlowSection>(spec.title, spec.tint);
        fxUnit.section->setStyle(ui::SectionStyle::standalone);
        addAndMakeVisible(*fxUnit.section);

        for (int k = 0; k < 3; ++k)
        {
            const auto control = spec.controls[(size_t) k];
            fxUnit.controls[(size_t) k] = control;

            auto knob = std::make_unique<ui::ParameterKnob>(
                project::drumFxControlName(control), spec.icons[(size_t) k],
                spec.ranges[(size_t) k], (double) params.drumFx[(size_t) control].load(),
                spec.formatters[(size_t) k]);

            const juce::String label = juce::String(spec.title) + " "
                                       + project::drumFxControlName(control);

            knob->onValueChanged = [this, control, label](double realValue, juce::String text)
            {
                processorRef.getParameters().drumFx[(size_t) control].store((float) realValue);
                lcd.printTransient(label.toUpperCase() + ": " + text);
            };

            // No begin/end gesture pair: there is no host parameter behind
            // these to tell. The undo point is still worth taking - a rack
            // dial moved by accident is exactly as annoying to put back as a
            // registered one.
            knob->onGestureStart = [this, label] { processorRef.recordUndoPoint(label); };

            addAndMakeVisible(*knob);
            fxUnit.knobs[(size_t) k] = std::move(knob);
        }
    }
}


void DrumPanel::visibilityChanged()
{
    if (!isVisible() && processorRef.getDrumNoteMap().isLearning())
    {
        processorRef.getDrumNoteMap().cancelLearn();
        repaint();
    }
}

void DrumPanel::saveNoteMap()
{
    // The map describes the pads on the desk, so it is remembered with the
    // application rather than with a project - the same place and for the same
    // reason as the CC map and the drum channel. Opening somebody else's kit
    // must not move your pads.
    AppSettings::getInstance().setDrumNoteMap(processorRef.getDrumNoteMap().toString());
}
void DrumPanel::refreshHeaders()
{
    for (int voice = 0; voice < project::numDrumVoices; ++voice)
    {
        auto& strip = strips[(size_t) voice];

        if (strip.audition == nullptr)
            continue;

        const auto loaded = processorRef.getDrumSampleName(voice);
        const auto engine = processorRef.getDrumEngine(voice);
        const auto name = juce::String(project::drumVoiceName(voice));
        const auto playing = processorRef.getDrumSlotDescription(voice);

        // The row keeps its own name whatever is on it, because the grid on
        // the Sequencer page still calls it that and two pages disagreeing
        // about which row is which is worse than not knowing what a slot
        // holds. The colour is what says it is not at its default, and the
        // tooltip is what says what.
        //
        // Three states rather than two now, and they are worth telling apart
        // at a glance: a sample, a drum that is not the one this row started
        // with, and an empty slot. A row playing its own drum is the plain
        // case and gets no tint - if everything is tinted, nothing is.
        const auto blank = loaded.isEmpty() && engine == drums::noEngine;

        const auto tint = [&]
        {
            if (loaded.isNotEmpty())
                return ui::colours::accent.withAlpha(0.35f);

            if (engine != drums::noEngine && engine != drums::defaultEngineFor(voice))
                return ui::colours::accentMod.withAlpha(0.30f);

            return juce::Colours::transparentBlack;
        }();

        strip.audition->setColour(juce::TextButton::buttonColourId, tint);

        // An empty slot is greyed rather than tinted. Tinting it made the one
        // row with nothing on it the BRIGHTEST thing in the line, which is the
        // wrong way round - a slot holding nothing should look like it holds
        // nothing. Measured before and after: the tint reached (80,84,93)
        // against a plain row's (71,75,82), which is both barely visible and
        // pointing the wrong way.
        strip.audition->setColour(juce::TextButton::textColourOffId,
                                  blank ? ui::colours::textDim.withAlpha(0.55f)
                                        : ui::colours::textPrimary);

        // Only says "playing X" when X is not just the row's own name, so the
        // eleven ordinary rows do not carry a tooltip that reads "Kick:
        // playing Kick".
        const auto what = playing == name ? juce::String() : ": " + playing;

        strip.audition->setTooltip(
            loaded.isNotEmpty()
                ? name + ": playing the sample " + loaded
                      + ". Click to hear it, right-click to change it."
                : engine == drums::noEngine
                      ? name + " is empty. Right-click to load a sample onto it or to pick a drum."
                      : name + what + ": click to hear it, right-click to load a sample"
                                      " onto it or to pick a different drum.");
    }
}

void DrumPanel::touchVoice(int voice)
{
    if (voice < 0 || voice >= project::numDrumVoices)
        return;

    lastTouchedVoice = voice;

    // Every time, not only when the slot CHANGES. It was written the other way
    // round to save work and was simply wrong: turning Decay on the snare for a
    // minute never changes which slot is being worked on, so the picture of it
    // would have been drawn once and then sat there while the sound moved
    // underneath. What this costs is bounded by the display's own timer, which
    // coalesces however fast a dial is dragged.
    if (onVoiceTouched != nullptr)
        onVoiceTouched(voice);
}

void DrumPanel::showSlotMenu(int voice)
{
    const auto name = juce::String(project::drumVoiceName(voice));
    const auto loaded = processorRef.getDrumSampleName(voice);
    const auto engine = processorRef.getDrumEngine(voice);

    juce::PopupMenu menu;
    menu.addSectionHeader(name + ": " + processorRef.getDrumSlotDescription(voice));

    menu.addItem(loadSampleItem, "Load sample...");
    menu.addItem(clearSampleItem, "Clear sample", loaded.isNotEmpty());

    // The 808's Tom/Conga toggle, generalised. On the hardware one channel is
    // a low tom or a low conga depending on a switch, because a tom and a
    // conga are not worth a channel each; here every slot has that switch and
    // it reaches all sixteen drums.
    //
    // A submenu rather than sixteen entries in the top level, which would
    // bury Load sample under a wall of drum names on a menu whose usual
    // errand is the sample.
    juce::PopupMenu engines;

    // Ticked when it is what the slot is set to, so the menu says where you
    // are as well as where you can go.
    engines.addItem(firstEngineItem + drums::numEngines, "None (silent)", true,
                    engine == drums::noEngine);
    engines.addSeparator();

    for (int i = 0; i < drums::numEngines; ++i)
        engines.addItem(firstEngineItem + i, drums::engineName(i), true, engine == i);

    menu.addSubMenu("Synth drum", engines);

    // Which pad plays this slot. Below the sound, because a slot's sound is
    // what the menu is usually opened for and this is set once per controller.
    menu.addSeparator();

    const auto& noteMap = processorRef.getDrumNoteMap();
    const auto transpose = juce::jlimit(-project::maxDrumTranspose, project::maxDrumTranspose,
                                        (int) std::round(
                                            processorRef.getParameters().drumTranspose.load()));
    const auto taught = noteMap.noteFor(voice) != DrumNoteMap::none;

    // The note a reset goes back to: this slot's GM primary, shifted by the
    // kit's transpose, whether or not the slot is currently taught.
    const auto gmNote = juce::jlimit(0, 127, project::drumPrimaryNoteFor(voice) + transpose);

    // The same item both ways round, because the way out of an arm has to be
    // the control that got you into it - the Options menu's MIDI Learn settled
    // this question the same way.
    const bool arming = noteMap.getLearnTarget() == voice;

    menu.addItem(learnNoteItem, arming ? "Cancel learn - waiting for a pad"
                                       : juce::String("Learn trigger note..."));

    // Named with the note it goes back to, so the item says what it will do
    // rather than only what it undoes. Disabled when the slot is already there,
    // which is also how the menu reports that a slot has not been taught.
    menu.addItem(resetNoteItem,
                 "Reset to GM (" + juce::MidiMessage::getMidiNoteName(gmNote, true, true, 3) + ")",
                 taught);

    // Said here rather than left to be discovered: a slot with a sample on it
    // plays the sample, and the drum underneath is what Clear goes back to.
    if (loaded.isNotEmpty())
        menu.addSectionHeader("Playing the sample; the drum is what Clear returns to");

    menu.showMenuAsync(juce::PopupMenu::Options{}.withTargetComponent(strips[(size_t) voice].audition.get()),
                       [this, voice](int result) { handleSlotMenu(voice, result); });
}

void DrumPanel::handleSlotMenu(int voice, int result)
{
    if (result == 0)
        return;

    touchVoice(voice);

    const auto name = juce::String(project::drumVoiceName(voice));

    if (result == loadSampleItem)
    {
        chooseSampleFor(voice);
        return;
    }

    if (result == clearSampleItem)
    {
        processorRef.recordUndoPoint(name + " sample");
        processorRef.clearDrumSample(voice);
        refreshHeaders();

        lcd.print(name + " is back to " + processorRef.getDrumSlotDescription(voice) + ".");
        return;
    }

    if (result == learnNoteItem)
    {
        if (processorRef.getDrumNoteMap().getLearnTarget() == voice)
        {
            processorRef.getDrumNoteMap().cancelLearn();
            repaint();
            lcd.print(name + " is unchanged - learn cancelled.");
            return;
        }

        const auto channel = juce::jlimit(0, 16,
                                          (int) std::round(
                                              processorRef.getParameters().drumChannel.load()));

        // Arming a kit nothing can reach would leave the strip saying "hit a
        // pad" until somebody worked out why nothing happened. The setting
        // that has to change is named, because it is not on this page.
        if (channel == 0)
        {
            lcd.print("Set a Drum Channel in Options first - nothing reaches the kit yet.");
            return;
        }

        processorRef.getDrumNoteMap().armLearn(voice);
        repaint();

        lcd.print("Hit the pad that should play " + name + " (on channel "
                  + juce::String(channel) + ").");
        return;
    }

    if (result == resetNoteItem)
    {
        processorRef.getDrumNoteMap().clearVoice(voice);
        saveNoteMap();
        repaint();

        const auto transpose = juce::jlimit(
            -project::maxDrumTranspose, project::maxDrumTranspose,
            (int) std::round(processorRef.getParameters().drumTranspose.load()));
        const auto gmNote = juce::jlimit(0, 127, project::drumPrimaryNoteFor(voice) + transpose);

        lcd.print(name + " is back on General MIDI - "
                  + juce::MidiMessage::getMidiNoteName(gmNote, true, true, 3) + " ("
                  + juce::String(gmNote) + ").");
        return;
    }

    if (result >= firstEngineItem && result <= firstEngineItem + drums::numEngines)
    {
        const auto chosen = result - firstEngineItem;
        const auto engine = chosen < drums::numEngines ? chosen : drums::noEngine;

        if (engine == processorRef.getDrumEngine(voice))
            return;

        processorRef.recordUndoPoint(name + " drum");
        processorRef.setDrumEngine(voice, engine);
        refreshHeaders();

        // Every dial on the strip keeps its position, which is the useful
        // behaviour and worth saying once: Decay and Attack are fractions of
        // whatever drum is on the slot, so the same setting means something
        // different on a crash than on a rim shot.
        if (engine == drums::noEngine)
            lcd.print(name + " is empty - load a sample onto it, or pick a drum.");
        else
            lcd.print(name + " is now a " + juce::String(drums::engineName(engine))
                      + ". Its dials kept their positions.");
    }
}

void DrumPanel::chooseSampleFor(int voice)
{
    const auto name = juce::String(project::drumVoiceName(voice));

    // What the slot holds right now, captured before any previewing, so that
    // cancelling puts back exactly this - including nothing, when the voice
    // was on its own circuit.
    const auto wasLoaded = processorRef.getDrumSample(voice);

    SampleBrowser::show(
        name,
        AppSettings::getInstance().getFolder(AppSettings::Folder::drumSample),

        // Preview: the sample really goes onto the voice, so what is heard is
        // this pad with its own Level, Tune, Attack, Decay, Pan and sends -
        // and, if the pattern is running, in the groove rather than alone.
        // No undo point: browsing is not an edit until it is committed.
        [this, voice](const juce::File& file, juce::String& error)
        {
            if (!processorRef.loadDrumSample(voice, file, error))
                return false;

            refreshHeaders();
            processorRef.auditionDrumVoice(voice);
            return true;
        },

        [this, voice, wasLoaded]
        {
            processorRef.setDrumSample(voice, wasLoaded);
            refreshHeaders();
        },

        [this, voice, name, wasLoaded](const juce::File& file)
        {
            settingsFolderFor(file);

            // recordUndoPoint snapshots the state as it stands, so the
            // ORIGINAL sample goes back into the slot for the length of that
            // call. Otherwise one undo would return the pad to the last file
            // arrowed past rather than to what it held before browsing - the
            // browser would have written its whole search into the history.
            const auto chosen = processorRef.getDrumSample(voice);

            processorRef.setDrumSample(voice, wasLoaded);
            processorRef.recordUndoPoint(name + " sample");
            processorRef.setDrumSample(voice, chosen);

            refreshHeaders();

            lcd.print(name + " is now " + file.getFileName() + " ("
                      + juce::String(chosen != nullptr ? chosen->seconds() : 0.0, 2) + " s).");
            lcd.print("Level, Attack, Decay, Tune and Pan all still work on it.");
        });
}

void DrumPanel::settingsFolderFor(const juce::File& file)
{
    AppSettings::getInstance().setFolder(AppSettings::Folder::drumSample,
                                         file.getParentDirectory());
}

std::vector<std::pair<ui::ParameterKnob*, int>> DrumPanel::automatableKnobs() const
{
    std::vector<std::pair<ui::ParameterKnob*, int>> pairs;
    pairs.reserve((size_t) project::numDrumParameters);

    for (const auto& strip : strips)
        for (size_t c = 0; c < strip.knobs.size(); ++c)
            if (strip.knobs[c] != nullptr && strip.ids[c] >= 0)
                pairs.push_back({strip.knobs[c].get(), strip.ids[c]});

    return pairs;
}

void DrumPanel::refresh()
{
    const auto& params = processorRef.getParameters();

    for (int voice = 0; voice < project::numDrumVoices; ++voice)
    {
        auto& strip = strips[(size_t) voice];

        for (int c = 0; c < project::numDrumControls; ++c)
        {
            if (strip.knobs[(size_t) c] == nullptr)
                continue;

            const auto storage = (size_t) project::drumControlIndex(voice, (project::DrumControl) c);
            strip.knobs[(size_t) c]->setRealValue((double) params.drumControls[storage].load());
        }
    }

    for (auto& fxUnit : rack)
        for (size_t k = 0; k < fxUnit.knobs.size(); ++k)
            if (fxUnit.knobs[k] != nullptr)
                fxUnit.knobs[k]->setRealValue(
                    (double) params.drumFx[(size_t) fxUnit.controls[k]].load());

    // A loaded project brings its own slots, so the headers have to follow
    // it the same way the dials do.
    refreshHeaders();

    timerCallback();
}

void DrumPanel::VoiceLed::showPeak(float peak)
{
    // Fast up, slow down. The rise is the hit itself, so it takes the peak
    // whole; the fall is the lamp's own, so that a hit shorter than a frame
    // still lights it for long enough to be seen.
    //
    // The square root is what makes a quiet hit visible. Loudness is not
    // linear in amplitude, and a lamp driven straight from the sample value
    // shows a hit at a quarter scale as a quarter lit, which reads as almost
    // off.
    const auto target = peak > ledFloor
                            ? juce::jmin(1.0f, std::sqrt(juce::jmin(1.0f, peak)) * ledGain)
                            : 0.0f;

    brightness = juce::jmax(target, brightness * ledDecayPerTick);

    if (brightness < ledFloor)
        brightness = 0.0f;

    const auto now = juce::Time::getMillisecondCounter();

    if (peak >= ledClipLevel)
    {
        clippedAtMs = now;
        everClipped = true;
    }

    // Elapsed-since rather than a deadline, which is the same wrap-safe shape
    // drums::RetiringStore uses: both sides unsigned, so the counter turning
    // over every 49 days reads as a small elapsed time. A deadline stored as
    // now + hold would need the comparison written the other way round and is
    // the version of this that is easy to get subtly wrong.
    const bool clipped = everClipped && now - clippedAtMs < ledClipHoldMs;

    const auto step = juce::roundToInt(brightness * (float) ledBrightnessSteps);

    if (step == drawnBrightness && clipped == drawnClipped)
        return;

    drawnBrightness = step;
    drawnClipped = clipped;
    repaint();
}

void DrumPanel::VoiceLed::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat().reduced(1.0f);

    // The unlit lamp, which is drawn whatever else happens: an indicator you
    // can only find when it is on is one you cannot find.
    g.setColour(ui::colours::panelEdge);
    g.fillEllipse(area);

    const auto lit = (float) drawnBrightness / (float) ledBrightnessSteps;

    if (drawnClipped)
    {
        // Red wins over white. A hit that went over is still a hit, and the
        // thing worth telling you about the two is the clipping.
        g.setColour(juce::Colours::red);
        g.fillEllipse(area);

        g.setColour(juce::Colours::white.withAlpha(0.45f));
        g.fillEllipse(area.reduced(area.getWidth() * 0.3f));
        return;
    }

    if (lit <= 0.0f)
        return;

    g.setColour(juce::Colours::white.withAlpha(lit));
    g.fillEllipse(area);
}

void DrumPanel::timerCallback()
{
    // Every tick: the lamps, which are the reason this runs as fast as it does.
    for (int voice = 0; voice < project::numDrumVoices; ++voice)
        if (auto* led = strips[(size_t) voice].led.get())
            led->showPeak(processorRef.takeDrumVoicePeak(voice));

    // Roughly four times a second: whether the pattern uses each voice, which
    // is edited on another page and changes at the speed of somebody clicking.
    if (++tick < ledRefreshHz / 4)
        return;

    tick = 0;

    // A learn binds on the audio thread, so this is the only way the panel
    // finds out. Watched at the pattern's rate rather than the lamps' because
    // it changes at the speed of somebody hitting a pad, and the repaint that
    // follows is the whole row of twelve.
    const auto noteMapVersion = processorRef.getDrumNoteMap().getVersion();

    if (noteMapVersion != lastNoteMapVersion)
    {
        lastNoteMapVersion = noteMapVersion;
        saveNoteMap();
        refreshHeaders();
        repaint();
    }

    auto& sequencer = processorRef.getSequencer();
    bool changed = false;

    for (int voice = 0; voice < project::numDrumVoices; ++voice)
    {
        const bool used = sequencer.isDrumVoiceUsed(voice);

        if (used != voiceUsed[(size_t) voice])
        {
            voiceUsed[(size_t) voice] = used;
            changed = true;
        }
    }

    if (changed)
        repaint();
}

juce::Rectangle<int> DrumPanel::stripArea(int voice) const
{
    auto area = mixerSection.getBounds().reduced(8, 6);
    area.removeFromTop(headerHeight);

    const int count = project::numDrumVoices;

    // Measured from the edges rather than by multiplying a rounded width, so
    // twelve strips fill the section exactly and there is no leftover column
    // belonging to no voice - the same arithmetic the drum grid's rows use.
    const int left = area.getX() + (area.getWidth() * voice) / count;
    const int right = area.getX() + (area.getWidth() * (voice + 1)) / count;

    return juce::Rectangle<int>(left, area.getY(), right - left, area.getHeight());
}

void DrumPanel::resized()
{
    auto bounds = getLocalBounds().reduced(8);

    // The rack takes its fixed column off the right, and only if there is
    // enough left for the mixer to still be a mixer. On a window too narrow
    // for both, twelve strips are the page and the rack goes rather than
    // both becoming unusable.
    auto rackArea = bounds.getWidth() > rackWidth * 3
                        ? bounds.removeFromRight(rackWidth)
                        : juce::Rectangle<int>();

    mixerSection.setBounds(bounds);

    for (int voice = 0; voice < project::numDrumVoices; ++voice)
    {
        auto& strip = strips[(size_t) voice];
        auto area = stripArea(voice).reduced(2, 0);

        if (strip.audition != nullptr)
            strip.audition->setBounds(area.removeFromTop(nameHeight));

        strip.noteArea = area.removeFromTop(noteHeight);

        area.removeFromTop(2);

        const auto height = (float) area.getHeight();
        const auto bigRow = juce::jmax(1, juce::roundToInt(height * bigRowWeight / totalRowWeight));
        const auto smallRow = juce::jmax(1, juce::roundToInt(height * smallRowWeight / totalRowWeight));

        const auto place = [&area, &strip](project::DrumControl control, int rowHeight)
        {
            auto& knob = strip.knobs[(size_t) control];

            if (knob != nullptr)
                knob->setBounds(area.removeFromTop(rowHeight).reduced(0, knobGap));
        };

        // Two side by side, each getting half the width. A pair rather than a
        // row of its own is what buys the space for the sends.
        const auto placePair = [&area, &strip](project::DrumControl left,
                                               project::DrumControl right, int rowHeight)
        {
            auto row = area.removeFromTop(rowHeight).reduced(0, knobGap);
            auto leftHalf = row.removeFromLeft(row.getWidth() / 2);

            if (strip.knobs[(size_t) left] != nullptr)
                strip.knobs[(size_t) left]->setBounds(leftHalf);

            if (strip.knobs[(size_t) right] != nullptr)
                strip.knobs[(size_t) right]->setBounds(row);
        };

        using DC = project::DrumControl;

        // Level, with the lamp in a narrow column beside it. Taken off the
        // row rather than laid over the dial, so the two never fight for the
        // same pixels or the same mouse.
        {
            auto row = area.removeFromTop(bigRow).reduced(0, knobGap);
            auto lamp = row.removeFromLeft(ledColumnWidth);

            if (strip.led != nullptr)
                strip.led->setBounds(lamp.withSizeKeepingCentre(ledSize, ledSize)
                                         .withY(lamp.getY() + ledTopInset));

            if (strip.knobs[(size_t) DC::level] != nullptr)
                strip.knobs[(size_t) DC::level]->setBounds(row);
        }
        placePair(DC::attack, DC::decay, smallRow);
        placePair(DC::tune, DC::pan, smallRow);

        // In chain order, top to bottom: what the voice goes through first,
        // then what it goes through after that. The page reads the way the
        // signal flows.
        placePair(DC::send1, DC::send1Amount, smallRow);
        placePair(DC::send2, DC::send2Amount, smallRow);
    }

    if (rackArea.isEmpty())
    {
        // Hidden rather than left at stale bounds, so a narrow window does not
        // leave twelve dials sitting under the mixer where they cannot be
        // seen but can still be clicked.
        for (auto& fxUnit : rack)
        {
            if (fxUnit.section != nullptr)
                fxUnit.section->setVisible(false);

            for (auto& knob : fxUnit.knobs)
                if (knob != nullptr)
                    knob->setVisible(false);
        }

        return;
    }

    rackArea.removeFromLeft(6);

    const int units = drums::numFxUnits;

    for (int u = 0; u < units; ++u)
    {
        auto& fxUnit = rack[(size_t) u];

        // Measured from the edges, the same arithmetic the strips use, so four
        // boxes fill the column exactly.
        const int top = rackArea.getY() + (rackArea.getHeight() * u) / units;
        const int bottom = rackArea.getY() + (rackArea.getHeight() * (u + 1)) / units;

        juce::Rectangle<int> box(rackArea.getX(), top, rackArea.getWidth(), bottom - top);

        if (fxUnit.section != nullptr)
        {
            fxUnit.section->setVisible(true);
            fxUnit.section->setBounds(box.reduced(0, 3));
        }

        auto inner = box.reduced(8, 3);
        inner.removeFromTop(headerHeight);

        const int count = (int) fxUnit.knobs.size();

        for (int k = 0; k < count; ++k)
        {
            const int left = inner.getX() + (inner.getWidth() * k) / count;
            const int right = inner.getX() + (inner.getWidth() * (k + 1)) / count;

            if (fxUnit.knobs[(size_t) k] != nullptr)
            {
                fxUnit.knobs[(size_t) k]->setVisible(true);
                fxUnit.knobs[(size_t) k]->setBounds(
                    juce::Rectangle<int>(left, inner.getY(), right - left, inner.getHeight())
                        .reduced(2, 0));
            }
        }
    }
}

void DrumPanel::paint(juce::Graphics& g)
{
    g.fillAll(ui::colours::background);
}

void DrumPanel::paintTriggerNotes(juce::Graphics& g)
{
    // The trigger note under each name.
    //
    // OVER the children, from paintOverChildren, and it has to be. The Drum
    // Kit box behind the strips is itself a child - a FlowSection - and fills
    // its whole area with the panel colour at 62% opacity. Drawn from paint(),
    // these sat UNDER that fill, and it was measured rather than guessed:
    // textDim (139,147,166) seen through 62% of panel (28,32,41) comes to
    // exactly the (70,76,88) the running app showed, where a dial name at the
    // same size and colour reaches (139,147,166). The first build had them at
    // 1.6:1 against the page and unreadable. The row they sit in holds no
    // control, so drawing over the children here covers nothing.
    //
    // Name AND number - "C1 (36)" - because the two do not travel together.
    // Every DAW agrees that this note is 36; they do not agree what to call
    // it. WaveLathe names it C1, FL Studio and Reaper name the same note C2,
    // and somebody comparing the two readouts without the number has a
    // mismatch to explain and nothing to explain it with.
    const auto& map = processorRef.getDrumNoteMap();
    const auto transpose = juce::jlimit(-project::maxDrumTranspose, project::maxDrumTranspose,
                                        (int) std::round(
                                            processorRef.getParameters().drumTranspose.load()));
    const auto learning = map.getLearnTarget();

    g.setFont(juce::Font(juce::FontOptions(noteFontHeight)));

    for (int voice = 0; voice < project::numDrumVoices; ++voice)
    {
        const auto& strip = strips[(size_t) voice];

        if (strip.noteArea.isEmpty())
            continue;

        if (voice == learning)
        {
            // Said on the strip being taught rather than only on the LCD, so
            // the thing you are about to hit a pad for is the thing that looks
            // like it is waiting.
            g.setColour(ui::colours::accent);
            g.drawText("hit a pad", strip.noteArea, juce::Justification::centred, false);
            continue;
        }

        const auto note = drumTriggerNote(map, voice, transpose);

        if (note < 0)
            continue;

        // A taught slot is brighter than one still on General MIDI. The kit is
        // usually all one or all the other, and when it is mixed the eye wants
        // to know which rows were touched without reading twelve labels.
        const bool taught = map.noteFor(voice) != DrumNoteMap::none;

        g.setColour(taught ? ui::colours::accentWarm : ui::colours::textDim);

        g.drawText(juce::MidiMessage::getMidiNoteName(note, true, true, 3)
                       + " (" + juce::String(note) + ")",
                   strip.noteArea, juce::Justification::centred, false);
    }
}

void DrumPanel::paintOverChildren(juce::Graphics& g)
{
    // First, so the wash below dims an unused voice's note along with the
    // rest of its strip rather than leaving it the brightest thing there.
    paintTriggerNotes(g);

    // A voice with nothing written on it is dimmed rather than hidden or
    // disabled. Hidden would make the mixer's twelve columns disagree with
    // the grid's twelve rows, which is the one thing making the two pages
    // readable together; disabled would stop you setting a level before
    // drawing a beat, which is a normal order to work in.
    //
    // Nothing is dimmed while the pattern is empty. Dimming is a contrast, and
    // twelve of twelve dimmed is not one - it is just a washed-out page, on
    // exactly the screen somebody sees first.
    //
    // The rack is never dimmed. It is not per voice, so there is no such thing
    // as a rack nobody is using - and a reverb turned down to nothing is still
    // a thing you are about to turn up.
    const bool anyUsed = std::any_of(voiceUsed.begin(), voiceUsed.end(), [](bool used) { return used; });

    if (!anyUsed)
        return;

    for (int voice = 0; voice < project::numDrumVoices; ++voice)
    {
        if (voiceUsed[(size_t) voice])
            continue;

        auto area = stripArea(voice);
        if (area.isEmpty())
            continue;

        g.setColour(ui::colours::background.withAlpha(0.45f));
        g.fillRect(area);
    }
}
} // namespace wavelathe
