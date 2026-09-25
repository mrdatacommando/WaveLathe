// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "PresetBrowser.h"
#include "UiComponents.h"

namespace wavelathe
{
namespace
{
constexpr int allCategories = -1;
} // namespace

PresetBrowser::PresetBrowser(ChooseFactory onFactory, ChooseFile onFile, Restore onRestore, Commit onCommit)
    : chooseFactory(std::move(onFactory)), chooseFile(std::move(onFile)),
      restorePatch(std::move(onRestore)), commitPatch(std::move(onCommit))
{
    searchBox.setTextToShowWhenEmpty("Search by name or category...", ui::colours::textDim);
    searchBox.onTextChange = [this] { refresh(); };
    addAndMakeVisible(searchBox);

    categoryBox.addItem("All categories", 1);
    for (int i = 0; i < (int) FactoryPresets::Category::numCategories; ++i)
        categoryBox.addItem(FactoryPresets::categoryName(i), i + 2);
    categoryBox.addItem("My Presets", 100);
    categoryBox.setSelectedId(1, juce::dontSendNotification);
    categoryBox.onChange = [this] { refresh(); };
    addAndMakeVisible(categoryBox);

    list.setModel(this);
    list.setRowHeight(26);
    list.setColour(juce::ListBox::backgroundColourId, ui::colours::background);
    addAndMakeVisible(list);

    loadButton.setButtonText("Load");
    loadButton.onClick = [this] { close(true); };
    addAndMakeVisible(loadButton);

    closeButton.setButtonText("Cancel");
    closeButton.onClick = [this] { close(false); };
    addAndMakeVisible(closeButton);

    countLabel.setColour(juce::Label::textColourId, ui::colours::textDim);
    countLabel.setFont(juce::Font(juce::FontOptions(12.0f)));
    addAndMakeVisible(countLabel);

    // Arrows anywhere in the dialog walk the list; the search box needs its own
    // listener because a text editor would otherwise swallow them.
    addKeyListener(this);
    searchBox.addKeyListener(this);

    refresh();
    setSize(560, 480);

    // Focus the list rather than the search box, so the arrow keys work the
    // moment the browser opens.
    juce::MessageManager::callAsync(
        [safeThis = juce::Component::SafePointer<PresetBrowser>(this)]
        {
            if (safeThis != nullptr)
                safeThis->list.grabKeyboardFocus();
        });
}

PresetBrowser::~PresetBrowser()
{
    // Closing from the title bar is still a cancel, so the patch goes back -
    // unless Load or Cancel has already settled it on the way out.
    if (!settled && restorePatch)
        restorePatch();
}

void PresetBrowser::refresh()
{
    rows.clear();

    auto term = searchBox.getText();
    int selected = categoryBox.getSelectedId();
    bool userOnly = (selected == 100);
    int categoryFilter = (selected >= 2 && selected < 100) ? selected - 2 : allCategories;

    if (!userOnly)
    {
        for (const auto* entry : FactoryPresets::search(term, categoryFilter))
            rows.push_back({entry->name, FactoryPresets::categoryName((int) entry->category), entry, {}});
    }

    // Anything the user has saved, listed alongside the built-in bank. Only
    // shown when the filter is not narrowed to one factory category.
    if (userOnly || categoryFilter == allCategories)
    {
        auto folder = AppSettings::getInstance().getFolder(AppSettings::Folder::presets);
        if (folder.isDirectory())
        {
            auto files = folder.findChildFiles(juce::File::findFiles, false, "*.fxp");
            files.sort();

            auto needle = term.trim().toLowerCase();
            for (const auto& file : files)
            {
                auto name = file.getFileNameWithoutExtension();
                if (needle.isNotEmpty() && !name.toLowerCase().contains(needle))
                    continue;

                rows.push_back({name, "My Presets", nullptr, file});
            }
        }
    }

    list.updateContent();
    list.deselectAllRows();

    countLabel.setText(juce::String(rows.size()) + " preset" + (rows.size() == 1 ? "" : "s")
                           + "  -  arrows audition, Load keeps it, Cancel puts your patch back",
                       juce::dontSendNotification);
    list.repaint();
}

int PresetBrowser::getNumRows()
{
    return (int) rows.size();
}

void PresetBrowser::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) rows.size())
        return;

    if (selected)
    {
        g.setColour(ui::colours::accent.withAlpha(0.18f));
        g.fillRect(0, 0, width, height);
    }

    const auto& entry = rows[(size_t) row];

    g.setColour(ui::colours::textPrimary);
    g.setFont(juce::Font(juce::FontOptions(14.0f)));
    g.drawText(entry.name, 10, 0, width - 130, height, juce::Justification::centredLeft);

    // User presets are tinted differently so the two sources are never confused.
    g.setColour(entry.factory != nullptr ? ui::colours::textDim : ui::colours::accentWarm);
    g.setFont(juce::Font(juce::FontOptions(11.0f)));
    g.drawText(entry.detail, width - 120, 0, 110, height, juce::Justification::centredRight);
}

