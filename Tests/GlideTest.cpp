// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "SynthVoice.h"
#include "SynthSound.h"
#include "MonoVoice.h"
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

using namespace wavelathe;

namespace
{
int failures = 0;

void check(bool condition, const juce::String& what)
{
    if (!condition)
    {
        std::cout << "  FAIL: " << what << std::endl;
        ++failures;
    }
}

constexpr double sampleRate = 44100.0;

// Long enough to hold several cycles of the lowest note measured here - a C3
// period is 7.6ms, so a 256-sample block would not even contain one - and short
// enough that a 500ms glide has barely started by the end of it, which is what
// makes the reading say where the slide BEGAN.
constexpr int blockSize = 2048;

float hzOf(int midiNote)
{
    return (float) juce::MidiMessage::getMidiNoteInHertz(midiNote);
}

// What pitch a buffer is actually sounding, by counting how often it rises
// through zero. A glide is a large, slow pitch movement, so this only has to
// tell one note from another - and unlike an FFT it needs no window long enough
// to blur the very movement being measured.
//
// With hysteresis, because a plain sign test does not survive a real waveform:
// the ripple either side of a band-limited saw's discontinuity crosses zero
// several times per cycle, and the first version of this counted every one of
// them and read C5 as a fifth too high. The signal now has to fall well below
// zero before another rise counts.
double soundingHz(const juce::AudioBuffer<float>& buffer)
{
    const auto* data = buffer.getReadPointer(0);
    const int numSamples = buffer.getNumSamples();

    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        peak = juce::jmax(peak, std::abs(data[i]));

    if (peak < 1.0e-4f)
        return 0.0; // silence has no pitch

    const float armLevel = -peak * 0.25f;
    const float fireLevel = peak * 0.25f;

    bool armed = false;
    int crossings = 0;
    int first = -1, last = -1;

    for (int i = 0; i < numSamples; ++i)
    {
        if (! armed && data[i] < armLevel)
        {
            armed = true;
        }
        else if (armed && data[i] > fireLevel)
        {
            armed = false;
            if (first < 0)
                first = i;
            last = i;
            ++crossings;
        }
    }

    if (crossings < 2 || last <= first)
        return 0.0;

    // Measured from the first crossing to the last rather than across the whole
    // buffer, so a partial cycle at either end does not drag the rate down.
    return (crossings - 1) * sampleRate / (double) (last - first);
}

void setupVoicePatch(SynthParameters& p)
{
    p.wavePosition = 0.0f;   // a plain shape, so zero crossings mean the pitch
    p.osc1Level = 1.0f;
    p.osc2Level = 0.0f;
    p.subLevel = 0.0f;
    p.noiseLevel = 0.0f;
    p.unisonVoices = 1.0f;
    p.unisonDetuneCents = 0.0f;
    p.attack = 0.001f;
    p.decay = 0.5f;
    p.sustain = 1.0f;
    p.release = 0.1f;
    p.filterType = 0.0f;
    p.filterCutoffHz = 18000.0f; // open, so nothing shapes the waveform
    p.filterResonance = 0.0f;
    p.modEnvToCutoff = 0.0f;
    p.lfoDepth = 0.0f;
    p.lfoAmpDepth = 0.0f;
    p.driveAmount = 0.0f;
    p.masterGain = 0.8f;
}

// Plays one note on one voice and reports the pitch of the FIRST block, which
// is where a glide has barely started and so still sounds like wherever it
// began.
double pitchAtNoteStart(SynthVoice& voice, int midiNote)
{
    juce::AudioBuffer<float> block(2, blockSize);
    block.clear();

    voice.startNote(midiNote, 1.0f, nullptr, 8192);
    voice.renderNextBlock(block, 0, blockSize);

    return soundingHz(block);
}
} // namespace

