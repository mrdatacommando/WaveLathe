// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "AppSettings.h"
#include "ProjectState.h"

namespace wavelathe
{
AppSettings& AppSettings::getInstance()
{
    static AppSettings instance;
    return instance;
}

AppSettings::AppSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "WaveLathe";
    options.filenameSuffix = "settings";
    options.folderName = "WaveLathe";
    options.osxLibrarySubFolder = "Application Support";
    properties.setStorageParameters(options);
}

juce::PropertiesFile* AppSettings::getProperties() const
{
    return properties.getUserSettings();
}

juce::String AppSettings::getKey(Folder which)
{
    switch (which)
    {
        case Folder::presets: return "dir.presets";
        case Folder::quickMatch: return "dir.quickMatch";
        case Folder::deepMatch: return "dir.deepMatch";
        case Folder::sampleWavetable: return "dir.sampleWavetable";
        case Folder::drumSample: return "dir.drumSample";
        case Folder::exportAb: return "dir.exportAb";
        case Folder::projects: return "dir.projects";
    }
    return "dir.other";
}

juce::String AppSettings::getFolderLabel(Folder which)
{
    switch (which)
    {
        case Folder::presets: return "Save / Load synth preset";
        case Folder::quickMatch: return "Quick Match audio";
        case Folder::deepMatch: return "Deep Match audio";
        case Folder::sampleWavetable: return "Sample Wavetable audio";
        case Folder::drumSample: return "Drum samples";
        case Folder::exportAb: return "Export A/B output";
        case Folder::projects: return "Save / Load project";
    }
    return "Other";
}

juce::File AppSettings::getDefaultFolder(Folder which)
{
    auto documents = juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

    switch (which)
    {
        case Folder::presets:
        case Folder::exportAb:
            return documents.getChildFile("WaveLathe Presets");

        case Folder::projects:
            return documents.getChildFile("WaveLathe Projects");

        case Folder::quickMatch:
        case Folder::deepMatch:
        case Folder::sampleWavetable:
        case Folder::drumSample:
            return juce::File::getSpecialLocation(juce::File::userMusicDirectory);
    }
    return documents;
}

const std::vector<AppSettings::Folder>& AppSettings::allFolders()
{
    static const std::vector<Folder> folders{Folder::presets, Folder::projects, Folder::quickMatch,
                                              Folder::deepMatch, Folder::sampleWavetable,
                                              Folder::drumSample, Folder::exportAb};
    return folders;
}

juce::File AppSettings::getFolder(Folder which) const
{
    if (auto* props = getProperties())
    {
        auto stored = props->getValue(getKey(which));
        if (stored.isNotEmpty())
        {
            juce::File folder(stored);
            if (folder.isDirectory())
                return folder;
        }
    }

    auto fallback = getDefaultFolder(which);
    return fallback.isDirectory() ? fallback
                                  : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
}

void AppSettings::setFolder(Folder which, const juce::File& folder)
{
    if (!folder.isDirectory())
        return;

    if (auto* props = getProperties())
    {
        props->setValue(getKey(which), folder.getFullPathName());
        props->saveIfNeeded();
    }
}



int AppSettings::getKeyboardChannel() const
{
    if (auto* props = getProperties())
        return juce::jlimit(0, 16, props->getIntValue("keyboardChannel", 0));

    return 0;
}

void AppSettings::setKeyboardChannel(int channel)
{
    if (auto* props = getProperties())
    {
        props->setValue("keyboardChannel", juce::jlimit(0, 16, channel));
        props->saveIfNeeded();
    }
}

int AppSettings::getDrumChannel() const
{
    if (auto* props = getProperties())
        return juce::jlimit(0, 16, props->getIntValue("drumChannel", 0));

    return 0;
}

void AppSettings::setDrumChannel(int channel)
{
    if (auto* props = getProperties())
    {
        props->setValue("drumChannel", juce::jlimit(0, 16, channel));
        props->saveIfNeeded();
    }
}

// One line of text rather than a structured block: the map is a handful of
// pairs, and a format you can read in the settings file is a format you can
// fix by hand when a controller is replaced.
juce::String AppSettings::getMidiMap() const
{
    if (auto* props = getProperties())
        return props->getValue("midiMap", {});

    return {};
}

void AppSettings::setMidiMap(const juce::String& text)
{
    if (auto* props = getProperties())
    {
        props->setValue("midiMap", text);
        props->saveIfNeeded();
    }
}

int AppSettings::getDrumTranspose() const
{
    if (auto* props = getProperties())
        return juce::jlimit(-project::maxDrumTranspose, project::maxDrumTranspose,
                            props->getIntValue("drumTranspose", 0));

    return 0;
}

void AppSettings::setDrumTranspose(int semitones)
{
    if (auto* props = getProperties())
    {
        props->setValue("drumTranspose",
                        juce::jlimit(-project::maxDrumTranspose, project::maxDrumTranspose,
                                     semitones));
        props->saveIfNeeded();
    }
}

juce::String AppSettings::getDrumNoteMap() const
{
    if (auto* props = getProperties())
        return props->getValue("drumNoteMap", {});

    return {};
}

void AppSettings::setDrumNoteMap(const juce::String& text)
{
    if (auto* props = getProperties())
    {
        props->setValue("drumNoteMap", text);
        props->saveIfNeeded();
    }
}

void AppSettings::resetFolder(Folder which)
{
    if (auto* props = getProperties())
    {
        props->removeValue(getKey(which));
        props->saveIfNeeded();
    }
}
} // namespace wavelathe
