// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "DrumSampleLoader.h"

#include <cmath>
#include <iostream>

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

constexpr double rate = 44100.0;

// A burst of tone with a given amount of digital silence in front of it and
// behind it. Nothing like a drum, deliberately: the silence is the thing
// under test and a shape with unambiguous edges is what makes it measurable.
// Takes the rate it is building FOR, which the first version did not: it
// counted samples at 44.1k and the file below was then written as 48k, so a
// "0.2 second" tone arrived 0.18 seconds long and the loader was blamed for
// it. A duration in seconds means nothing without the rate it is counted at.
juce::AudioBuffer<float> makeBurst(double leadingSeconds, double toneSeconds,
                                   double trailingSeconds, int numChannels = 1,
                                   float amplitude = 0.5f, double atRate = rate)
{
    const auto lead = (int) (leadingSeconds * atRate);
    const auto tone = (int) (toneSeconds * atRate);
    const auto trail = (int) (trailingSeconds * atRate);

    juce::AudioBuffer<float> buffer(numChannels, lead + tone + trail);
    buffer.clear();

    for (int channel = 0; channel < numChannels; ++channel)
        for (int i = 0; i < tone; ++i)
            buffer.setSample(channel, lead + i,
                             amplitude * std::sin(juce::MathConstants<float>::twoPi * 220.0f
                                                  * (float) i / (float) atRate));

    return buffer;
}

// Writes a buffer out as a real WAV and reads it back through the file path,
// so the decode half is exercised rather than assumed.
juce::File writeTempWav(const juce::AudioBuffer<float>& buffer, const juce::String& name,
                        double sampleRate = rate, int bitDepth = 24)
{
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getChildFile("WaveLatheDrumSampleTest")
                    .getChildFile(name + ".wav");

    file.getParentDirectory().createDirectory();
    file.deleteFile();

    std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());

    if (stream == nullptr)
        return {};

    juce::WavAudioFormat format;
    const auto options = juce::AudioFormatWriterOptions{}
                             .withSampleRate(sampleRate)
                             .withNumChannels(buffer.getNumChannels())
                             .withBitsPerSample(bitDepth);

    std::unique_ptr<juce::OutputStream> owned = std::move(stream);
    auto writer = format.createWriterFor(owned, options);

    if (writer == nullptr)
        return {};

    writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples());
    writer.reset();

    return file;
}
} // namespace

