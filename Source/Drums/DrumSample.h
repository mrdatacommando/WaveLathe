// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>
#include <vector>

#include "DrumParameters.h"

namespace drums
{
// ---- A loaded sample, and the sixteen slots one can sit in -----------------
//
// Deliberately dumb: floats, a rate and a name. No decoding, no file, no
// format - this library links juce_core and juce_audio_basics and nothing
// that knows what a WAV is. Whatever loads the audio hands one of these over
// already decoded, which is also what lets the preset reader build one from
// bytes it read itself without going near an audio format.
//
// MONO, and that is a decision rather than a shortcut. A kit voice has a Pan
// control, and Pan is what you give a mono source; a stereo file is summed on
// the way in. It also halves both the memory and the size of a saved project,
// and the thing being loaded is almost always one drum hit.

// The longest a slot will hold, and the number is a file-size decision.
//
// Ten seconds is far past any drum hit and still bounds a project: sixteen
// slots of it at 44.1kHz is about twenty-eight megabytes, which is large for
// a project file but is what "the samples travel with it" costs. A loop
// longer than this is trimmed rather than refused - refusing would be the
// more annoying half of the same answer.
constexpr double maxSampleSeconds = 10.0;

struct Sample
{
    std::vector<float> data;

    // The rate the file was recorded at, NOT the rate it will play at.
    //
    // Kept rather than resampled, and the playback ratio does the work. That
    // is the same interpolation Tune needs anyway, so doing it here would be
    // doing it twice - and it means a project moved between a 44.1k session
    // and a 96k one is right without having been converted.
    double sourceRate = 44100.0;

    // What to call it on the strip. The file's name without its extension.
    juce::String name;

    bool isEmpty() const { return data.empty(); }
    int length() const { return (int) data.size(); }
    double seconds() const { return sourceRate > 0.0 ? (double) data.size() / sourceRate : 0.0; }
};

// Shared and const, so that a bank, an undo snapshot and a saved project can
// all point at one copy.
//
// Undo is the reason. A snapshot is taken every time a dial is touched, and a
// snapshot that deep-copied sixteen samples would make turning a knob cost
// twenty-eight megabytes.
using SampleRef = std::shared_ptr<const Sample>;

// Which voices are playing a sample instead of their own circuit.
//
// A null slot means "this voice is synthesised", which is the default and is
// what fifteen of the sixteen usually are. Published to the audio thread as
// one pointer, so a voice can never see half a bank.
struct SampleBank
{
    std::array<const Sample*, numVoices> slots{};

    bool has(int voice) const
    {
        return voice >= 0 && voice < numVoices && slots[(size_t) voice] != nullptr;
    }
};

// ---- Letting go of a sample nobody is playing any more ---------------------
//
// A voice copies a raw Sample* out of the bank when it is struck and holds it
// for the whole hit, so taking a sample out of a slot does NOT mean the audio
// thread has finished with it. There is no cheap way to ask whether it has -
// the render path must not take a lock and must not touch a reference count.
//
// So the answer is the clock. A displaced sample is kept for longer than any
// hit could possibly still be running, and then dropped. The alternative -
// keeping every sample the session ever saw - was fine while loading one was
// a deliberate act, and stopped being fine the moment browsing a folder
// auditioned a file per keypress.
//
// The arithmetic, worst case:
//     10 s   the longest a slot will hold        (maxSampleSeconds)
//    x 2     the slowest Tune plays it at        (tuneRatioFor bottoms at 0.5)
//    + 6 s   the longest the sends ring after it (fxTailSeconds)
//    = 26 s
// Sixty is that with the doubt taken out. What it costs is a few displaced
// samples held a minute longer than needed, which is measured in megabytes;
// what it buys is never having to be right about the number.
constexpr juce::uint32 retirementGraceMs = 60000;

// Holds things that have been taken out of use until they are certainly not
// being read, then lets them go.
//
// Templated because samples and banks retire on the same clock for the same
// reason, and a SampleRef and a unique_ptr<SampleBank> have nothing else in
// common. Message thread only.
template <typename Held>
class RetiringStore
{
public:
    void retire(Held item, juce::uint32 nowMs)
    {
        items.push_back({std::move(item), nowMs});
        sweep(nowMs);
    }

    // Drops everything whose grace has run out. Cheap enough to call on every
    // retire, which is the only thing that does call it: a store nobody is
    // adding to holds whatever was last put in it until somebody does, and
    // that residue is a handful of entries rather than a session's worth.
    void sweep(juce::uint32 nowMs)
    {
        const auto expired = [nowMs](const Entry& entry)
        {
            // Both sides are unsigned, so the subtraction wraps with the
            // counter (which turns over every 49 days of uptime) and reads as
            // a small elapsed time rather than an enormous one. It is the
            // TYPES doing that, not anything written here - an earlier version
            // cast the result to uint32 as though the cast were the guard, and
            // a cast around a subtraction that is already unsigned does
            // nothing. Keep retiredAtMs unsigned and this stays true.
            return nowMs - entry.retiredAtMs >= retirementGraceMs;
        };

        items.erase(std::remove_if(items.begin(), items.end(), expired), items.end());
    }

    int size() const { return (int) items.size(); }

private:
    struct Entry
    {
        Held item;
        juce::uint32 retiredAtMs = 0;
    };

    std::vector<Entry> items;
};
} // namespace drums
