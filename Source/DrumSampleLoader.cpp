// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "DrumSampleLoader.h"

#include <cmath>

namespace wavelathe
{
namespace drumsamples
{
drums::SampleRef fromAudio(const juce::AudioBuffer<float>& audio, double sourceRate,
                           const juce::String& name, juce::String& error)
{
    const auto numChannels = audio.getNumChannels();
    const auto numSamples = audio.getNumSamples();

    if (numChannels <= 0 || numSamples <= 0)
    {
        error = "That file has no audio in it.";
        return nullptr;
    }

    if (sourceRate < 1000.0 || sourceRate > 768000.0)
    {
        error = "That file reports a sample rate of " + juce::String(sourceRate, 0)
                + " Hz, which cannot be right.";
        return nullptr;
    }

    // Summed to mono, and this is the one lossy thing the loader does.
    //
    // A kit voice has a Pan control, and Pan is what you give a mono source -
    // a stereo slot would need the whole voice, the chain and the panner to
    // become stereo for a gain nobody asked for on a drum hit. It also halves
    // the memory and the size of a saved project.
    //
    // Divided by the channel count rather than summed raw, so a file whose
    // two sides are the same does not arrive twice as loud as one that is
    // genuinely mono.
    std::vector<float> mono((size_t) numSamples, 0.0f);
    const auto scale = 1.0f / (float) numChannels;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto* source = audio.getReadPointer(channel);

        for (int i = 0; i < numSamples; ++i)
            mono[(size_t) i] += source[i] * scale;
    }

    float peak = 0.0f;
    for (const auto value : mono)
        peak = juce::jmax(peak, std::abs(value));

    if (peak <= silenceFloor)
    {
        error = "That file is silent.";
        return nullptr;
    }

    const auto threshold = juce::jmax(silenceFloor, peak * silenceBelowPeak);

    int first = 0;
    while (first < numSamples && std::abs(mono[(size_t) first]) < threshold)
        ++first;

    int last = numSamples - 1;
    while (last > first && std::abs(mono[(size_t) last]) < threshold)
        --last;

    first = juce::jmax(0, first - preRollSamples);
    last = juce::jmin(numSamples - 1, last + postRollSamples);

    auto trimmed = juce::jmax(1, last - first + 1);

    // Capped rather than refused. A loop dropped onto a drum slot is a
    // mistake, but the useful response to a mistake is the first ten seconds
    // of it rather than a dialog - and the cap is what stops one careless
    // drag turning a project file into hundreds of megabytes.
    const auto maxFrames = (int) (drums::maxSampleSeconds * sourceRate);
    trimmed = juce::jmin(trimmed, maxFrames);

    auto sample = std::make_shared<drums::Sample>();
    sample->sourceRate = sourceRate;
    sample->name = name;
    sample->data.assign(mono.begin() + first, mono.begin() + first + trimmed);

