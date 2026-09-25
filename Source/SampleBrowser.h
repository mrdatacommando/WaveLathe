// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <functional>
#include <memory>

namespace wavelathe
{
// Picking a sample by ear rather than by filename.
//
// A sample library is a folder of names that all describe the same drum -
// Snare-SessionDry-Stick-Side-Hard next to six more of its kind - and the
// only way to tell them apart is to hear them. A native file dialog cannot
// say what has been highlighted, so choosing one meant loading it, listening,
// and opening the dialog again for the next.
//
// So this is the PresetBrowser's bargain applied to files: moving through the
// list only ever PREVIEWS. The sample really is loaded onto the voice, so what
// you hear is that pad - its Level, Tune, Attack, Decay, Pan and both its
// sends - and if the pattern is running you hear it in the groove. Load keeps
// what is playing; closing any other way puts back whatever the slot held, so
// browsing can never cost you the kit you had.
//
// Windows note, since it looks like a step backwards: JUCE will host a preview
// component inside the native dialog, but only by falling back to the
// pre-Vista GetOpenFileName one - no places sidebar, no search, no recent
// folders. That is the wrong trade for someone working through a library, so
// this uses JUCE's own browser and keeps the modern navigation.
class SampleBrowser : public juce::Component,
                      private juce::FileBrowserListener,
                      private juce::KeyListener,
                      private juce::Timer
{
public:
    // Preview returns whether the file could be loaded; a false answer is
    // shown in the browser rather than swallowed, because "that one is not
    // readable" is the single most useful thing it can say about a file.
    using Preview = std::function<bool(const juce::File&, juce::String& error)>;
    using Restore = std::function<void()>;
    using Commit = std::function<void(const juce::File&)>;

    SampleBrowser(const juce::String& voiceName, const juce::File& startIn,
                  Preview onPreview, Restore onRestore, Commit onCommit);
    ~SampleBrowser() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    static void show(const juce::String& voiceName, const juce::File& startIn,
                     Preview onPreview, Restore onRestore, Commit onCommit);

private:
    void selectionChanged() override;
    void fileClicked(const juce::File&, const juce::MouseEvent&) override;
    void fileDoubleClicked(const juce::File&) override;
    void browserRootChanged(const juce::File&) override;

    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;

    // The debounce. Holding an arrow key down walks the list far faster than
    // a file can be decoded and heard, so the preview waits for the moment
    // you stop moving - the same rule the synth's own auditioning follows.
    void timerCallback() override;

    void previewSelected();
    void close(bool keepThePreview);

    juce::File selectedFile() const;

    Preview previewFile;
    Restore restoreSlot;
    Commit commitSlot;

    // Set once close() has done whatever it was going to do, either way.
    //
    // The destructor is the catch for a window shut from its title bar, which
    // never reaches close() - so it needs to know whether it is the one that
    // has to act. Without it a cancel restores twice, and restoring republishes
    // the sample bank, so the second one is real work to reach the state it is
    // already in.
    bool settled = false;

    // What the last preview actually loaded, so Load commits the file that is
    // being heard rather than whatever the list happens to be highlighting -
    // they differ for the moment between a keypress and the debounce firing.
    juce::File previewed;

    juce::WildcardFileFilter filter{"*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3", "*",
                                    "Audio files"};
    std::unique_ptr<juce::FileBrowserComponent> browser;

    juce::ToggleButton autoPlayButton{"Play on select"};
    juce::TextButton playButton{"Play"};
    juce::TextButton loadButton{"Load"};
    juce::TextButton closeButton{"Cancel"};
    juce::Label statusLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleBrowser)
};
} // namespace wavelathe