int main()
{
    std::cout << "Drum sample loader" << std::endl << std::endl;

    // ---- Silence comes off both ends ---------------------------------------
    //
    // The reason this matters is timing. A file with twenty milliseconds of
    // silence in front of the hit plays twenty milliseconds late, every time,
    // on every step - and a drum machine that is reliably late is worse than
    // one that is wrong, because it sounds like the groove rather than like a
    // fault.
    {
        juce::String error;
        auto sample = drumsamples::fromAudio(makeBurst(0.2, 0.1, 0.3), rate, "Burst", error);

        check(sample != nullptr, "a burst with silence around it loads: " + error);

        if (sample != nullptr)
        {
            // A tenth of a second of tone, plus the run-up and run-out the
            // loader deliberately keeps. Not exact, because the threshold
            // trims a little into the first and last cycles of a sine that
            // starts at zero.
            const auto seconds = sample->seconds();

            check(seconds > 0.09 && seconds < 0.12,
                  "and arrives about a tenth of a second long rather than six tenths ("
                      + juce::String(seconds, 4) + " s)");

            // The run-up is the point: the loader keeps a little silence in
            // front so the transient is not cut into and so the recording
            // does not begin on a non-zero sample.
            check(std::abs(sample->data.front()) < 0.01f,
                  "starting from something near silence rather than mid-waveform");
        }
    }

    // ---- A file that is already tight is left alone ------------------------
    {
        juce::String error;
        auto sample = drumsamples::fromAudio(makeBurst(0.0, 0.1, 0.0), rate, "Tight", error);

        check(sample != nullptr, "a burst with no silence at all loads: " + error);

        if (sample != nullptr)
            check(sample->seconds() > 0.09 && sample->seconds() < 0.11,
                  "and is still about a tenth of a second (" + juce::String(sample->seconds(), 4) + " s)");
    }

    // ---- Stereo is summed rather than doubled ------------------------------
    //
    // Both halves matter. A stereo file has to become mono because a kit voice
    // is a mono source with a Pan control - and it has to arrive at the same
    // level a mono file would, or loading the stereo version of a sample
    // would be loading a louder one.
    {
        juce::String error;
        auto fromMono = drumsamples::fromAudio(makeBurst(0.0, 0.1, 0.0, 1, 0.5f), rate, "M", error);
        auto fromStereo = drumsamples::fromAudio(makeBurst(0.0, 0.1, 0.0, 2, 0.5f), rate, "S", error);

        check(fromMono != nullptr && fromStereo != nullptr, "both load: " + error);

        if (fromMono != nullptr && fromStereo != nullptr)
        {
            const auto peakOf = [](const drums::SampleRef& s)
            {
                float peak = 0.0f;
                for (const auto value : s->data)
                    peak = juce::jmax(peak, std::abs(value));
                return peak;
            };

            check(std::abs(peakOf(fromMono) - peakOf(fromStereo)) < 0.01f,
                  "and the same hit in stereo arrives at the same level, not twice it ("
                      + juce::String(peakOf(fromMono), 3) + " against "
                      + juce::String(peakOf(fromStereo), 3) + ")");
        }
    }

    // ---- Nothing usable is refused with a reason ---------------------------
    //
    // A reason rather than a null. "It did not work" sends somebody back to
    // the file with nothing to go on; "that file is silent" is the whole
    // answer.
    {
        juce::String error;
        juce::AudioBuffer<float> silent(1, 44100);
        silent.clear();

        check(drumsamples::fromAudio(silent, rate, "Silent", error) == nullptr,
              "a silent file is refused");
        check(error.isNotEmpty() && error.containsIgnoreCase("silent"),
              "and says why: " + error);

        juce::String emptyError;
        juce::AudioBuffer<float> nothing(0, 0);
        check(drumsamples::fromAudio(nothing, rate, "Empty", emptyError) == nullptr
                  && emptyError.isNotEmpty(),
              "and so is a file with no audio in it: " + emptyError);
    }

    // ---- A recording longer than the cap is trimmed, not refused -----------
    //
    // Capped because the samples travel inside the project file, and one
    // careless drag of a ten-minute mixdown onto a drum slot should not turn
    // a project into hundreds of megabytes. Trimmed rather than refused
    // because the useful answer to a mistake is the first few seconds of it.
    {
        juce::String error;
        auto sample = drumsamples::fromAudio(makeBurst(0.0, drums::maxSampleSeconds + 5.0, 0.0),
                                             rate, "Long", error);

        check(sample != nullptr, "an over-long recording still loads: " + error);

        if (sample != nullptr)
            check(sample->seconds() <= drums::maxSampleSeconds + 0.01,
                  "and is capped at " + juce::String(drums::maxSampleSeconds, 0) + " seconds ("
                      + juce::String(sample->seconds(), 2) + " s)");
    }

    // ---- A real file, through the real decoder -----------------------------
    //
    // Everything above builds its audio in memory, which tests the decisions
    // and not the decode. This writes an actual WAV at a rate that is NOT the
    // one anything here runs at, and checks that the rate comes back intact -
    // because the rate is what the playback ratio is built from, and a
    // loader that quietly assumed 44.1k would detune every 48k sample by a
    // semitone and a half.
    {
        constexpr double fileRate = 48000.0;
        auto file = writeTempWav(makeBurst(0.1, 0.2, 0.1, 1, 0.5f, fileRate), "burst48", fileRate);

        check(file.existsAsFile(), "the test could write a WAV to read back");

        if (file.existsAsFile())
        {
            juce::String error;
            auto sample = drumsamples::load(file, error);

            check(sample != nullptr, "and it loads from disk: " + error);

            if (sample != nullptr)
            {
                check(sample->sourceRate == 48000.0,
                      "at its own rate rather than an assumed one ("
                          + juce::String(sample->sourceRate, 0) + " Hz)");
                check(sample->name == "burst48", "and named after the file: " + sample->name);
                check(sample->seconds() > 0.19 && sample->seconds() < 0.22,
                      "and trimmed to the tone (" + juce::String(sample->seconds(), 4) + " s)");
            }

            file.getParentDirectory().deleteRecursively();
        }
    }

    // ---- An AIFC whose compression type nothing recognises -----------------
    //
    // Reported from use: an .aif would not load while a WAV loaded fine. An
    // AIFC header carries a four-character compression type, and JUCE accepts
    // five of them; anything else and it refuses the file outright. Plenty of
    // tools write an odd code over perfectly ordinary PCM.
    //
    // Both of these are built here rather than shipped as fixtures, because
    // the pair is the point: identical headers, identical sizes, and only the
    // audio inside them differs. Nothing but looking at the audio can tell
    // them apart, which is exactly what the loader has to do.
    {
        // An AIFC with a made-up compression type over honest PCM.
        const auto writeAifc = [](const juce::String& name, const juce::String& codecName,
                                  bool scrambleTheAudio)
        {
            constexpr int frames = 8000;
            constexpr int channels = 1;

            auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("WaveLatheDrumSampleTest")
                            .getChildFile(name + ".aif");

            file.getParentDirectory().createDirectory();
            file.deleteFile();

            juce::MemoryOutputStream audio;

            // A deterministic xorshift, so the "not a waveform" case is the
            // same bytes on every run.
            //
            // It has to be genuinely uncorrelated. The first attempt used a
            // counter times a constant, which is a sawtooth - about as
            // correlated as audio gets - and the loader accepted it, exactly
            // as it should have. The point of this file is data that reads as
            // a waveform under NO byte order, which is what the real one
            // does: consecutive samples slamming between the extremes.
            juce::uint32 noise = 0x12345678u;

            for (int i = 0; i < frames; ++i)
            {
                // A quiet sine, which is about as correlated sample to sample
                // as audio gets.
                const auto value = (int) (12000.0 * std::sin(juce::MathConstants<double>::twoPi
                                                             * 220.0 * i / rate));

                if (scrambleTheAudio)
                {
                    // The same number of bytes as the PCM would need, so the
                    // size test still passes and only the second test can
                    // tell the difference.
                    noise ^= noise << 13;
                    noise ^= noise >> 17;
                    noise ^= noise << 5;

                    audio.writeByte((char) ((noise >> 24) & 0xff));
                    audio.writeByte((char) ((noise >> 8) & 0xff));
                }
                else
                {
                    audio.writeByte((char) ((value >> 8) & 0xff));   // big-endian, as AIFF means
                    audio.writeByte((char) (value & 0xff));
                }
            }

            const auto audioBytes = (int) audio.getDataSize();

            // The compression name is a Pascal string, padded to an even
            // length as the format requires.
            const auto nameBytes = codecName.getNumBytesAsUTF8();
            const auto commLength = 18 + 4 + 1 + (int) nameBytes + (((int) nameBytes + 1) % 2);

            juce::MemoryOutputStream out;
            out.write("FORM", 4);
            out.writeIntBigEndian(4 + (8 + 4) + (8 + commLength) + (8 + 8 + audioBytes));
            out.write("AIFC", 4);

            out.write("FVER", 4);
            out.writeIntBigEndian(4);
            out.writeIntBigEndian((int) 0xa2805140);

            out.write("COMM", 4);
            out.writeIntBigEndian(commLength);
            out.writeShortBigEndian((short) channels);
            out.writeIntBigEndian(frames);
            out.writeShortBigEndian(16);

            // 44100 as the 80-bit extended float AIFF stores rates in: two
            // bytes of exponent and eight of mantissa, shifted up so its top
            // bit is set. TEN bytes in total - the first version of this
            // wrote twelve, which pushed every field after it out of place
            // and made the loader look broken.
            out.writeShortBigEndian((short) 0x400e);
            out.writeIntBigEndian((int) 0xac440000);
            out.writeIntBigEndian(0);

            out.write("able", 4);
            out.writeByte((char) nameBytes);
            out.write(codecName.toRawUTF8(), nameBytes);

            if (((int) nameBytes + 1) % 2 != 0)
                out.writeByte(0);

            out.write("SSND", 4);
            out.writeIntBigEndian(8 + audioBytes);
            out.writeIntBigEndian(0);
            out.writeIntBigEndian(0);
            out.write(audio.getData(), audio.getDataSize());

            file.replaceWithData(out.getData(), out.getDataSize());
            return file;
        };

        auto honest = writeAifc("oddcodec", "Some Tool", false);
        juce::String error;
        auto loaded = drumsamples::load(honest, error);

        check(loaded != nullptr,
              "an AIFC with an unrecognised compression type over real PCM loads: " + error);

        if (loaded != nullptr)
        {
            check(loaded->sourceRate == rate,
                  "at its own rate (" + juce::String(loaded->sourceRate, 0) + " Hz)");
            check(loaded->length() > 7000,
                  "with its audio intact (" + juce::String(loaded->length()) + " frames)");
        }

        // The other half, and the one that matters. This is the shape of the
        // file that was actually reported: the header is identical, the size
        // is exactly right for uncompressed audio, and the contents are not a
        // waveform. An earlier version of this loader accepted it and handed
        // back white noise, which would have shipped as a fix.
        auto scrambled = writeAifc("notpcm", "Ableton Content", true);
        juce::String scrambledError;

        check(drumsamples::load(scrambled, scrambledError) == nullptr,
              "and one whose bytes are the right SIZE but are not a waveform is refused");

        check(scrambledError.containsIgnoreCase("Ableton Content"),
              "naming the format so the message is worth reading: " + scrambledError);
        check(scrambledError.containsIgnoreCase("WAV"),
              "and saying what to do about it");

        honest.getParentDirectory().deleteRecursively();
    }

    // ---- Something that is not audio at all --------------------------------
    {
        auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                        .getChildFile("WaveLatheDrumSampleTest")
                        .getChildFile("notaudio.wav");

        file.getParentDirectory().createDirectory();
        file.replaceWithText("this is not a wav file, whatever it is called");

        juce::String error;
        check(drumsamples::load(file, error) == nullptr && error.isNotEmpty(),
              "a file that is not audio is refused with a reason: " + error);

        file.getParentDirectory().deleteRecursively();
    }

    // ---- Letting go of a sample nobody is playing any more ------------------
    //
    // The store is what makes browsing a folder affordable: previews come off
    // the voice as fast as the arrow key moves, and every one of them used to
    // be kept for the life of the plugin. What these check is that it lets go
    // at the right moment - too early frees memory a voice is still reading
    // mid-hit, too late is the leak it was written to stop.
    {
        std::cout << std::endl << "Retiring displaced samples" << std::endl;

        const auto makeSample = []
        {
            auto sample = std::make_shared<drums::Sample>();
            sample->data.assign(1000, 0.25f);
            sample->sourceRate = rate;
            return drums::SampleRef(sample);
        };

        drums::RetiringStore<drums::SampleRef> store;

        // A weak_ptr is the whole measurement: it says whether the store is
        // the last owner and whether it has let go, without the test having
        // to know anything about how it holds them.
        auto first = makeSample();
        std::weak_ptr<const drums::Sample> watch = first;

        constexpr juce::uint32 start = 1000000;

        store.retire(std::move(first), start);
        check(!watch.expired(), "a sample just taken off a voice is still held");

        // One millisecond before the grace runs out. A hit started the instant
        // it was displaced could still be sounding, so this is the case that
        // must not free anything.
        store.sweep(start + drums::retirementGraceMs - 1);
        check(!watch.expired(), "and is still held one millisecond before its grace is up");
        check(store.size() == 1, "with the store still reporting it");

        store.sweep(start + drums::retirementGraceMs);
        check(watch.expired(), "and is let go once the grace is up");
        check(store.size() == 0, "leaving the store empty");
    }

    {
        // Browsing at speed: many previews, each displacing the last. The
        // point is that the store does not grow with the size of the folder.
        drums::RetiringStore<drums::SampleRef> store;
        constexpr juce::uint32 start = 5000000;

        // Thirty files arrowed past at roughly ten a second, which is a real
        // keyboard repeat rate rather than a number chosen to pass.
        for (int i = 0; i < 30; ++i)
        {
            auto sample = std::make_shared<drums::Sample>();
            sample->data.assign(1000, 0.25f);
            store.retire(drums::SampleRef(sample), start + (juce::uint32) i * 100);
        }

        // Three seconds of browsing is well inside the grace, so all thirty
        // are still held - which is the correct answer, not a failure. Any of
        // them could still be ringing.
        check(store.size() == 30, "everything arrowed past inside the grace period is kept");

        // Once the browsing stops they go in the order they were displaced,
        // so a sweep half way through the run drops half of them. Swept at
        // the END of the run instead, every one of the thirty is exactly at
        // or past its grace and the store empties in one go - which says
        // nothing about the order, and is what the first version of this
        // check asked for and then expected one survivor of.
        store.sweep(start + 1400 + drums::retirementGraceMs);
        check(store.size() == 15, "and drains as the grace passes, oldest first");

        store.sweep(start + 2900 + drums::retirementGraceMs);
        check(store.size() == 0, "down to nothing once the last one's grace is up");
    }

    {
        // The millisecond counter wraps every 49 days of uptime, and a plugin
        // left open in a studio for seven weeks is not a strange thing.
        //
        // This pins the behaviour rather than guarding something fragile: the
        // subtraction is between two unsigned values, so it wraps correctly
        // and keeps wrapping correctly under every rearrangement tried against
        // it. It earns its place by proving the claim in the comment beside
        // the code, and by failing the day somebody stores the timestamp as
        // something signed.
        drums::RetiringStore<drums::SampleRef> store;

        const juce::uint32 beforeWrap = 0xffffffffu - 1000;

        auto sample = std::make_shared<drums::Sample>();
        sample->data.assign(1000, 0.25f);
        std::weak_ptr<const drums::Sample> watch = sample;

        store.retire(drums::SampleRef(sample), beforeWrap);
        sample.reset();

        // 2000 ms later, which is 1000 ms past the wrap and a small number.
        const juce::uint32 afterWrap = beforeWrap + 2000;
        check(afterWrap < beforeWrap, "the test really does straddle the wrap");

        store.sweep(afterWrap);
        check(!watch.expired(), "a sample retired just before the counter wraps is kept");

        store.sweep(beforeWrap + drums::retirementGraceMs);
        check(watch.expired(), "and let go on schedule across it");
    }

    std::cout << std::endl;
    if (failures == 0)
        std::cout << "ALL DRUM SAMPLE TESTS PASSED" << std::endl;
    else
        std::cout << failures << " DRUM SAMPLE TEST(S) FAILED" << std::endl;

    return failures == 0 ? 0 : 1;
}
