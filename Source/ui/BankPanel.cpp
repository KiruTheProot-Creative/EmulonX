// SPDX-License-Identifier: AGPL-3.0-or-later
#include "BankPanel.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

//== The presets of the bank as a grid of cards (columns set in Settings) ==========
class BankPanel::Grid final : public Component, public SettableTooltipClient
{
public:
    explicit Grid (BankPanel& o) : owner (o)
    {
        setWantsKeyboardFocus (true);
        setTooltip ("Click to select - double-click (or Enter) to load - Delete removes it from the bank");
    }

    static constexpr int cellH = 26, gap = 5;
    int columns() const { return owner.proc.getSettings().getBankColumns(); }
    int count() const   { return (int) owner.proc.getBank().size(); }
    int heightFor (int width) const
    {
        juce::ignoreUnused (width);
        const int rowsNeeded = (count() + columns() - 1) / columns();
        return jmax (1, rowsNeeded * (cellH + gap) - gap);
    }
    Rectangle<float> cell (int i) const
    {
        const int c = columns();
        const float w = ((float) getWidth() - (float) ((c - 1) * gap)) / (float) c;
        return { (float) (i % c) * (w + (float) gap), (float) ((i / c) * (cellH + gap)), w, (float) cellH };
    }
    int indexAt (Point<float> p) const
    {
        for (int i = 0; i < count(); ++i)
            if (cell (i).contains (p))
                return i;
        return -1;
    }

    void paint (Graphics& g) override
    {
        const auto& bank = owner.proc.getBank();
        for (int i = 0; i < (int) bank.size(); ++i)
        {
            const auto r = cell (i);
            const bool sel = i == owner.selected, over = i == hover;
            g.setColour (sel ? theme::red.withAlpha (0.22f) : theme::hue (0xff16121a).withAlpha (over ? 1.0f : 0.85f));
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (sel ? theme::red.withAlpha (0.9f) : (over ? theme::redDim.withAlpha (0.7f) : theme::edge));
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

            auto t = r.reduced (8.0f, 0.0f);
            g.setFont (theme::font (8.5f, 500, 0.04f));
            g.setColour (theme::textMuted);
            g.drawText (String (i + 1).paddedLeft ('0', 2), t.removeFromLeft (22.0f), Justification::centredLeft, false);
            if (t.getWidth() > 150.0f)
            {
                g.setFont (theme::font (7.5f, 500, 0.04f));
                g.drawText (String (bank[(size_t) i].data.getSize() / (1024.0 * 1024.0), 1) + " MB", t.removeFromRight (44.0f),
                            Justification::centredRight, false);
            }
            g.setFont (theme::font (10.0f, 500, 0.04f));
            g.setColour (sel ? theme::textStrong : theme::listText);
            g.drawText (bank[(size_t) i].name, t, Justification::centredLeft, true);
        }
    }

    void mouseMove (const MouseEvent& e) override { setHover (indexAt (e.position)); }
    void mouseExit (const MouseEvent&) override   { setHover (-1); }
    void mouseDown (const MouseEvent& e) override
    {
        grabKeyboardFocus();
        owner.select (indexAt (e.position));
    }
    void mouseDoubleClick (const MouseEvent& e) override
    {
        if (const int i = indexAt (e.position); i >= 0)
            owner.proc.loadBankEntry (i);
    }
    bool keyPressed (const KeyPress& k) override
    {
        const int sel = owner.selected, c = columns(), n = count();
        if (n == 0) return false;
        if (k == KeyPress::returnKey && sel >= 0) { owner.proc.loadBankEntry (sel); return true; }
        if ((k == KeyPress::deleteKey || k == KeyPress::backspaceKey) && sel >= 0) { owner.proc.removeBankEntry (sel); return true; }
        int next = sel;
        if (k == KeyPress::leftKey)  next = sel - 1;
        if (k == KeyPress::rightKey) next = sel + 1;
        if (k == KeyPress::upKey)    next = sel - c;
        if (k == KeyPress::downKey)  next = sel + c;
        if (next != sel)
        {
            owner.select (jlimit (0, n - 1, sel < 0 ? 0 : next));
            if (auto* vp = findParentComponentOfClass<Viewport>())
            {
                const auto r = cell (owner.selected).toNearestInt();
                if (r.getY() < vp->getViewPositionY())
                    vp->setViewPosition (0, r.getY());
                else if (r.getBottom() > vp->getViewPositionY() + vp->getViewHeight())
                    vp->setViewPosition (0, r.getBottom() - vp->getViewHeight());
            }
            return true;
        }
        return false;
    }

private:
    void setHover (int i) { if (i != hover) { hover = i; repaint(); } }
    BankPanel& owner;
    int hover = -1;
};

