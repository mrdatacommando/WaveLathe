// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "FactoryPresets.h"
#include "AppSettings.h"
#include <functional>
#include <memory>
#include <vector>

namespace wavelathe
{
// Browsing presets by name and category, rather than hunting through a file
// dialog. Lists the built-in bank and whatever the user has saved into their
// presets folder, side by side.
class PresetBrowser : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::KeyListener
{
public:
    // Moving through the list only ever previews. Load commits what is being
    // previewed; closing any other way puts the patch back the way it was, so
    // browsing can never cost you the sound you had.
    using ChooseFactory = std::function<void(const FactoryPresets::Entry&)>;
    using ChooseFile = std::function<void(const juce::File&)>;
    using Restore = std::function<void()>;
    using Commit = std::function<void()>;

    PresetBrowser(ChooseFactory onFactory, ChooseFile onFile, Restore onRestore, Commit onCommit);
    ~PresetBrowser() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // The window it opened, which owns itself and goes when it is closed. Watch
    // it with a SafePointer rather than holding on to the raw pointer.
    static juce::DialogWindow* show(ChooseFactory onFactory, ChooseFile onFile, Restore onRestore,
                                    Commit onCommit);

    // Whether the computer keyboard's note keys keep playing with the browser
    // up. Every other dialog silences them, but choosing a sound means playing
    // it, and having to close the window to do that defeats the audition.
    //
    // Only while the browser is the topmost modal thing, so its category menu
    // or an alert over it still takes the keys, and never while the search box
    // has focus, because those letters are being typed.
    //
    // Takes what it looks at rather than asking JUCE, so it can be tested
    // without opening a window.
    static bool leavesNoteKeysFree(const juce::Component* browserWindow,
                                   const juce::Component* topModal,
                                   const juce::Component* focused);

private:
    struct Row
    {
        juce::String name;
        juce::String detail;
        const FactoryPresets::Entry* factory = nullptr;
        juce::File file;
    };

    int getNumRows() override;
    void paintListBoxItem(int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemDoubleClicked(int row, const juce::MouseEvent&) override;
    void selectedRowsChanged(int lastRowSelected) override;

    bool keyPressed(const juce::KeyPress& key, juce::Component* origin) override;

    void refresh();
    void loadSelected();

    // Moves the highlight, which auditions whatever it lands on.
    void moveSelection(int delta);

    void close(bool keepThePreview);

    ChooseFactory chooseFactory;
    ChooseFile chooseFile;
    Restore restorePatch;
    Commit commitPatch;

    // Set once Load or Cancel has dealt with the patch. Either one closes the
    // window, which destroys the browser, and the destructor - there for the
    // title bar's close box - must not take that for a second cancel.
    bool settled = false;

    juce::TextEditor searchBox;
    juce::ComboBox categoryBox;
    juce::ListBox list;
    juce::TextButton loadButton{"Load"};
    juce::TextButton closeButton{"Close"};
    juce::Label countLabel;

    std::vector<Row> rows;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetBrowser)
};
} // namespace wavelathe
