// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>

namespace wavelathe
{
namespace ui
{
// ---- Palette ---------------------------------------------------------------
namespace colours
{
const juce::Colour background{0xff14161a};
const juce::Colour panel{0xff1c2029};
const juce::Colour panelEdge{0xff2b313d};
const juce::Colour knobTrack{0xff2b313d};
const juce::Colour accent{0xff4dd0e1};     // voice signal chain
const juce::Colour accentWarm{0xffffb454}; // master effects
const juce::Colour accentMod{0xffb794f6};  // modulation sources
const juce::Colour textPrimary{0xffe4e8f0};
const juce::Colour textDim{0xff8b93a6};
const juce::Colour flowLine{0xff3a4152};
const juce::Colour lcdBackground{0xff0b1a10};
const juce::Colour lcdText{0xff6dffa8};
const juce::Colour lcdDim{0xff1d3b28};
} // namespace colours

// ---- Readout numbers ---------------------------------------------------------
// This many decimals, or a whole number when that is zero. Not juce::String(v,
// 0): JUCE reads zero places as "the stream's default format", six significant
// figures, which is how the mastering threshold came to read -10.5827dB and a
// cutoff above 10k would have read 12.3456k.
inline juce::String toFixed(double v, int places)
{
    return places > 0 ? juce::String(v, places) : juce::String(juce::roundToInt(v));
}

// ---- Icons -----------------------------------------------------------------
// Small vector glyphs drawn per parameter, so each dial reads at a glance.
enum class IconType
{
    wave,
    voices,
    detune,
    width,
    cutoff,
    resonance,
    drive,
    attack,
    decay,
    sustain,
    release,
    lfoRate,
    lfoToFilter,
    lfoToAmp,
    gain,
    distortion,
    delayTime,
    delayFeedback,
    delayMix,
    reverbSize,
    reverbMix,
    modEnvToWave,
    modEnvToCutoff,
    lfoToWave,
    pitch,
    subOsc,
    noise,
    filterType
};

void drawParameterIcon(juce::Graphics& g, IconType icon, juce::Rectangle<float> area, juce::Colour colour);

// ---- Modern rotary look ----------------------------------------------------
class WaveLatheLookAndFeel : public juce::LookAndFeel_V4
{
public:
    WaveLatheLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                          float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;

    juce::Font getLabelFont(juce::Label&) override;

    // JUCE draws tooltips bold and centred by default, which is the wrong
    // shape for anything longer than two or three words: bold at 13px is
    // heavy to read, and centred prose leaves every line starting in a
    // different place so the eye has to find the left edge again each time.
    // These draw it plain and ranged left, in the panel colours.
    juce::Rectangle<int> getTooltipBounds(const juce::String& tipText, juce::Point<int> screenPos,
                                          juce::Rectangle<int> parentArea) override;
    void drawTooltip(juce::Graphics&, const juce::String& text, int width, int height) override;

private:
    // Shared by the two above so the box is measured and drawn the same way.
    static juce::TextLayout layoutTooltip(const juce::String& text, float width);
};

// ---- One parameter dial ----------------------------------------------------
// The dial itself always moves in 128 equal MIDI-style steps (0-127), which is
// both what a MIDI CC sends and what the readout shows. The real parameter
// value is derived from that step through a NormalisableRange, so skewed
// ranges (cutoff, times) still feel natural to turn.
class ParameterKnob : public juce::Component,
                      private juce::Slider::Listener,
                      private juce::Timer
{
public:
    ParameterKnob(const juce::String& name, IconType icon, juce::NormalisableRange<double> range,
                  double initialValue, std::function<juce::String(double)> valueFormatter);
    ~ParameterKnob() override;

    void paint(juce::Graphics&) override;

    // The value is drawn after the dial, into the wedge its arc leaves open at
    // the bottom - space that was showing nothing, and the one place on a dial
    // where text cannot collide with the arc or the pointer.
    void paintOverChildren(juce::Graphics&) override;
    void resized() override;

    // Called with (realValue, displayText) whenever the dial moves.
    std::function<void(double, juce::String)> onValueChanged;

    // Called once when a dial starts being moved, not for every step of the
    // move - so a sweep across the whole range is one thing you did. A new
    // gesture begins when the dial has been still long enough for its readout
    // to settle, which is the same span that already governs the highlight.
    std::function<void()> onGestureStart;

    // Called once when that move finishes. Paired with onGestureStart, and the
    // pairing is the point: a host told a gesture began and never told it ended
    // believes the control is still under your hand, and holds its own
    // automation off it indefinitely.
    //
    // A drag ends when the mouse comes up. The wheel and the arrow keys have no
    // such moment, so those end when the dial settles - the same 900ms that
    // already governs the readout, and the same span that started the gesture.
    std::function<void()> onGestureEnd;

    void setRealValue(double newValue);

    // Swaps what the dial measures. A rate dial reads in note divisions when it
    // is following the tempo and in plain speed when it is not, which is how
    // every synced rate control states itself - the units tell you which grid
    // you are on.
    void setScale(juce::NormalisableRange<double> newRange,
                  std::function<juce::String(double)> newFormatter, double valueToKeep);
    double getRealValue() const;
    int getMidiStep() const { return (int) slider.getValue(); }
    juce::String getName() const { return name; }
    juce::String getFormattedValue() const;

    // True while the dial is being moved and for a short moment after, which is
    // what the sequencer uses to decide whether you or a recorded lane is in
    // charge of this parameter.
    bool isRecentlyMoved() const { return showValue; }

private:
    void sliderValueChanged(juce::Slider*) override;

    // A gesture is the mouse going down and coming up again, however long it
    // lingers in between. Working it out from the value readout's timeout
    // instead - which only restarts when the value actually changes - split a
    // slow, careful adjustment into several, so undoing it went back only part
    // of the way.
    void sliderDragStarted(juce::Slider*) override;
    void sliderDragEnded(juce::Slider*) override;
    bool dragging = false;

    // Whether a gesture has been announced and not yet closed. Guards against
    // both halves of the mistake: a second begin, and an end with no begin.
    bool gestureOpen = false;
    void beginGesture();
    void endGesture();
    void timerCallback() override;

    // Works out where along the dial the displayed value changes, so a dial
    // with only a few settings can show them as notches.
    void markValueBoundaries();

    // A cell much wider than it is tall has room beside the dial rather than
    // only above and below it. Putting the name and value there instead lets
    // the dial grow to the full height of the cell - roughly twice the size it
    // manages when it has to share that height with a row of text.
    bool usesSideLayout() const;
    juce::Rectangle<int> dialArea() const;
    juce::Rectangle<int> labelArea() const;

    juce::String name;
    IconType icon;
    juce::NormalisableRange<double> range;
    std::function<juce::String(double)> formatter;
    juce::Slider slider;

    // The dial's own arc already shows where it sits, so the number only
    // appears while you are actually turning it, drawn over the dial face
    // rather than taking up a row of its own.
    bool showValue = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParameterKnob)
};

// ---- LCD message panel -----------------------------------------------------
// A small character display for status messages, styled like an old backlit
// LCD module: a few fixed lines that scroll, on a lit green panel.
class LcdDisplay : public juce::Component
{
public:
    explicit LcdDisplay(int numLines = 3);

