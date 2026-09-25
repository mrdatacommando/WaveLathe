// WaveLathe - Copyright (C) 2026 Mark Van de Velde
// SPDX-License-Identifier: AGPL-3.0-or-later

#include "MidiMapView.h"
#include "ParameterRegistry.h"
#include "UiComponents.h"

namespace wavelathe
{
MidiMapView::MidiMapView(MidiMap& mapToShow, std::function<void()> onChanged)
    : map(mapToShow), changed(std::move(onChanged))
{
    addAndMakeVisible(list);
    list.setModel(this);
    list.setRowHeight(26);
    list.setColour(juce::ListBox::backgroundColourId, ui::colours::panel);
    list.setColour(juce::ListBox::outlineColourId, ui::colours::panelEdge);
    list.setOutlineThickness(1);

    addAndMakeVisible(countLabel);
    countLabel.setColour(juce::Label::textColourId, ui::colours::textDim);
    countLabel.setJustificationType(juce::Justification::centredLeft);

    auto styleButton = [this](juce::TextButton& button)
    {
        addAndMakeVisible(button);
        button.setColour(juce::TextButton::buttonColourId, ui::colours::panel);
        button.setColour(juce::TextButton::textColourOffId, ui::colours::textPrimary);
    };

    styleButton(clearButton);
    styleButton(clearAllButton);
    styleButton(closeButton);

    clearButton.onClick = [this] { clearSelected(); };
    clearAllButton.onClick = [this] { clearEverything(); };
    closeButton.onClick = [this]
    {
        if (auto* dialog = findParentComponentOfClass<juce::DialogWindow>())
            dialog->exitModalState(0);
    };

    refresh();
}

MidiMapView::~MidiMapView()
{
    list.setModel(nullptr);
}

void MidiMapView::refresh()
{
    rows.clear();

    for (int cc = 0; cc < MidiMap::numControllers; ++cc)
    {
        int id = map.parameterFor(cc);
        if (id >= 0)
            rows.push_back({cc, id});
    }

    list.updateContent();
    list.repaint();

    countLabel.setText(rows.empty() ? "Nothing assigned yet."
                                    : juce::String((int) rows.size()) + " control"
                                          + (rows.size() == 1 ? "" : "s") + " assigned",
                       juce::dontSendNotification);

    clearButton.setEnabled(!rows.empty());
    clearAllButton.setEnabled(!rows.empty());
}

int MidiMapView::getNumRows()
{
    return (int) rows.size();
}

void MidiMapView::paintListBoxItem(int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= (int) rows.size())
        return;

    const auto& entry = rows[(size_t) row];

    if (selected)
        g.fillAll(ui::colours::accent.withAlpha(0.18f));

    auto area = juce::Rectangle<int>(0, 0, width, height).reduced(10, 0);

    // The controller number in its own column and in the accent colour, because
    // it is what you are looking THROUGH the list for - the name beside it is
    // the answer, not the thing being searched.
    g.setColour(ui::colours::accent);
    g.setFont(juce::Font(juce::FontOptions(13.0f)));
    g.drawText("CC " + juce::String(entry.controller), area.removeFromLeft(64),
               juce::Justification::centredLeft, false);

    g.setColour(ui::colours::textPrimary);
    g.drawText(paramreg::name(entry.parameterId), area.removeFromLeft(150),
               juce::Justification::centredLeft, true);

    g.setColour(ui::colours::textDim);
    g.setFont(juce::Font(juce::FontOptions(12.0f)));
    g.drawText(paramreg::group(entry.parameterId), area, juce::Justification::centredLeft, true);
}

void MidiMapView::listBoxItemClicked(int row, const juce::MouseEvent& event)
{
    // Right-click clears, the same gesture as on the learn overlay, so the two
    // places you can undo an assignment are undone the same way.
    if (!event.mods.isPopupMenu() || row < 0 || row >= (int) rows.size())
        return;

    map.clearController(rows[(size_t) row].controller);
    refresh();

    if (changed)
        changed();
}

void MidiMapView::clearSelected()
{
    int row = list.getSelectedRow();
    if (row < 0 || row >= (int) rows.size())
        return;

    map.clearController(rows[(size_t) row].controller);
    refresh();

    if (changed)
        changed();
}

void MidiMapView::clearEverything()
{
    map.clearAll();
    refresh();

    if (changed)
        changed();
}

void MidiMapView::paint(juce::Graphics& g)
{
    g.fillAll(ui::colours::background);
}

void MidiMapView::resized()
{
    auto area = getLocalBounds().reduced(12);

    auto footer = area.removeFromBottom(34);
    area.removeFromBottom(8);

    closeButton.setBounds(footer.removeFromRight(84).reduced(0, 4));
    footer.removeFromRight(6);
    clearAllButton.setBounds(footer.removeFromRight(90).reduced(0, 4));
    footer.removeFromRight(6);
    clearButton.setBounds(footer.removeFromRight(72).reduced(0, 4));
    countLabel.setBounds(footer);

    list.setBounds(area);
}

void MidiMapView::show(MidiMap& mapToShow, std::function<void()> onChanged)
{
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = "MIDI Assignments";
    options.dialogBackgroundColour = ui::colours::background;
    options.content.setOwned(new MidiMapView(mapToShow, std::move(onChanged)));
    options.content->setSize(440, 380);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = true;

    options.launchAsync();
}
} // namespace wavelathe
