// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace wavelathe
{
// Remembers, between sessions, where each file action last pointed. Each
// action keeps its own folder: presets, reference audio for matching, and
// wavetable sources usually live in different places, so a single shared
// "last folder" would send you back to the wrong one every other time.
class AppSettings
{
public:
    enum class Folder
    {
        presets = 0,
        quickMatch,
        deepMatch,
        sampleWavetable,
        exportAb,
        projects,

        // Where drum samples are loaded from. Its own entry rather than
        // sharing sampleWavetable's: one is a folder of single drum hits and
        // the other is a folder of sustained notes to build a table from, and
        // nobody keeps both in the same place.
        drumSample
    };

    static AppSettings& getInstance();

    // The folder to open for this action, falling back to a sensible default.
    juce::File getFolder(Folder which) const;
    void setFolder(Folder which, const juce::File& folder);
    void resetFolder(Folder which);

    // The MIDI learn map, as "74:Cutoff,71:Reso". Kept here rather than in a
    // preset because it describes the hardware on the desk, not the sound: a
    // patch that rearranged your controller would be a patch you could not
    // trust to load. A saved project carries its own copy and overrides this.
    // Which MIDI channel plays notes: 0 for omni, 1-16 for one channel. A desk
    // setting like the map above, so it is remembered with the application.
    int getKeyboardChannel() const;
    void setKeyboardChannel(int channel);

    // Which MIDI channel strikes the drum kit: 0 for none, 1-16 for one. The
    // same kind of setting as the keyboard channel above and remembered the
    // same way - it describes which device on the desk is the pads.
    int getDrumChannel() const;
    void setDrumChannel(int channel);

    juce::String getMidiMap() const;
    void setMidiMap(const juce::String& text);

    // How far the kit's General MIDI note map is shifted, in semitones. A desk
    // setting for the same reason the drum channel is: it describes where the
    // pads in front of you send, not anything about a song. Saving it with a
    // project would mean opening somebody else's kit moved your pads.
    int getDrumTranspose() const;
    void setDrumTranspose(int semitones);

    // Which pad hits which drum, for the slots that have been taught. Same
    // shape and same reasoning as the CC map above - one line of text you can
    // read in the settings file and fix by hand when a controller is replaced.
    juce::String getDrumNoteMap() const;
    void setDrumNoteMap(const juce::String& text);

    static juce::String getFolderLabel(Folder which);
    static juce::File getDefaultFolder(Folder which);
    static const std::vector<Folder>& allFolders();

private:
    AppSettings();

    juce::PropertiesFile* getProperties() const;
    static juce::String getKey(Folder which);

    mutable juce::ApplicationProperties properties;
};
} // namespace wavelathe