    void paint(juce::Graphics&) override;

    // Pushes a new message, scrolling older lines up.
    void print(const juce::String& message);
    // Replaces the bottom line (for rapidly-updating readouts).
    void printTransient(const juce::String& message);

private:
    int numLines;
    juce::StringArray lines;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LcdDisplay)
};

// ---- Signal-flow section frame --------------------------------------------
// A labelled panel behind a group of dials, so the layout reads as the order
// the sound actually passes through: oscillator, filter, drive, amp, and so on.
// How a section sits in the signal path. Chain sections are drawn as
// interlocking chevrons - each points into the next and is notched to receive
// the previous - so the direction of flow is part of the shape itself, rather
// than a separate arrow that has to be positioned to match.
enum class SectionStyle
{
    chainStart,  // flat left edge, points right
    chainMiddle, // notched left, points right
    chainEnd,    // notched left, flat right
    standalone,  // plain panel, not part of a chain
    modulator    // plain panel, modulation colour
};

class FlowSection : public juce::Component
{
public:
    FlowSection(const juce::String& title, juce::Colour tint);

    void paint(juce::Graphics&) override;

    // stageNumber > 0 draws a badge showing its place in the chain.
    void setStyle(SectionStyle newStyle, int stageNumber = 0);
    juce::String getTitle() const { return title; }

    // A section whose heading changes with what it is showing - the sequencer's
    // grid is either Notes or Drums. Repaints itself, because a section draws
    // its own heading and nothing else would know to.
    void setTitle(const juce::String& newTitle)
    {
        if (title == newTitle)
            return;

        title = newTitle;
        repaint();
    }

    // Width of the interlocking notch/point, so layouts can overlap sections
    // by this much and have them mesh.
    static constexpr int chevronWidth = 14;

private:
    juce::String title;
    juce::Colour tint;
    SectionStyle style = SectionStyle::standalone;
    int stage = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FlowSection)
};
// ---- Transport buttons -----------------------------------------------------
// Play, stop and record, drawn as the shapes everything uses for them rather
// than written out as words. A triangle, a square and a filled circle are read
// faster than any label, and they stay readable at the size a transport button
// wants to be.
class TransportButton : public juce::Button
{
public:
    enum class Shape { play, stop, record };

    TransportButton(Shape shapeToDraw, juce::Colour tint);

    void paintButton(juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

    void setShape(Shape newShape);

    // Lit means the thing it does is currently happening - playing, or armed.
    void setLit(bool shouldBeLit);

private:
    Shape shape;
    juce::Colour colour;
    bool lit = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TransportButton)
};

// ---- Full screen -----------------------------------------------------------
// The corner brackets every application draws for this, and nothing else. The
// icon is the whole button: it lives in the title bar beside minimise and
// close, where there is room for a glyph and none for a word.
//
// It draws the OPPOSITE of the current state - brackets pointing outwards while
// the window is small, inwards while it is full - so it shows what pressing it
// will do rather than what it already did.
class FullScreenButton : public juce::Button
{
public:
    FullScreenButton();

    void paintButton(juce::Graphics&, bool isMouseOver, bool isButtonDown) override;

    void setExpanded(bool isFullScreen);

private:
    bool expanded = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FullScreenButton)
};

// ---- Page tabs -------------------------------------------------------------
// The two pages of the synth, drawn as tabs rather than one button that
// changes its own name. A button that says "Sequencer" has to be read twice -
// once to see what it says and once to work out whether that is where you are
// or where it will take you. Tabs show both pages at once and mark the one you
// are on, so there is nothing to work out.
class PageTabs : public juce::Component
{
public:
    explicit PageTabs(juce::StringArray tabNames);

    void paint(juce::Graphics&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;

    void setSelected(int index, juce::NotificationType notify = juce::sendNotification);
    int getSelected() const { return selected; }

    // The width the tabs need to sit side by side without crowding.
    int getPreferredWidth() const;

    std::function<void(int)> onTabSelected;

private:
    juce::Rectangle<int> boundsForTab(int index) const;
    int tabAt(juce::Point<int> position) const;

    juce::StringArray names;
    int selected = 0;
    int hovered = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PageTabs)
};

} // namespace ui
} // namespace wavelathe
