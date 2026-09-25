// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include "Drums/DrumSample.h"

namespace wavelathe
{
// Turning a file on disk into something a drum slot can play.
//
// Its own unit rather than a method on the processor, and the reason is
// testability: everything here is decisions - what counts as silence, how much
// of the front to keep, what to do with a stereo file, when a recording is too
// long - and every one of them is worth a test that does not need a synth, an
// audio device or a host to run.
//
// It lives in wavelathe rather than in drums because it needs
// juce_audio_formats to know what a WAV is, and the drums library deliberately
// links neither that nor anything else that would tie it to this synth.
namespace drumsamples
{
// How far below the peak counts as silence at the ends of a recording.
//
// Relative to the peak rather than to full scale, because a drum hit recorded
// quietly has a quiet noise floor too and an absolute threshold would either
// keep all of its leading hiss or trim into a loud one. The absolute floor
// underneath it is a 16-bit least significant bit, so a file that is digital
// silence at the front is recognised as such however loud the hit is.
constexpr float silenceBelowPeak = 0.001f;   // -60 dB
constexpr float silenceFloor = 1.0f / 32768.0f;

// How much of the run-up to keep in front of the first sound, and how much
// room to leave after the last.
//
// Not zero. Trimming to the exact first sample above the threshold cuts into
// the attack of anything with a soft onset, and starting a recording on a
// non-zero sample is the click the voices spend three separate fades
// avoiding. Thirty-two samples is under a millisecond and costs nothing.
constexpr int preRollSamples = 32;
constexpr int postRollSamples = 64;

// Decodes, sums to mono, trims the silence off both ends and caps the length.
//
// Returns null and fills `error` on anything that is not usable audio. The
// caller is expected to show that text: "it did not work" is not a message.
drums::SampleRef load(const juce::File& file, juce::String& error);

// The same, from audio that has already been decoded.
//
// Exists so the decisions above can be tested against audio built in a test
// rather than against a file somebody has to ship - and so that a caller who
// already has the samples does not have to write a WAV to use this.
drums::SampleRef fromAudio(const juce::AudioBuffer<float>& audio, double sourceRate,
                           const juce::String& name, juce::String& error);
} // namespace drumsamples
} // namespace wavelathe