int main()
{
    std::cout << "Glide test - where a slide starts from" << std::endl << std::endl;

    auto tables = std::make_unique<WavetableSet>();
    WavetableProvider provider;
    provider.active.store(tables.get());

    // ---- The measure can tell one pitch from another -----------------------
    // Everything below is a comparison of pitches, so it is worth knowing the
    // pitch reading works before trusting what it says about glides.
    std::cout << "Sanity - the pitch reading:" << std::endl;
    {
        SynthParameters p;
        setupVoicePatch(p);
        p.glideTimeMs = 0.0f;

        // On the heap: a voice holds a filter pair and seven oscillators twice
        // over, which is more than a thread stack wants.
        auto voicePtr = std::make_unique<SynthVoice>(provider, p);
        auto& voice = *voicePtr;
        voice.prepare(sampleRate, blockSize);

        double low = pitchAtNoteStart(voice, 48);
        voice.stopNote(0.0f, false);

        double high = pitchAtNoteStart(voice, 72);
        voice.stopNote(0.0f, false);

        std::cout << "  C3 reads " << juce::String(low, 1) << " Hz (really "
                  << juce::String(hzOf(48), 1) << ")" << std::endl;
        std::cout << "  C5 reads " << juce::String(high, 1) << " Hz (really "
                  << juce::String(hzOf(72), 1) << ")" << std::endl;

        check(std::abs(low - hzOf(48)) < hzOf(48) * 0.15,
              "a note with glide off sounds at its own pitch");
        check(high > low * 3.0, "and two octaves up reads about four times higher");
    }

    // ---- A chord is not a melody -------------------------------------------
    // This is the bug the test exists for. The glide origin used to be a single
    // value on the shared parameters, read and then overwritten by every voice
    // as it started. Playing a chord, the second voice read the first voice's
    // pitch and slid up from it, the third read the second's, and so on - so a
    // chord smeared into existence from the bottom up rather than each note
    // sliding from its own previous pitch.
    //
    // Three fresh voices with nothing behind them is the clearest case: none of
    // them has anywhere to slide from, so all three should start on pitch.
    std::cout << std::endl << "A chord on three fresh voices:" << std::endl;
    {
        SynthParameters p;
        setupVoicePatch(p);
        p.glideTimeMs = 500.0f; // long, so a wrong origin is unmistakable

        const int chord[] = {48, 52, 55}; // C3 E3 G3

        std::vector<std::unique_ptr<SynthVoice>> voices;
        for (int i = 0; i < 3; ++i)
        {
            voices.push_back(std::make_unique<SynthVoice>(provider, p));
            voices.back()->prepare(sampleRate, blockSize);
        }

        bool allOnPitch = true;

        for (int i = 0; i < 3; ++i)
        {
            double heard = pitchAtNoteStart(*voices[(size_t) i], chord[i]);
            double wanted = hzOf(chord[i]);
            double errorRatio = heard / wanted;

            std::cout << "  " << juce::String(chord[i]).paddedRight(' ', 4) << "wanted "
                      << juce::String(wanted, 1).paddedRight(' ', 9) << "heard "
                      << juce::String(heard, 1).paddedRight(' ', 9) << "("
                      << juce::String(errorRatio, 2) << "x)" << std::endl;

            if (std::abs(errorRatio - 1.0) > 0.15)
                allOnPitch = false;
        }

        check(allOnPitch, "every note of a chord starts on its own pitch, not on its neighbour's");
    }

    // ---- But a line still glides -------------------------------------------
    // The fix must not simply switch glide off. One voice playing two notes in
    // succession - which is what mono mode always is, and what glide is mostly
    // used for - has to still slide from the first to the second.
    std::cout << std::endl << "One voice, two notes in a row:" << std::endl;
    {
        SynthParameters p;
        setupVoicePatch(p);
        p.glideTimeMs = 500.0f;

        // On the heap: a voice holds a filter pair and seven oscillators twice
        // over, which is more than a thread stack wants.
        auto voicePtr = std::make_unique<SynthVoice>(provider, p);
        auto& voice = *voicePtr;
        voice.prepare(sampleRate, blockSize);

        double first = pitchAtNoteStart(voice, 48);
        voice.stopNote(0.0f, false);

        double second = pitchAtNoteStart(voice, 72);

        std::cout << "  first note  C3, heard " << juce::String(first, 1) << " Hz" << std::endl;
        std::cout << "  then        C5, heard " << juce::String(second, 1) << " Hz  (C5 is "
                  << juce::String(hzOf(72), 1) << ")" << std::endl;

        check(std::abs(first - hzOf(48)) < hzOf(48) * 0.15,
              "the first note of all has nothing to slide from, so it starts on pitch");
        check(second < hzOf(72) * 0.75,
              "and the second note starts well below itself, still sliding up from the first");
    }

    // ---- Glide off means glide off -----------------------------------------
    std::cout << std::endl << "The same line with glide off:" << std::endl;
    {
        SynthParameters p;
        setupVoicePatch(p);
        p.glideTimeMs = 0.0f;

        // On the heap: a voice holds a filter pair and seven oscillators twice
        // over, which is more than a thread stack wants.
        auto voicePtr = std::make_unique<SynthVoice>(provider, p);
        auto& voice = *voicePtr;
        voice.prepare(sampleRate, blockSize);

        pitchAtNoteStart(voice, 48);
        voice.stopNote(0.0f, false);

        double second = pitchAtNoteStart(voice, 72);

        std::cout << "  after C3, C5 heard " << juce::String(second, 1) << " Hz" << std::endl;

        check(std::abs(second - hzOf(72)) < hzOf(72) * 0.15,
              "with glide off a note starts exactly where it should");
    }

    // ---- When one voice may pick up where another left off -----------------
    // An earlier version of this test asserted that a voice NEVER slides from a
    // pitch a different voice played. That was too strict, and measuring the
    // real allocator is what showed it: in poly, a line played faster than its
    // own release tail lands on a fresh voice every single note, so a strict
    // per-voice rule means such a line never glides at all. It was measured at
    // nought notes out of five.
    //
    // What separates the two cases is not which voice, but whether a note was
    // RELEASED first. A line has one. A chord does not - its notes follow each
    // other with everything still held. So a fresh voice picks up the line from
    // the note that just stopped, and finds nothing to pick up when the previous
    // note is still sounding.
    std::cout << std::endl << "One voice picking up where another left off:" << std::endl;
    {
        SynthParameters p;
        setupVoicePatch(p);
        p.glideTimeMs = 500.0f;

        // A line: A sounds, A is released, then B starts. B should slide from
        // where A was, even though B has never sounded before.
        {
            auto aPtr = std::make_unique<SynthVoice>(provider, p);
            auto bPtr = std::make_unique<SynthVoice>(provider, p);
            auto& a = *aPtr;
            auto& b = *bPtr;
            a.prepare(sampleRate, blockSize);
            b.prepare(sampleRate, blockSize);

            pitchAtNoteStart(a, 36);          // voice A plays C2
            a.stopNote(0.0f, false);          // and the key comes up

            double heard = pitchAtNoteStart(b, 72); // voice B takes the next note

            std::cout << "  after C2 was released, a fresh voice's C5 starts at "
                      << juce::String(heard, 1) << " Hz  (C5 is " << juce::String(hzOf(72), 1)
                      << ")" << std::endl;

            check(heard < hzOf(72) * 0.75,
                  "a line carries on through whichever voice takes the next note");
        }

        // A chord: A sounds and is STILL HELD when B starts. B has nothing to
        // pick up, so it starts where it should.
        {
            auto aPtr = std::make_unique<SynthVoice>(provider, p);
            auto bPtr = std::make_unique<SynthVoice>(provider, p);
            auto& a = *aPtr;
            auto& b = *bPtr;
            a.prepare(sampleRate, blockSize);
            b.prepare(sampleRate, blockSize);

            pitchAtNoteStart(a, 36);          // voice A plays C2 and holds it
            double heard = pitchAtNoteStart(b, 72); // B joins while A still sounds

            std::cout << "  while C2 is still held,     a fresh voice's C5 starts at "
                      << juce::String(heard, 1) << " Hz" << std::endl;

            check(std::abs(heard - hzOf(72)) < hzOf(72) * 0.15,
                  "but a note joining one that is still sounding does not slide into it");
        }
    }

    // ---- A line through the real voice allocator ---------------------------
    // Everything above drives voices by hand. This is the case that decides
    // whether per-voice glide is actually better or just differently wrong,
    // because it is the one a player would notice: a single-note LINE in POLY
    // mode, allocated by juce::Synthesiser rather than by the test.
    //
    // findFreeVoice hands out the first voice that is NOT ACTIVE, and a voice
    // stays active all through its release tail. So a detached line should come
    // back to the same voice every time and glide, while a line played faster
    // than the release should walk down the voice list onto voices with no
    // history, and start each note on pitch instead.
    //
    // Measured by asking which voice the allocator picked, not by listening.
    // Listening was tried first and does not work here: with a short gap the
    // previous note is still a long way into a linear release - at 0.05s into
    // 0.3s it is still at about four fifths of its level - so the buffer holds
    // two notes at once and any pitch reading is of the mixture. The first
    // version of this section read 286 Hz for a note that had either started at
    // 523 or slid from 131, which is neither, and that is the giveaway.
    //
    // Which voice was chosen is exact, needs no audio, and answers the question
    // directly: a voice glides if and only if it last played a DIFFERENT note,
    // which is the rule startNote actually uses.
    std::cout << std::endl << "A line through the real voice allocator:" << std::endl;
    {
        SynthParameters p;
        setupVoicePatch(p);
        p.glideTimeMs = 500.0f;
        p.release = 0.30f; // a long tail, so the two cases differ clearly

        const int line[] = {48, 72, 48, 72, 48, 72}; // two octaves apart, C3 / C5

        auto runLine = [&](const char* what, double gapSeconds)
        {
            juce::Synthesiser synth;
            synth.addSound(new SynthSound());

            constexpr int numVoices = 8;
            for (int i = 0; i < numVoices; ++i)
                synth.addVoice(new SynthVoice(provider, p));

            synth.setCurrentPlaybackSampleRate(sampleRate);
            for (int i = 0; i < numVoices; ++i)
                if (auto* v = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
                    v->prepare(sampleRate, blockSize);

            const double holdSeconds = 0.25;
            const int holdBlocks = juce::jmax(1, (int) (holdSeconds * sampleRate / blockSize));
            const int gapBlocks = juce::jmax(1, (int) (gapSeconds * sampleRate / blockSize));

            juce::AudioBuffer<float> block(2, blockSize);

            int glided = 0;
            int following = 0;

            std::cout << "  " << juce::String(what).paddedRight(' ', 22);

            for (size_t n = 0; n < sizeof(line) / sizeof(line[0]); ++n)
            {
                const int note = line[n];

                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, note, 1.0f), 0);

                block.clear();
                synth.renderNextBlock(block, midi, 0, blockSize);

                // Which voice took it.
                int chosen = -1;
                for (int i = 0; i < numVoices; ++i)
                {
                    auto* v = synth.getVoice(i);
                    if (v->isVoiceActive() && v->getCurrentlyPlayingNote() == note)
                    {
                        chosen = i;
                        break;
                    }
                }

                // Read from the voice itself rather than worked out here, so this
                // cannot drift away from the rule it is checking.
                float startedAt = 0.0f;
                if (chosen >= 0)
                    if (auto* v = dynamic_cast<SynthVoice*>(synth.getVoice(chosen)))
                        startedAt = v->getGlideStartHz();

                const bool wouldGlide = startedAt > 20.0f;

                if (n > 0)
                {
                    ++following;
                    glided += wouldGlide ? 1 : 0;
                }

                std::cout << " v" << chosen << "/" << juce::String(startedAt, 0).paddedLeft(' ', 4);

                for (int b = 1; b < holdBlocks; ++b)
                {
                    juce::MidiBuffer empty;
                    block.clear();
                    synth.renderNextBlock(block, empty, 0, blockSize);
                }

                juce::MidiBuffer off;
                off.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                block.clear();
                synth.renderNextBlock(block, off, 0, blockSize);

                for (int b = 1; b < gapBlocks; ++b)
                {
                    juce::MidiBuffer empty;
                    block.clear();
                    synth.renderNextBlock(block, empty, 0, blockSize);
                }
            }

            std::cout << "    " << glided << " of " << following << " would slide" << std::endl;
            return glided;
        };

        std::cout << "  (voice / the pitch its glide started from, 0 meaning it started on its own note)"
                  << std::endl;

        // A gap longer than the release: every voice is free again by the next
        // note, so the allocator hands back the same one.
        const int detached = runLine("detached, 0.5s gaps", 0.5);

        // A gap shorter than the release: the previous voice is still sounding
        // its tail, so the allocator reaches for the next one along.
        const int quick = runLine("quick, 0.05s gaps", 0.05);

        std::cout << "  detached " << detached << " of 5;   quick " << quick << " of 5" << std::endl;

        check(detached >= 4, "a detached line glides, the allocator reusing the one voice");
        check(quick >= 4, "and so does a line played faster than the release tail");
    }

    // ---- Mono, through the whole chain -------------------------------------
    // The 0.23.2 note claimed mono portamento was unaffected, "there being only
    // one voice". That was wrong, and worth correcting rather than quietly
    // dropping: MonoVoice rewrites the MIDI so one note SOUNDS at a time, it
    // does not reduce the voice count. The Synthesiser still holds eight and
    // still cycles them while old ones finish their release tails.
    //
    // Worse than the poly case, in fact. With legato off, MonoVoice emits the
    // note-off for the old note and the note-on for the new one at the SAME
    // SAMPLE, so the old voice is always still sounding when the new note
    // starts, and the new note therefore always lands on a voice with no history
    // at all. Under a per-voice rule alone that is not "sometimes" - it is every
    // single note.
    //
    // Legato is a different path again: nothing is emitted, and the sounding
    // voice follows monoTargetNote from inside renderNextBlock. Nothing had ever
    // exercised it.
    //
    // So this runs the real chain - MIDI through MonoVoice, then through the
    // Synthesiser, then the voices - rather than any part of it in isolation.
    std::cout << std::endl << "Mono, through the whole chain:" << std::endl;
    {
        struct MonoRig
        {
            juce::Synthesiser synth;
            MonoVoice mono;
            SynthParameters& p;
            juce::AudioBuffer<float> block{2, blockSize};

            MonoRig(SynthParameters& params, const WavetableProvider& provider) : p(params)
            {
                synth.addSound(new SynthSound());

                for (int i = 0; i < 8; ++i)
                    synth.addVoice(new SynthVoice(provider, p));

                synth.setCurrentPlaybackSampleRate(sampleRate);

                for (int i = 0; i < 8; ++i)
                    if (auto* v = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
                        v->prepare(sampleRate, blockSize);
            }

            double run(juce::MidiBuffer midi)
            {
                mono.process(midi, p);
                block.clear();
                synth.renderNextBlock(block, midi, 0, blockSize);
                return soundingHz(block);
            }

            double press(int note)
            {
                juce::MidiBuffer m;
                m.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
                return run(m);
            }

            double lift(int note)
            {
                juce::MidiBuffer m;
                m.addEvent(juce::MidiMessage::noteOff(1, note), 0);
                return run(m);
            }

            double idle() { return run({}); }

            // What the voice now playing this note started its glide from, read
            // from the voice rather than worked out here.
            //
            // The NEWEST voice playing it, which matters: falling back to a key
            // still under your finger returns to a note whose original voice may
            // well still be finishing its release, so a naive scan finds that one
            // instead and reports the wrong note's glide. It did exactly that.
            float originOf(int note)
            {
                SynthVoice* newest = nullptr;

                for (int i = 0; i < 8; ++i)
                {
                    auto* v = dynamic_cast<SynthVoice*>(synth.getVoice(i));
                    if (v == nullptr || ! v->isVoiceActive() || v->getCurrentlyPlayingNote() != note)
                        continue;

                    if (newest == nullptr || newest->wasStartedBefore(*v))
                        newest = v;
                }

                return newest != nullptr ? newest->getGlideStartHz() : -1.0f;
            }

            // What the voice on this note is sounding right now. Used on a
            // voice that is finishing its release while a different note plays,
            // where the buffer holds both and no measure of the sum could say
            // which pitch belonged to which.
            float currentHzOf(int note)
            {
                for (int i = 0; i < 8; ++i)
                {
                    auto* v = dynamic_cast<SynthVoice*>(synth.getVoice(i));
                    if (v != nullptr && v->isVoiceActive() && v->getCurrentlyPlayingNote() == note)
                        return v->getCurrentHz();
                }

                return -1.0f;
            }

            int activeVoices()
            {
                int n = 0;
                for (int i = 0; i < 8; ++i)
                    if (auto* v = dynamic_cast<SynthVoice*>(synth.getVoice(i)))
                        n += v->isVoiceActive() ? 1 : 0;
                return n;
            }
        };

        // ---- Legato off: a line, one key overlapping the next ---------------
        std::cout << "  legato off, keys overlapping:" << std::endl;
        {
            SynthParameters p;
            setupVoicePatch(p);
            p.glideTimeMs = 500.0f;
            p.release = 0.30f;
            p.monoMode = 1.0f;
            p.legatoMode = 0.0f;

            auto rigPtr = std::make_unique<MonoRig>(p, provider);
            auto& rig = *rigPtr;

            rig.press(48);              // C3
            for (int i = 0; i < 3; ++i) rig.idle();

            rig.press(72);              // C5 pressed while C3 is still held
            const float origin = rig.originOf(72);

            std::cout << "    C5 taking over from C3 starts its glide at "
                      << juce::String(origin, 1) << " Hz  (C3 is " << juce::String(hzOf(48), 1)
                      << ")" << std::endl;

            check(std::abs(origin - hzOf(48)) < hzOf(48) * 0.1f,
                  "a mono line glides from the note it took over from");
        }

        // ---- Legato off: releasing back to a key still held ------------------
        // MonoVoice's own comment calls this most of what makes a mono synth
        // playable: let the newer key go while an older one is still down and
        // the line falls back to it. That is another note-off and note-on at the
        // same sample, so it is the same case again.
        std::cout << "  legato off, falling back to a held key:" << std::endl;
        {
            SynthParameters p;
            setupVoicePatch(p);
            p.glideTimeMs = 500.0f;
            p.release = 0.30f;
            p.monoMode = 1.0f;
            p.legatoMode = 0.0f;

            auto rigPtr = std::make_unique<MonoRig>(p, provider);
            auto& rig = *rigPtr;

            rig.press(48);
            for (int i = 0; i < 3; ++i) rig.idle();
            rig.press(72);
            for (int i = 0; i < 3; ++i) rig.idle();

            rig.lift(72);               // C5 released, C3 still under a finger
            const float origin = rig.originOf(48);

            std::cout << "    falling back to C3 starts its glide at "
                      << juce::String(origin, 1) << " Hz  (C5 is " << juce::String(hzOf(72), 1)
                      << ")" << std::endl;

            check(std::abs(origin - hzOf(72)) < hzOf(72) * 0.1f,
                  "and slides back down to the key still under your finger");
        }

        // ---- Legato on: no note-on at all ------------------------------------
        // The pitch changes from inside renderNextBlock, following
        // monoTargetNote, so there is nothing for startNote to decide and the
        // only way to see it is to listen. One voice sounds throughout, so the
        // reading is clean.
        std::cout << "  legato on, the sounding note changing pitch:" << std::endl;
        {
            SynthParameters p;
            setupVoicePatch(p);
            p.glideTimeMs = 500.0f;
            p.monoMode = 1.0f;
            p.legatoMode = 1.0f;

            auto rigPtr = std::make_unique<MonoRig>(p, provider);
            auto& rig = *rigPtr;

            rig.press(48);
            for (int i = 0; i < 3; ++i) rig.idle();

            std::cout << "    holding C3 at " << juce::String(rig.idle(), 1) << " Hz" << std::endl;

            // One voice is sounding and nothing else, so the two ways of
            // asking what pitch it is on have to agree. The section below
            // relies on getCurrentHz where no measure of the audio could work,
            // and this is where that instrument is calibrated against the ear.
            {
                const double heard = rig.idle();
                const float reported = rig.currentHzOf(48);

                std::cout << "    zero crossings hear " << juce::String(heard, 1)
                          << " Hz, the voice reports " << juce::String(reported, 1)
                          << " Hz" << std::endl;

                check(std::abs(heard - reported) < reported * 0.05f,
                      "what the voice reports it is playing is what comes out of it");
            }

            rig.press(72); // C5 while C3 is held: no note-on is emitted at all

            std::cout << "    then C5 pressed:";

            // Long enough to actually arrive. The glide is exponential with a
            // 500ms time constant and a block is 46ms, so after six blocks -
            // which an earlier version of this checked - it is only
            // 1 - exp(-0.28/0.5) = 42% of the way in log space, about 235 Hz.
            // Twenty-four blocks is 1.1s, or 89%, which is 450 Hz and audibly
            // arrived. The reading below confirms the curve rather than the
            // threshold being fitted to it.
            constexpr int settleBlocks = 24;

            bool everBetween = false;
            double last = 0.0;

            for (int i = 0; i < settleBlocks; ++i)
            {
                last = rig.idle();

                if (i % 4 == 0 || i == settleBlocks - 1)
                    std::cout << juce::String(last, 0).paddedLeft(' ', 7);

                if (last > hzOf(48) * 1.2 && last < hzOf(72) * 0.85)
                    everBetween = true;
            }

            std::cout << std::endl;

            check(everBetween, "a legato change slides through the pitches between the two notes");
            check(last > hzOf(72) * 0.8, "and arrives at the note it was heading for");
        }

        // ---- Legato on: a note let go, still ringing, when the next starts --
        // The legato target is read by every ACTIVE voice, and a voice stays
        // active all through its release tail. So the question is whether a
        // note whose key is already up - decaying, finished, nothing to do with
        // the line any more - gets dragged along to the next note the line
        // reaches.
        //
        // It takes a gap to see it. Hold one key and press another and only one
        // voice ever sounds, which is why the section above could not find
        // this. Let the first key go, let the tail ring, then play the next
        // note: MonoVoice sees nothing sounding and emits a real note-on, the
        // allocator hands it to a second voice because the first is still busy,
        // and now there are two active voices reading the same target.
        //
        // Played at any speed shorter than the release - which is most playing,
        // on a patch with any tail at all - this is every note.
        std::cout << "  legato on, a released note still ringing:" << std::endl;
        {
            SynthParameters p;
            setupVoicePatch(p);
            p.glideTimeMs = 500.0f;
            p.release = 0.60f;      // a tail that outlasts the gap below
            p.monoMode = 1.0f;
            p.legatoMode = 1.0f;

            auto rigPtr = std::make_unique<MonoRig>(p, provider);
            auto& rig = *rigPtr;

            rig.press(48);                              // C3
            for (int i = 0; i < 3; ++i) rig.idle();

            rig.lift(48);                               // key up: the line is over
            const float atRelease = rig.currentHzOf(48);

            for (int i = 0; i < 2; ++i) rig.idle();     // about 93ms of tail

            rig.press(72);                              // C5, a new line entirely
            for (int i = 0; i < 6; ++i) rig.idle();     // 280ms, plenty of glide

            const float tailNow = rig.currentHzOf(48);
            const int voices = rig.activeVoices();

            std::cout << "    C3's tail was at " << juce::String(atRelease, 1)
                      << " Hz when the key came up, and reads "
                      << juce::String(tailNow, 1) << " Hz six blocks into C5  ("
                      << voices << " voices active)" << std::endl;

            check(voices >= 2, "the released note is still sounding while the new one plays");
            check(tailNow > 0.0f && std::abs(tailNow - hzOf(48)) < hzOf(48) * 0.05f,
                  "a note whose key is up stays on its own pitch instead of "
                  "following the line to the next note");
        }

        // ---- The shape a sequencer slide makes -------------------------------
        // SequencerTest proves the step sequencer emits the right events in the
        // right order. It cannot prove those events make a sound that slides -
        // it never renders anything. This does, through the same MonoVoice and
        // the same Synthesiser the synth uses, so the two halves meet.
        //
        // The shape is particular: a note-on for the new note and a note-off for
        // the old one, in that order, at the SAME sample. Nobody playing a
        // keyboard produces that. A sequencer tying one step into the next does,
        // every time.
        std::cout << "  a sequencer slide - on and off in the same instant:" << std::endl;
        {
            struct Reading
            {
                bool slid = false;
                double endedAt = 0.0;
                int voices = 0;
            };

            // Both orders, so that what is measured is the ordering rather than
            // merely the presence of two events.
            auto slideWith = [&](bool newNoteFirst)
            {
                SynthParameters p;
                setupVoicePatch(p);
                p.glideTimeMs = 500.0f;
                p.release = 0.30f;
                p.monoMode = 1.0f;
                p.legatoMode = 1.0f;

                auto rigPtr = std::make_unique<MonoRig>(p, provider);
                auto& rig = *rigPtr;

                rig.press(48); // C3, the step that is about to be tied onward
                for (int i = 0; i < 3; ++i)
                    rig.idle();

                juce::MidiBuffer tie;
                if (newNoteFirst)
                {
                    tie.addEvent(juce::MidiMessage::noteOn(1, 72, 0.9f), 0);
                    tie.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
                }
                else
                {
                    tie.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
                    tie.addEvent(juce::MidiMessage::noteOn(1, 72, 0.9f), 0);
                }

                rig.run(tie);

                // Read straight after the tie, while a voice that was restarted
                // would still be holding the old note's release tail.
                Reading reading;
                reading.voices = rig.activeVoices();

                for (int i = 0; i < 24; ++i)
                {
                    reading.endedAt = rig.idle();
                    if (reading.endedAt > hzOf(48) * 1.2 && reading.endedAt < hzOf(72) * 0.85)
                        reading.slid = true;
                }

                return reading;
            };

            auto tied = slideWith(true);
            auto abrupt = slideWith(false);

            std::cout << "    on-then-off: slid? " << (tied.slid ? "yes" : "no")
                      << ", ends at " << juce::String(tied.endedAt, 1) << " Hz, "
                      << tied.voices << " voice(s) sounding" << std::endl;
            std::cout << "    off-then-on: slid? " << (abrupt.slid ? "yes" : "no")
                      << ", ends at " << juce::String(abrupt.endedAt, 1) << " Hz, "
                      << abrupt.voices << " voice(s) sounding" << std::endl;

            check(tied.slid, "a sequencer slide bends through the pitches between the two steps");
            check(tied.endedAt > hzOf(72) * 0.8, "and arrives at the step it was tied to");

            // Both orders slide, and that is not the point.
            //
            // It was worth getting wrong once to find out: releasing first still
            // moves the pitch, because 0.23.3 has a fresh voice pick up from the
            // note that most recently stopped. What it cannot avoid is being a
            // FRESH VOICE - a new attack, the envelope from the top, the old
            // note left decaying beside it. That is a re-articulated note that
            // happens to arrive by a slide, not a tie.
            //
            // A tie is one voice bending and no retrigger, which is what a
            // slide means on the machines this borrows from. So the reading
            // that matters is how many voices are sounding, not what pitch
            // they are at.
            check(tied.voices == 1, "a tied step keeps the one voice, so the envelope never restarts");
            check(abrupt.voices > tied.voices,
                  "where releasing first starts a second voice, which is the retrigger the tie avoids");
        }
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL GLIDE TESTS PASSED" << std::endl;
    else
        std::cout << failures << " GLIDE TEST(S) FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
