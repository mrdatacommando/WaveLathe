// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MidiMap.h"
#include "ParameterRegistry.h"

namespace wavelathe
{
MidiMap::MidiMap()
{
    for (auto& slot : controllers)
        slot.store(none);
}

bool MidiMap::handleController(int controller, int value, SynthParameters& params)
{
    if (controller < 0 || controller >= numControllers)
        return false;

    // Reserved by the spec for Channel Mode messages, which are instructions to
    // the instrument rather than the position of anything. Live emits All Notes
    // Off every time the transport stops, so without this, arming a learn in a
    // host and pressing stop binds the dial to CC 123 - after which it snaps to
    // zero on every stop and nothing on screen says why.
    //
    // Refused BEFORE the learn is taken, so one arriving mid-learn neither
    // claims the armed dial nor quietly disarms a learn still waiting for the
    // control you actually meant. The synth still receives them: this map never
    // touches the buffer, and juce::Synthesiser reads All Notes Off from it
    // exactly as it always did.
    if (controller >= firstChannelModeController)
        return false;

    // Taken with exchange so two controllers moved at the same moment cannot
    // both claim the same armed dial - the first one through takes it and the
    // second sees an unarmed map, which is what a person watching would expect.
    int target = learnTarget.exchange(none);
    if (target >= 0 && target < paramreg::count())
    {
        controllers[(size_t) controller].store(target);
        version.fetch_add(1);
    }

    int parameterId = controllers[(size_t) controller].load();
    if (parameterId < 0)
        return false;

    // Applied straight after binding as well, so the dial jumps to where the
    // hardware already is. Seeing it move is how you know the assignment took.
    paramreg::writeNormalised(params, parameterId, (float) juce::jlimit(0, 127, value) / 127.0f);
    return true;
}

int MidiMap::parameterFor(int controller) const
{
    if (controller < 0 || controller >= numControllers)
        return none;

    return controllers[(size_t) controller].load();
}

void MidiMap::assign(int controller, int parameterId)
{
    if (controller < 0 || controller >= numControllers)
        return;

    // Reserved, so never part of the map at all rather than in it and inert.
    // An assignment the dialog lists but which could never fire is worse than
    // no assignment.
    if (controller >= firstChannelModeController)
        return;

    if (parameterId < 0 || parameterId >= paramreg::count())
        return;

    controllers[(size_t) controller].store(parameterId);
    version.fetch_add(1);
}

void MidiMap::clearController(int controller)
{
    if (controller < 0 || controller >= numControllers)
        return;

    controllers[(size_t) controller].store(none);
    version.fetch_add(1);
}

void MidiMap::clearParameter(int parameterId)
{
    for (auto& slot : controllers)
        if (slot.load() == parameterId)
            slot.store(none);

    version.fetch_add(1);
}

void MidiMap::clearAll()
{
    for (auto& slot : controllers)
        slot.store(none);

    learnTarget.store(none);
    version.fetch_add(1);
}

int MidiMap::controllerFor(int parameterId) const
{
    if (parameterId < 0)
        return none;

    for (int cc = 0; cc < numControllers; ++cc)
        if (controllers[(size_t) cc].load() == parameterId)
            return cc;

    return none;
}

int MidiMap::countAssignments() const
{
    int count = 0;
    for (const auto& slot : controllers)
        if (slot.load() != none)
            ++count;

    return count;
}

void MidiMap::armLearn(int parameterId)
{
    if (parameterId < 0 || parameterId >= paramreg::count())
    {
        cancelLearn();
        return;
    }

    learnTarget.store(parameterId);
    version.fetch_add(1);
}

void MidiMap::cancelLearn()
{
    learnTarget.store(none);
    version.fetch_add(1);
}

juce::String MidiMap::toString() const
{
    juce::StringArray parts;

    for (int cc = 0; cc < numControllers; ++cc)
    {
        int id = controllers[(size_t) cc].load();
        if (id >= 0 && id < paramreg::count())
            parts.add(juce::String(cc) + ":" + paramreg::name(id));
    }

    return parts.joinIntoString(",");
}

void MidiMap::fromString(const juce::String& text)
{
    for (auto& slot : controllers)
        slot.store(none);

    juce::StringArray parts;
    parts.addTokens(text, ",", "");

    for (const auto& part : parts)
    {
        auto trimmed = part.trim();
        if (trimmed.isEmpty())
            continue;

        int colon = trimmed.indexOfChar(':');
        if (colon <= 0)
            continue;

        int cc = trimmed.substring(0, colon).getIntValue();
        int id = paramreg::idForName(trimmed.substring(colon + 1).trim());

        // A name the registry no longer knows is dropped rather than guessed
        // at: a slot pointing at the wrong dial is worse than an empty one.
        // Reserved controllers are dropped on the way in too, so a map saved by
        // an older build cannot carry one back and list it as live.
        if (cc >= 0 && cc < firstChannelModeController && id >= 0)
            controllers[(size_t) cc].store(id);
    }

    version.fetch_add(1);
}

void filterNotesToChannel(juce::MidiBuffer& buffer, int channel)
{
    if (channel <= 0 || channel > 16)
        return;

    juce::MidiBuffer kept;

    for (const auto metadata : buffer)
    {
        auto message = metadata.getMessage();

        // Note-offs are filtered along with note-ons, deliberately. Keeping a
        // release whose note-on was dropped would tell the voices to let go of
        // something they were never holding; the pairs have to stand or fall
        // together. All-notes-off and all-sound-off go with them because they
        // are releases for the whole channel.
        const bool isNote = message.isNoteOnOrOff() || message.isAllNotesOff()
                            || message.isAllSoundOff();

        if (isNote && message.getChannel() != channel)
            continue;

        kept.addEvent(message, metadata.samplePosition);
    }

    buffer.swapWith(kept);
}

std::vector<DrumNote> splitIncomingNotes(juce::MidiBuffer& buffer, int drumChannel,
                                         int keyboardChannel)
{
    std::vector<DrumNote> struck;

    if (drumChannel >= 1 && drumChannel <= 16)
    {
        juce::MidiBuffer kept;

        for (const auto metadata : buffer)
        {
            auto message = metadata.getMessage();

            // Everything that is not a note passes through untouched, on every
            // channel, exactly as it does for the keyboard filter. A knob is a
            // knob whichever device sent it.
            const bool isNote = message.isNoteOnOrOff() || message.isAllNotesOff()
                                || message.isAllSoundOff();

            if (!isNote || message.getChannel() != drumChannel)
            {
                kept.addEvent(message, metadata.samplePosition);
                continue;
            }

            // On the drum channel from here. Note-ons become hits; note-offs
            // and the all-notes-off pair are swallowed, because a struck drum
            // has nothing to release.
            if (message.isNoteOn())
                struck.push_back({message.getNoteNumber(), message.getFloatVelocity(),
                                  metadata.samplePosition});
        }

        buffer.swapWith(kept);
    }

    filterNotesToChannel(buffer, keyboardChannel);

    return struck;
}

// ---- Which pad hits which drum ----------------------------------------------

DrumNoteMap::DrumNoteMap()
{
    for (auto& slot : notes)
        slot.store(none);
}

int DrumNoteMap::voiceForNote(int note, int transpose) const
{
    // Overrides first. A slot that has been taught a note answers to it even
    // when GM had that note spoken for, which is what makes the learn worth
    // having: teaching the Clap your controller's C3 has to beat GM's opinion
    // that C3 is a note for the keyboard.
    for (int voice = 0; voice < project::numDrumVoices; ++voice)
        if (notes[(size_t) voice].load() == note)
            return voice;

    // Then GM, shifted. Subtracted rather than added: transpose says where the
    // kit LISTENS, so +12 means "the note I play is an octave above the one GM
    // names", and finding it means coming back down.
    const auto shifted = note - juce::jlimit(-project::maxDrumTranspose,
                                             project::maxDrumTranspose, transpose);

    if (shifted < 0 || shifted > 127)
        return none;

    const auto voice = project::drumVoiceForNote(shifted);

    // A slot that has been taught a note is OFF the GM map, so a GM note that
    // used to reach it no longer does. Without this, learning the Clap to C3
    // would leave it answering C3 and GM's 39 both, and "which pad is this
    // drum on" would have two answers for every slot anybody had touched.
    if (voice >= 0 && notes[(size_t) voice].load() != none)
        return none;

    return voice;
}

bool DrumNoteMap::captureLearn(int note)
{
    const auto target = learnTarget.load();

    if (target < 0 || target >= project::numDrumVoices)
        return false;

    if (note < 0 || note > 127)
        return false;

    // Cleared from whatever else held it, here on the audio thread, for the
    // same reason assign() does it on the message thread: the invariant is "a
    // note reaches at most one slot" and it cannot hold only on one of the two
    // paths that can break it.
    for (int voice = 0; voice < project::numDrumVoices; ++voice)
        if (voice != target && notes[(size_t) voice].load() == note)
            notes[(size_t) voice].store(none);

    notes[(size_t) target].store(note);
    learnTarget.store(none);
    version.fetch_add(1);

    return true;
}

int DrumNoteMap::noteFor(int voice) const
{
    if (voice < 0 || voice >= project::numDrumVoices)
        return none;

    return notes[(size_t) voice].load();
}

void DrumNoteMap::assign(int voice, int note)
{
    if (voice < 0 || voice >= project::numDrumVoices || note < 0 || note > 127)
        return;

    for (int other = 0; other < project::numDrumVoices; ++other)
        if (other != voice && notes[(size_t) other].load() == note)
            notes[(size_t) other].store(none);

    notes[(size_t) voice].store(note);
    version.fetch_add(1);
}

void DrumNoteMap::clearVoice(int voice)
{
    if (voice < 0 || voice >= project::numDrumVoices)
        return;

    notes[(size_t) voice].store(none);
    version.fetch_add(1);
}

void DrumNoteMap::clearAll()
{
    for (auto& slot : notes)
        slot.store(none);

    learnTarget.store(none);
    version.fetch_add(1);
}

int DrumNoteMap::countAssignments() const
{
    int count = 0;

    for (const auto& slot : notes)
        if (slot.load() != none)
            ++count;

    return count;
}

void DrumNoteMap::armLearn(int voice)
{
    if (voice < 0 || voice >= project::numDrumVoices)
        return;

    learnTarget.store(voice);
    version.fetch_add(1);
}

void DrumNoteMap::cancelLearn()
{
    learnTarget.store(none);
    version.fetch_add(1);
}

juce::String DrumNoteMap::toString() const
{
    juce::StringArray parts;

    for (int voice = 0; voice < project::numDrumVoices; ++voice)
    {
        const auto note = notes[(size_t) voice].load();

        if (note != none)
            parts.add(juce::String(project::drumVoiceName(voice)) + ":" + juce::String(note));
    }

    return parts.joinIntoString(",");
}

void DrumNoteMap::fromString(const juce::String& text)
{
    for (auto& slot : notes)
        slot.store(none);

    juce::StringArray parts;
    parts.addTokens(text, ",", "");

    for (const auto& part : parts)
    {
        auto trimmed = part.trim();
        if (trimmed.isEmpty())
            continue;

        const auto colon = trimmed.indexOfChar(':');
        if (colon <= 0)
            continue;

        const auto name = trimmed.substring(0, colon).trim();
        const auto note = trimmed.substring(colon + 1).trim().getIntValue();

        if (note < 0 || note > 127)
            continue;

        // By name, so a file written before the rows were reordered still puts
        // the Cowbell's pad on the Cowbell. A name this build does not know is
        // dropped rather than guessed at - a slot answering the wrong pad is
        // worse than one still on GM.
        for (int voice = 0; voice < project::numDrumVoices; ++voice)
        {
            if (name == project::drumVoiceName(voice))
            {
                notes[(size_t) voice].store(note);
                break;
            }
        }
    }

    // Two slots taught the same note cannot come out of assign() or
    // captureLearn(), but they can come out of a hand-edited settings file,
    // and voiceForNote would then answer with whichever sits first. Resolved
    // the same way - the later slot keeps it - so the file and the running map
    // cannot mean different things.
    for (int voice = 0; voice < project::numDrumVoices; ++voice)
    {
        const auto note = notes[(size_t) voice].load();

        if (note == none)
            continue;

        for (int later = voice + 1; later < project::numDrumVoices; ++later)
            if (notes[(size_t) later].load() == note)
                notes[(size_t) voice].store(none);
    }

    version.fetch_add(1);
}

int drumTriggerNote(const DrumNoteMap& map, int voice, int transpose)
{
    const auto taught = map.noteFor(voice);

    if (taught != DrumNoteMap::none)
        return taught;

    const auto primary = project::drumPrimaryNoteFor(voice);

    if (primary < 0)
        return -1;

    return juce::jlimit(0, 127,
                        primary + juce::jlimit(-project::maxDrumTranspose,
                                               project::maxDrumTranspose, transpose));
}
} // namespace wavelathe