void PresetBrowser::listBoxItemDoubleClicked(int, const juce::MouseEvent&)
{
    loadSelected();
}

void PresetBrowser::selectedRowsChanged(int)
{
    // Selecting a preset loads it straight away, so arrowing down the list
    // auditions each one in turn rather than needing a click per patch.
    loadButton.setEnabled(list.getSelectedRow() >= 0);
    loadSelected();
}

void PresetBrowser::moveSelection(int delta)
{
    if (rows.empty())
        return;

    int current = list.getSelectedRow();
    int next = current < 0 ? (delta > 0 ? 0 : (int) rows.size() - 1)
                           : juce::jlimit(0, (int) rows.size() - 1, current + delta);

    list.selectRow(next);
    list.scrollToEnsureRowIsOnscreen(next);
}

bool PresetBrowser::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    // The list handles the arrows itself once it has focus; this catches the
    // case where you are still typing in the search box, so you can filter and
    // then walk the results without reaching for the mouse.
    if (key == juce::KeyPress::upKey)
    {
        moveSelection(-1);
        return true;
    }

    if (key == juce::KeyPress::downKey)
    {
        moveSelection(1);
        return true;
    }

    // Return keeps what you are hearing, Escape puts back what you had - the
    // two answers to "is this better than what I already have?".
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

    return false;
}

void PresetBrowser::close(bool keepThePreview)
{
    // Once. A second press before the window has gone would otherwise put the
    // patch back, or commit it, twice over.
    if (settled)
        return;

    settled = true;

    if (keepThePreview)
    {
        if (commitPatch)
            commitPatch();
    }
    else if (restorePatch)
    {
        restorePatch();
    }

    if (auto* window = findParentComponentOfClass<juce::DialogWindow>())
        window->exitModalState(0);
}

void PresetBrowser::loadSelected()
{
    int row = list.getSelectedRow();
    if (row < 0 || row >= (int) rows.size())
        return;

    const auto& entry = rows[(size_t) row];

    if (entry.factory != nullptr)
    {
        if (chooseFactory)
            chooseFactory(*entry.factory);
    }
    else if (chooseFile)
    {
        chooseFile(entry.file);
    }

    // Left open deliberately, so several patches can be auditioned in a row.
}

void PresetBrowser::paint(juce::Graphics& g)
{
    g.fillAll(ui::colours::background);
}

void PresetBrowser::resized()
{
    auto area = getLocalBounds().reduced(12);

    auto top = area.removeFromTop(28);
    categoryBox.setBounds(top.removeFromRight(170));
    top.removeFromRight(8);
    searchBox.setBounds(top);

    area.removeFromTop(10);

    auto bottom = area.removeFromBottom(32);
    closeButton.setBounds(bottom.removeFromRight(90));
    bottom.removeFromRight(8);
    loadButton.setBounds(bottom.removeFromRight(90));
    countLabel.setBounds(bottom);

    area.removeFromBottom(10);
    list.setBounds(area);
}

void PresetBrowser::show(ChooseFactory onFactory, ChooseFile onFile, Restore onRestore, Commit onCommit)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "Synth Presets";
    options.dialogBackgroundColour = ui::colours::background;
    options.content.setOwned(new PresetBrowser(std::move(onFactory), std::move(onFile),
                                                std::move(onRestore), std::move(onCommit)));
    options.content->setSize(560, 480);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    options.launchAsync();
}
} // namespace wavelathe