    return sample;
}

namespace
{
// ---- AIFC files whose compression type is not one JUCE knows ---------------
//
// Reported from use: an .aif from an Ableton library would not load, while a
// WAV loaded fine. An AIFC header carries a four-character compression type,
// and JUCE's AIFF reader accepts NONE, twos, sowt and the two float types -
// for anything else it sets the sample rate to zero and gives up, so
// createReaderFor returns null and the file looks unreadable.
//
// Plenty of files carry an odd code over perfectly ordinary PCM, and those
// are worth reading rather than refusing. So when JUCE gives up, read the
// header directly, and take the audio if - and only if - it really is audio.
//
// TWO TESTS, AND BOTH ARE NEEDED. That is the lesson of the file that
// prompted this, which passes the first one and is not PCM at all:
//
//  1. THE SIZE. An AIFC's COMM chunk gives the frame count, the channel count
//     and the bits per sample; its SSND chunk says how many bytes of audio
//     there are. If those bytes are exactly frames x channels x
//     bytes-per-sample then nothing has been made smaller. Necessary, and NOT
//     sufficient: a transform that rearranges bytes without removing any
//     passes it untouched.
//
//  2. THE AUDIO. Decode a stretch and ask whether it behaves like a waveform,
//     by measuring how much each sample resembles the one before it. Real
//     audio is strongly correlated sample to sample even when it is a cymbal;
//     anything else - bytes swapped, planes separated, content encoded - has
//     no correlation at all.
//
// The reported file passes the size test exactly and fails the audio test at
// both byte orders, because its samples slam between plus and minus thirty
// thousand every single sample. Its compression name says "Ableton Content",
// and working out what that arrangement actually is would mean reverse
// engineering a proprietary format for commercial library content. So it is
// refused - but with a message that says what it is and what to do, rather
// than "could not load that".
//
// Here rather than somewhere shared, with a cost worth naming:
// SampleMatcher::loadMono has the same blind spot, so Quick Match and Sample
// Wavetable will still refuse these files. Fixing it in one place would mean
// moving decoding out from under two features and their tests.
struct AiffHeader
{
    int numChannels = 0;
    int bitsPerSample = 0;
    juce::int64 numFrames = 0;
    double sampleRate = 0.0;
    juce::int64 dataStart = 0;
    juce::int64 dataBytes = 0;
    juce::String compressionName;
    bool sawComm = false;
    bool sawData = false;
};

// How much correlation counts as a waveform.
//
// Generous on purpose. A bright cymbal at 96kHz is the least correlated thing
// a drum library holds and still measures far above this; the arrangements
// this is guarding against measure within a hundredth of zero. There is no
// close call here to get wrong.
constexpr double waveformCorrelation = 0.10;

// A four-character chunk tag, built the way it will be compared.
//
// LITTLE-endian on purpose, and not a mistake being copied: the tags are read
// with readInt(), which is little-endian, so the constant has to be built the
// same way round or every comparison fails and the file parses as nothing.
// JUCE's own AIFF reader does exactly this. The first version here used
// bigEndianInt and refused the very file it was written for.
int fourCC(const char* tag)
{
    return (int) juce::ByteOrder::littleEndianInt(tag);
}

// The 80-bit extended float an AIFF stores its rate in, read the way JUCE
// reads it so that both paths agree about what 44100 looks like.
double readExtendedRate(const juce::uint8* bytes)
{
    const auto exponent = (int) juce::ByteOrder::bigEndianShort(bytes);
    auto mantissa = juce::ByteOrder::bigEndianInt(bytes + 2);

    const auto shift = 16414 - exponent;

    if (shift < 0 || shift > 31)
        return 0.0;

    mantissa >>= shift;
    return (double) mantissa;
}

bool parseAiffChunks(juce::FileInputStream& stream, AiffHeader& header)
{
    if (stream.readInt() != fourCC("FORM"))
        return false;

    stream.readIntBigEndian();   // the outer size, which nothing here needs

    const auto formType = stream.readInt();

    if (formType != fourCC("AIFC") && formType != fourCC("AIFF"))
        return false;

    while (!stream.isExhausted())
    {
        const auto tag = stream.readInt();
        const auto length = (juce::int64) (juce::uint32) stream.readIntBigEndian();

        if (length < 0)
            return false;

        const auto next = stream.getPosition() + length + (length & 1);   // chunks pad to even

        if (tag == fourCC("COMM") && length >= 18)
        {
            header.numChannels = stream.readShortBigEndian();
            header.numFrames = (juce::int64) (juce::uint32) stream.readIntBigEndian();
            header.bitsPerSample = stream.readShortBigEndian();

            juce::uint8 rateBytes[10] = {};
            stream.read(rateBytes, 10);
            header.sampleRate = readExtendedRate(rateBytes);

            // Past the rate sits the compression type and then a Pascal
            // string naming it. The name is what makes a refusal useful:
            // "Ableton Content" tells somebody what to do next in a way a
            // four-character code never will.
            if (length >= 23)
            {
                stream.readInt();   // the compression type itself, unused

                const auto nameLength = (int) (juce::uint8) stream.readByte();

                if (nameLength > 0 && nameLength < 64)
                {
                    juce::MemoryBlock text((size_t) nameLength, true);
                    stream.read(text.getData(), nameLength);
                    header.compressionName
                        = juce::String::fromUTF8((const char*) text.getData(), nameLength).trim();
                }
            }

            header.sawComm = true;
        }
        else if (tag == fourCC("SSND") && length >= 8)
        {
            const auto offset = (juce::int64) (juce::uint32) stream.readIntBigEndian();
            stream.readIntBigEndian();   // block size, unused

            header.dataStart = stream.getPosition() + offset;
            header.dataBytes = length - 8 - offset;
            header.sawData = true;
        }

        if (!stream.setPosition(next) || stream.getPosition() != next)
            break;
    }

    return header.sawComm && header.sawData;
}

drums::SampleRef readUnknownAiff(const juce::File& file, juce::String& error)
{
    juce::FileInputStream stream(file);

    if (!stream.openedOk())
        return nullptr;

    AiffHeader header;

    if (!parseAiffChunks(stream, header))
        return nullptr;

    const auto bytesPerSample = header.bitsPerSample / 8;

    if (header.numChannels < 1 || header.numChannels > 32
        || bytesPerSample < 1 || bytesPerSample > 4
        || header.bitsPerSample % 8 != 0
        || header.numFrames <= 0
        || header.sampleRate < 1000.0 || header.sampleRate > 768000.0)
        return nullptr;

    const auto describe = [&header, &file]
    {
        return header.compressionName.isNotEmpty()
                   ? file.getFileName() + " is in " + header.compressionName + " format"
                   : file.getFileName() + " is in an encoding";
    };

    // Test one: nothing has been made smaller.
    const auto expected = header.numFrames * header.numChannels * bytesPerSample;

    if (expected != header.dataBytes)
    {
        error = describe() + ", which this cannot read. Exporting it as a WAV will work.";
        return nullptr;
    }

    const auto maxFrames = (juce::int64) (drums::maxSampleSeconds * header.sampleRate);
    const auto frames = (int) juce::jmin(header.numFrames, maxFrames);

    if (frames <= 0 || !stream.setPosition(header.dataStart))
        return nullptr;

    const auto frameBytes = header.numChannels * bytesPerSample;
    std::vector<juce::uint8> raw((size_t) frames * (size_t) frameBytes);

    if (stream.read(raw.data(), (int) raw.size()) != (int) raw.size())
        return nullptr;

    // One sample, read either way round. Shifted as an unsigned value and
    // sign-extended at the end, because shifting a negative signed int left
    // is undefined - which the first version of this did.
    const auto sampleAt = [&raw, bytesPerSample](size_t at, bool bigEndian)
    {
        const auto* bytes = raw.data() + at;
        juce::uint32 value = 0;

        for (int b = 0; b < bytesPerSample; ++b)
            value = (value << 8) | bytes[bigEndian ? b : bytesPerSample - 1 - b];

        const auto signBit = juce::uint32(1) << (bytesPerSample * 8 - 1);

        if ((value & signBit) != 0)
            value |= ~((signBit << 1) - 1);

        return (juce::int32) value;
    };

    // Test two: it behaves like a waveform.
    const auto correlationOf = [&sampleAt, frames, frameBytes](bool bigEndian)
    {
        // A few thousand frames is plenty, and keeps this off the critical
        // path for a long file.
        const auto window = juce::jmin(frames, 8192);

        double lagged = 0.0;
        double energy = 0.0;
        double previous = 0.0;

        for (int frame = 0; frame < window; ++frame)
        {
            const auto value = (double) sampleAt((size_t) frame * (size_t) frameBytes, bigEndian);

            lagged += previous * value;
            energy += value * value;
            previous = value;
        }

        return energy > 0.0 ? lagged / energy : 0.0;
    };

    const auto asBig = correlationOf(true);
    const auto asLittle = correlationOf(false);

    if (juce::jmax(asBig, asLittle) < waveformCorrelation)
    {
        error = describe() + ", and the audio inside it is not plain PCM."
                             " Exporting it as a WAV will work.";
        return nullptr;
    }

    const auto bigEndian = asBig >= asLittle;
    const auto scale = 1.0f / (float) (1 << (header.bitsPerSample - 1));

    juce::AudioBuffer<float> decoded(header.numChannels, frames);

    for (int frame = 0; frame < frames; ++frame)
        for (int channel = 0; channel < header.numChannels; ++channel)
            decoded.setSample(channel, frame,
                              (float) sampleAt((size_t) frame * (size_t) frameBytes
                                                   + (size_t) (channel * bytesPerSample),
                                               bigEndian)
                                  * scale);

    return fromAudio(decoded, header.sampleRate, file.getFileNameWithoutExtension(), error);
}
} // namespace

drums::SampleRef load(const juce::File& file, juce::String& error)
{
    if (!file.existsAsFile())
    {
        error = "There is no file at " + file.getFullPathName() + ".";
        return nullptr;
    }

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));

    if (reader == nullptr)
    {
        // Before giving up: an AIFC whose compression type JUCE does not know
        // may still be plain PCM. See the note above readUnknownAiff - it
        // accepts only what it can show is audio, and when it cannot, it
        // leaves behind a message that says what the file actually is.
        if (auto rescued = readUnknownAiff(file, error))
            return rescued;

        if (error.isEmpty())
            error = file.getFileName() + " is not an audio format this can read.";

        return nullptr;
    }

    // Read at most what the cap allows, rather than reading a ten-minute file
    // into memory and then throwing most of it away. The trim below may take
    // more off the front, which is why this is generous rather than exact.
    const auto available = (juce::int64) reader->lengthInSamples;
    const auto wanted = (juce::int64) (drums::maxSampleSeconds * reader->sampleRate)
                        + preRollSamples + postRollSamples;
    const auto toRead = (int) juce::jmin(available, wanted);

    if (toRead <= 0)
    {
        error = file.getFileName() + " has no audio in it.";
        return nullptr;
    }

    juce::AudioBuffer<float> decoded((int) juce::jmax(1u, reader->numChannels), toRead);
    decoded.clear();

    if (!reader->read(&decoded, 0, toRead, 0, true, true))
    {
        error = "Could not read the audio out of " + file.getFileName() + ".";
        return nullptr;
    }

    return fromAudio(decoded, reader->sampleRate, file.getFileNameWithoutExtension(), error);
}
} // namespace drumsamples
} // namespace wavelathe
