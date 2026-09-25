// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include <juce_audio_processors/juce_audio_processors.h>
#include "HostParameters.h"
#include "ParameterRegistry.h"
#include <cstdio>
#include <cmath>
#include <vector>

using namespace wavelathe;

namespace
{
int failures = 0;

void check(bool condition, const char* what)
{
    std::printf("%s: %s\n", condition ? "PASS" : "FAIL", what);
    if (!condition)
        ++failures;
}

bool nearly(float a, float b, float tolerance = 0.001f) { return std::abs(a - b) < tolerance; }
} // namespace

int main()
{
    std::printf("Registry: %d parameters\n\n", paramreg::count());

    const int cutoff = paramreg::idForName("Cutoff");   // logarithmic, 20 - 20000
    const int sustain = paramreg::idForName("Sustain"); // linear, 0 - 1
    const int semi = paramreg::idForName("Osc2 Semi");  // linear, -24 - 24

    check(cutoff >= 0 && sustain >= 0 && semi >= 0, "the parameters this suite uses all exist");

    // ---- A window, not a copy ----------------------------------------------
    // The whole design rests on this: the host parameter reads and writes the
    // same atomic the engine does. If it ever held a value of its own the two
    // would drift, and every bug after that would look like a different bug.
    std::printf("\nA window onto the engine's own value:\n");
    {
        SynthParameters params;
        HostParameter p{cutoff, params, nullptr};

        params.filterCutoffHz = 2000.0f;
        check(nearly(p.getValue(), paramreg::readNormalised(params, cutoff)),
              "a value written behind its back is what it reports");

        p.setValue(0.25f);
        check(nearly(paramreg::readNormalised(params, cutoff), 0.25f),
              "and what it is told, the engine has");
    }

    std::printf("\nThe ends of the range, where a skew shows up:\n");
    {
        SynthParameters params;
        HostParameter p{cutoff, params, nullptr};

        p.setValue(0.0f);
        check(nearly(params.filterCutoffHz.load(), 20.0f, 0.01f), "zero is the bottom of the range");

        p.setValue(1.0f);
        check(nearly(params.filterCutoffHz.load(), 20000.0f, 1.0f), "one is the top");

        // The midpoint of a LOGARITHMIC range is the geometric mean, not the
        // arithmetic one: 632 Hz, not 10010. Getting this wrong would put every
        // automation curve in the wrong place while still passing at both ends.
        p.setValue(0.5f);
        check(nearly(params.filterCutoffHz.load(), std::sqrt(20.0f * 20000.0f), 1.0f),
              "and the middle is the geometric mean, because the range is logarithmic");
    }

    std::printf("\nDefaults come from the engine, not from a second list:\n");
    {
        SynthParameters params;
        const SynthParameters fresh;
        bool allMatch = true;
        const char* firstMismatch = nullptr;

        for (int id = 0; id < paramreg::count(); ++id)
        {
            HostParameter p{id, params, nullptr};

            if (!nearly(p.getDefaultValue(), paramreg::readNormalised(fresh, id)))
            {
                allMatch = false;
                if (firstMismatch == nullptr)
                    firstMismatch = paramreg::name(id);
            }
        }

        if (!allMatch)
            std::printf("      first mismatch: %s\n", firstMismatch);

        check(allMatch, "every parameter's default matches a freshly built engine");

        HostParameter s{sustain, params, nullptr};
        check(s.getDefaultValue() >= 0.0f && s.getDefaultValue() <= 1.0f,
              "and a default is a normalised value, not a real one");
    }

    // ---- Text, which is what a host actually shows -------------------------
    std::printf("\nText a person can read:\n");
    {
        SynthParameters params;
        HostParameter p{cutoff, params, nullptr};

        // 0.5 on a log range from 20 to 20000 is 632.5 Hz.
        check(p.getText(0.5f, 0) == "632", "the middle of the cutoff range reads as its real value");
        check(p.getText(0.0f, 0) == "20.0", "and the bottom reads 20, not 0");

        HostParameter n{semi, params, nullptr};
        check(n.getText(0.5f, 0) == "0.00", "a range through zero reads zero in the middle");

        check(p.getText(0.5f, 3).length() <= 3, "and it honours the length a host asks for");
    }

    std::printf("\nAnd text back again:\n");
    {
        SynthParameters params;
        HostParameter p{cutoff, params, nullptr};

        check(nearly(p.getValueForText("20"), 0.0f, 0.001f), "the bottom of the range round-trips");
        check(nearly(p.getValueForText("20000"), 1.0f, 0.001f), "so does the top");
        check(nearly(p.getValueForText("632.46"), 0.5f, 0.001f), "and so does the middle");

        // Out of range is clamped rather than wrapped or refused: a host may
        // hand over whatever a person typed into a box.
        check(nearly(p.getValueForText("999999"), 1.0f), "something far too big is clamped");
        check(nearly(p.getValueForText("-5"), 0.0f), "and so is something below the range");
    }

    // ---- Who is told what --------------------------------------------------
    // The half of this that is easy to get wrong. A write FROM the host has to
    // announce itself, so the step sequencer's lane lets go of that dial. A
    // report TO the host must not, or a dial the user is turning would lock its
    // own lane out - and that case is already handled, by the hand-on-the-dial
    // rule the lanes have had since they were written.
    std::printf("\nWhich direction announces itself:\n");
    {
        SynthParameters params;
        std::vector<int> written;
        HostParameter p{cutoff, params, [&written](int id) { written.push_back(id); }};

        p.setValue(0.4f);
        check(written.size() == 1, "a write from the host says so");
        check(written.size() == 1 && written[0] == cutoff, "naming the parameter it wrote");

        written.clear();
        p.reportToHost();
        check(written.empty(), "a report TO the host does not - that is our dial moving, not theirs");
        check(nearly(paramreg::readNormalised(params, cutoff), 0.4f),
              "and reporting changes no value at all");
    }


    // ---- Parameters that have positions, not a range ------------------------
    // Four of the sixty-three are choices wearing a dial, and the engine rounds
    // them at the point of use - so a host drawing a smooth ramp across one is
    // drawing through values that can never sound. Telling it they are stepped
    // makes its automation lane snap, and its readout say something.
    std::printf("\nStepped parameters:\n");
    {
        SynthParameters params;

        const int filterType = paramreg::idForName("Filter Type");
        const int subOct = paramreg::idForName("Sub Oct");
        const int voices = paramreg::idForName("Voices");
        const int arpOct = paramreg::idForName("Arp Octaves");

        check(paramreg::steps(filterType) == 5, "Filter Type has five positions");
        check(paramreg::steps(subOct) == 2, "Sub Oct has two");
        check(paramreg::steps(voices) == 7, "Voices has seven");
        check(paramreg::steps(arpOct) == 3, "Arp Octaves has three");
        check(paramreg::steps(cutoff) == 0, "and Cutoff has none, being continuous");

        HostParameter stepped{filterType, params, nullptr};
        HostParameter smooth{cutoff, params, nullptr};

        check(stepped.isDiscrete(), "a stepped one says so to the host");
        check(stepped.getNumSteps() == 5, "and how many steps it has");
        check(!smooth.isDiscrete(), "a continuous one does not");
    }

    std::printf("\nAnd they read as words, not as numbers:\n");
    {
        SynthParameters params;
        const int filterType = paramreg::idForName("Filter Type");
        HostParameter p{filterType, params, nullptr};

        // 0, 0.25, 0.5, 0.75, 1 across a range of 0-4 are the five positions.
        check(p.getText(0.0f, 0) == "LP 12", "the bottom position is named");
        check(p.getText(0.75f, 0) == "BP 12", "and so is the one Live showed as 3.00");
        check(p.getText(1.0f, 0) == "Notch", "and the top");

        // The value between two positions is the one the ENGINE would round to,
        // so the host never shows a setting the synth would not play.
        check(p.getText(0.7f, 0) == "BP 12", "a value between positions reads as the one it rounds to");

        const int subOct = paramreg::idForName("Sub Oct");
        HostParameter s{subOct, params, nullptr};
        check(s.getText(0.0f, 0) == "-1 oct", "Sub Oct reads in octaves");
        check(s.getText(1.0f, 0) == "-2 oct", "at both of its two positions");

        // No labels, so the number - but a whole one, not 1.00.
        const int voices = paramreg::idForName("Voices");
        HostParameter v{voices, params, nullptr};
        check(v.getText(0.0f, 0) == "1", "an unnamed step is a whole number");
        check(v.getText(1.0f, 0) == "7", "at the top as well as the bottom");
    }

    std::printf("\nAnd can be typed by name:\n");
    {
        SynthParameters params;
        const int filterType = paramreg::idForName("Filter Type");
        HostParameter p{filterType, params, nullptr};

        check(nearly(p.getValueForText("BP 12"), 0.75f), "a position can be typed by its name");
        check(nearly(p.getValueForText("bp 12"), 0.75f), "whatever case it is typed in");

        // The one that catches this out: read as a number, "LP 12" is zero -
        // which is also a real position, so a wrong answer here looks right.
        check(nearly(p.getValueForText("Notch"), 1.0f),
              "and a name that reads as zero still finds its own position");

        check(nearly(p.getValueForText("3"), 0.75f), "a number still works too");
    }

    std::printf("\nThe panel and the host agree on the names:\n");
    {
        // The dial reads its labels from the registry now rather than from its
        // own copy. The copy is what this guards against coming back.
        check(juce::String(paramreg::stepLabel(paramreg::idForName("Filter Type"), 1)) == "LP 24",
              "step 1 of Filter Type is LP 24, which is what the dial shows");
        check(paramreg::stepLabel(paramreg::idForName("Filter Type"), 5) == nullptr,
              "a step past the end has no name rather than a wrong one");
        check(paramreg::stepLabel(paramreg::idForName("Voices"), 0) == nullptr,
              "and a parameter with no names has none");
    }
    std::printf("\nEvery parameter can be built and named:\n");
    {
        SynthParameters params;
        bool allNamed = true;
        bool allDistinct = true;
        juce::StringArray ids;

        for (int id = 0; id < paramreg::count(); ++id)
        {
            HostParameter p{id, params, nullptr};

            if (p.getName(64) != juce::String(paramreg::name(id)))
                allNamed = false;

            // VST3 hashes this string into the id a saved session stores, so two
            // parameters sharing one would point a reopened Live set at the
            // wrong dial - silently, and only after saving and coming back.
            if (ids.contains(p.getParameterID()))
                allDistinct = false;

            ids.add(p.getParameterID());
        }

        check(ids.size() == paramreg::count(), "there is one for every registry entry");
        check(allNamed, "each carries the registry's own name");
        check(allDistinct, "and no two share a parameter ID");
    }

    std::printf("\n%s\n",
                failures == 0 ? "ALL HOST PARAMETER TESTS PASSED" : "SOME HOST PARAMETER TESTS FAILED");
    return failures == 0 ? 0 : 1;
}
