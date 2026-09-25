// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "SampleBrowser.h"
#include "UiComponents.h"

namespace wavelathe
{
namespace
{
// Long enough that holding an arrow key down scrolls without decoding every
// file it passes, short enough that landing on one and hearing it feels like
// the same action. Measured against a keyboard's repeat rate, which starts
// around thirty a second and is the thing being outrun.
constexpr int previewDelayMs = 140;
} // namespace

SampleBrowser::SampleBrowser(const juce::String& voiceName, const juce::File& startIn,
                             Preview onPreview, Restore onRestore, Commit onCommit)
    : previewFile(std::move(onPreview)), restoreSlot(std::move(onRestore)),
      commitSlot(std::move(onCommit))
{
    browser = std::make_unique<juce::FileBrowserComponent>(
        juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
            | juce::FileBrowserComponent::filenameBoxIsReadOnly,
        startIn, &filter, nullptr);

    browser->addListener(this);
    addAndMakeVisible(*browser);

    autoPlayButton.setToggleState(true, juce::dontSendNotification);
    autoPlayButton.setColour(juce::ToggleButton::textColourId, ui::colours::textDim);
    autoPlayButton.setColour(juce::ToggleButton::tickColourId, ui::colours::accent);
    autoPlayButton.setTooltip("Hear each sample as you move onto it. Off if you would"
                              " rather walk the folder in silence and press Play.");
    addAndMakeVisible(autoPlayButton);

    // There when auto-play is off, and worth having when it is on: hearing the
    // same hit twice is most of how you decide between two that are close.
    playButton.onClick = [this] { previewSelected(); };
    addAndMakeVisible(playButton);

    loadButton.onClick = [this] { close(true); };
    addAndMakeVisible(loadButton);

    closeButton.onClick = [this] { close(false); };
    addAndMakeVisible(closeButton);

    statusLabel.setColour(juce::Label::textColourId, ui::colours::textDim);
    statusLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    statusLabel.setText(voiceName + " - arrows audition, Load keeps it, Cancel puts back"
                                    " what was on the pad",
                        juce::dontSendNotification);
    addAndMakeVisible(statusLabel);

    addKeyListener(this);

    setSize(640, 520);

    // Focus the browser rather than the dialog, so the arrow keys walk the
    // file list the moment it opens.
    juce::MessageManager::callAsync(
        [safeThis = juce::Component::SafePointer<SampleBrowser>(this)]
        {
            if (safeThis != nullptr)
                safeThis->browser->grabKeyboardFocus();
        });
}

SampleBrowser::~SampleBrowser()
{
    browser->removeListener(this);

    // Closing from the title bar never reaches close(), and is still a cancel,
    // so the slot goes back.
    if (!settled && restoreSlot)
        restoreSlot();
}

juce::File SampleBrowser::selectedFile() const
{
    auto file = browser->getSelectedFile(0);

    // getSelectedFile falls back to the filename box, which can name a folder
    // while one is highlighted. Only a real file is worth previewing.
    return file.existsAsFile() ? file : juce::File{};
}

void SampleBrowser::selectionChanged()
{
    const auto file = selectedFile();

    loadButton.setEnabled(file != juce::File{});

    if (file == juce::File{} || !autoPlayButton.getToggleState())
        return;

    // Restarted on every move, so a run of keypresses previews once at the end
    // rather than once each.
    startTimer(previewDelayMs);
}

void SampleBrowser::timerCallback()
{
    stopTimer();
    previewSelected();
}

void SampleBrowser::fileClicked(const juce::File&, const juce::MouseEvent&)
{
}

void SampleBrowser::fileDoubleClicked(const juce::File& file)
{
    if (file.existsAsFile())
        close(true);
}

void SampleBrowser::browserRootChanged(const juce::File&)
{
    // Nothing is selected in a folder you have just walked into, so there is
    // nothing to hear until the first arrow key.
    loadButton.setEnabled(false);
}

void SampleBrowser::previewSelected()
{
    stopTimer();

    const auto file = selectedFile();

    if (file == juce::File{} || !previewFile)
        return;

    juce::String error;

    if (!previewFile(file, error))
    {
        // The slot is left holding whatever it held: a file that will not load
        // should not silence the pad on the way past.
        previewed = juce::File{};
        loadButton.setEnabled(false);
        statusLabel.setText(file.getFileName() + " - " + error, juce::dontSendNotification);
        return;
    }

    previewed = file;
    loadButton.setEnabled(true);
    statusLabel.setText(file.getFileName() + " - Load keeps it, Cancel puts back what was"
                                             " on the pad",
                        juce::dontSendNotification);
}

void SampleBrowser::close(bool keepThePreview)
{
    stopTimer();

    // Set before doing anything, not after, so that whatever happens below the
    // destructor knows this has been dealt with.
    settled = true;

    if (keepThePreview)
    {
        // Load on a file the debounce has not reached yet means the highlight
        // moved and the button was pressed before it could be heard. Preview
        // it now so that what gets committed is what was asked for.
        if (previewed != selectedFile())
            previewSelected();

        if (previewed != juce::File{})
        {
            if (commitSlot)
                commitSlot(previewed);
        }
        else if (restoreSlot)
        {
            // Nothing loadable was ever heard, so there is nothing to keep.
            restoreSlot();
        }
    }
    else if (restoreSlot)
    {
        restoreSlot();
    }

    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}

bool SampleBrowser::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    // Return keeps what you are hearing, Escape puts back what you had - the
    // two answers to "is this better than what is on the pad?".
    if (key == juce::KeyPress::returnKey)
    {
        close(true);
        return true;
    }

    if (key == juce::KeyPress::escapeKey)
    {
        close(false);
        return true;
    }

    // Space re-strikes without moving, for comparing one against the last.
    if (key == juce::KeyPress::spaceKey)
    {
        previewSelected();
        return true;
    }

    return false;
}

void SampleBrowser::paint(juce::Graphics& g)
{
    g.fillAll(ui::colours::background);
}

void SampleBrowser::resized()
{
    auto area = getLocalBounds().reduced(12);

    auto bottom = area.removeFromBottom(32);
    closeButton.setBounds(bottom.removeFromRight(90));
    bottom.removeFromRight(8);
    loadButton.setBounds(bottom.removeFromRight(90));
    bottom.removeFromRight(8);
    playButton.setBounds(bottom.removeFromRight(70));
    bottom.removeFromRight(12);
    autoPlayButton.setBounds(bottom.removeFromRight(130));

    area.removeFromBottom(8);
    statusLabel.setBounds(area.removeFromBottom(20));
    area.removeFromBottom(4);

    browser->setBounds(area);
}

void SampleBrowser::show(const juce::String& voiceName, const juce::File& startIn,
                         Preview onPreview, Restore onRestore, Commit onCommit)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Load a sample onto " + voiceName;
    options.dialogBackgroundColour = ui::colours::background;
    options.content.setOwned(new SampleBrowser(voiceName, startIn, std::move(onPreview),
                                               std::move(onRestore), std::move(onCommit)));
    options.content->setSize(640, 520);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    options.launchAsync();
}
} // namespace wavelathe