BankPanel::BankPanel (EmulonXProcessor& p) : proc (p)
{
    nameLabel.setTooltip ("Bank name - click to rename");
    nameLabel.setPlaceholder ("MY BANK");
    nameLabel.onTextChange = [this]
    {
        const auto n = nameLabel.getText().trim();
        proc.setBankName (n.isNotEmpty() ? n.substring (0, 40) : String ("My Bank"));
    };
    addAndMakeVisible (nameLabel);

    grid = std::make_unique<Grid> (*this);
    viewport.setViewedComponent (grid.get(), false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);

    addButton.setTooltip ("Put a copy of the current preset (with its samples) into the bank");
    addButton.onClick = [this] { proc.addCurrentPresetToBank(); };
    loadButton.setTooltip ("Load the selected preset (or double-click it)");
    loadButton.onClick = [this] { proc.loadBankEntry (selected); };
    removeButton.setTooltip ("Take the selected preset out of the bank");
    removeButton.onClick = [this] { proc.removeBankEntry (selected); };
    clearButton.setTooltip ("Empty the bank");
    clearButton.onClick = [this] { proc.clearBank(); };

    openButton.setTooltip ("Open an .emxb bank file into this list");
    openButton.onClick = [this]
    {
        auto folders = proc.getSettings().getFolders (Settings::Folder::base);
        chooser = std::make_unique<FileChooser> ("Open bank", folders.isEmpty() ? File() : File (folders[0]), "*.emxb;*.zip");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles,
                              [this] (const FileChooser& fc)
                              {
                                  if (fc.getResult() != File())
                                      proc.openBank (fc.getResult());
                              });
    };

    exportButton.setTooltip ("Save every preset in the list as one .emxb bank file");
    exportButton.onClick = [this]
    {
        auto folders = proc.getSettings().getFolders (Settings::Folder::other);
        if (folders.isEmpty())
            folders = proc.getSettings().getFolders (Settings::Folder::base);
        const auto dir = folders.isEmpty() ? proc.getSettings().getUserPresetFolder() : File (folders[0]);
        chooser = std::make_unique<FileChooser> ("Export bank", dir.getChildFile (loader::safeFileName (proc.getBankName()) + ".emxb"), "*.emxb");
        chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                              [this] (const FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f == File())
                                      return;
                                  if (! f.hasFileExtension ("emxb"))
                                      f = f.withFileExtension ("emxb");
                                  proc.exportBank (f);
                              });
    };

    for (auto* b : { &addButton, &loadButton, &removeButton, &openButton, &exportButton, &clearButton })
        addAndMakeVisible (b);

    proc.addChangeListener (this);
    proc.getSettings().addChangeListener (this);   // grid columns
    refresh();
}

BankPanel::~BankPanel()
{
    proc.getSettings().removeChangeListener (this);
    proc.removeChangeListener (this);
}

void BankPanel::select (int index)
{
    selected = index;
    const bool has = selected >= 0 && selected < (int) proc.getBank().size();
    loadButton.setEnabled (has);
    removeButton.setEnabled (has);
    grid->repaint();
}

void BankPanel::changeListenerCallback (ChangeBroadcaster*) { refresh(); }

void BankPanel::refresh()
{
    if (! nameLabel.isBeingEdited())
        nameLabel.setText (proc.getBankName(), dontSendNotification);
    const int n = (int) proc.getBank().size();
    if (selected >= n)
        selected = n - 1;
    const bool any = n > 0;
    for (auto* b : { &exportButton, &clearButton })
        b->setEnabled (any);
    addButton.setEnabled (! proc.getZones().empty());
    select (selected);
    resized();
    repaint();
}

void BankPanel::resized()
{
    auto r = getLocalBounds().reduced (11, 9);
    auto head = r.removeFromTop (18);
    head.removeFromLeft (50);   // "BANK"
    nameLabel.setBounds (head.removeFromLeft (jmin (230, head.getWidth() / 2)));
    r.removeFromTop (8);

    auto side = r.removeFromRight (2 * 104 + 6);
    r.removeFromRight (7);
    auto colA = side.removeFromLeft (104);
    side.removeFromLeft (6);
    auto colB = side;
    for (auto* b : { &addButton, &loadButton, &removeButton })
    {
        b->setBounds (colA.removeFromTop (24));
        colA.removeFromTop (6);
    }
    for (auto* b : { &openButton, &exportButton, &clearButton })
    {
        b->setBounds (colB.removeFromTop (24));
        colB.removeFromTop (6);
    }
    listArea = r.toFloat();
    viewport.setBounds (r.reduced (6));
    const int w = viewport.getWidth() - (grid->heightFor (viewport.getWidth()) > viewport.getHeight() ? viewport.getScrollBarThickness() + 2 : 0);
    grid->setSize (w, grid->heightFor (w));
}

void BankPanel::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    theme::drawPanel (g, r);
    theme::drawHeading (g, r.reduced (10.0f, 9.0f), "Bank");

    auto head = getLocalBounds().reduced (11, 9).removeFromTop (18).toFloat();
    head.removeFromLeft (50.0f + (float) jmin (230, (getWidth() - 72) / 2) + 10.0f);
    g.setColour (theme::textMuted);
    g.setFont (theme::font (8.0f, 500, 0.1f));
    g.drawText (String ((int) proc.getBank().size()) + " PRESET(S) - EXPORT AS ONE .EMXB FILE", head, Justification::centredLeft, true);

    theme::drawLed (g, listArea, {});
    if (proc.getBank().empty())
    {
        g.setColour (theme::textMuted);
        g.setFont (theme::font (9.0f, 500, 0.06f));
        g.drawFittedText ("EMPTY. PRESS ADD CURRENT TO COLLECT PRESETS, OR OPEN AN .EMXB BANK.",
                          listArea.reduced (10.0f, 8.0f).toNearestInt(), Justification::topLeft, 2, 1.0f);
    }
}
} // namespace emx
