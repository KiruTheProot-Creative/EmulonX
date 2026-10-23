// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Explorer.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

namespace
{
enum class Kind { root, category, folder, audio, preset, bank, bankPreset, placeholder };
enum class Show { samples, banks, presets };   // what a folder lists

static constexpr int maxEntriesPerFolder = 3000;

void drawIcon (Graphics& g, Kind kind, Rectangle<float> r, bool open)
{
    r = r.withSizeKeepingCentre (12.0f, 10.0f);
    Path p;
    switch (kind)
    {
        case Kind::folder:
            p.startNewSubPath (r.getX(), r.getY() + 1.0f);
            p.lineTo (r.getX() + 4.5f, r.getY() + 1.0f);
            p.lineTo (r.getX() + 6.0f, r.getY() + 2.5f);
            p.lineTo (r.getRight(), r.getY() + 2.5f);
            p.lineTo (r.getRight(), r.getBottom());
            p.lineTo (r.getX(), r.getBottom());
            p.closeSubPath();
            g.setColour (open ? theme::red : theme::redDim.withAlpha (0.8f));
            g.strokePath (p, PathStrokeType (1.2f));
            return;
        case Kind::bank:
            for (int i = 0; i < 3; ++i)
                p.addRoundedRectangle (r.getX() + (float) i * 4.0f, r.getY(), 3.0f, r.getHeight(), 0.8f);
            g.setColour (theme::red);
            g.fillPath (p);
            return;
        case Kind::preset:
        case Kind::bankPreset:
            p.addRoundedRectangle (r.reduced (1.5f, 0.0f), 1.5f);
            g.setColour (theme::red);
            g.strokePath (p, PathStrokeType (1.2f));
            g.fillRect (r.reduced (4.0f, 3.5f));
            return;
        case Kind::audio:
        {
            const float h[] { 0.3f, 0.8f, 0.5f, 1.0f, 0.4f, 0.7f };
            for (int i = 0; i < 6; ++i)
            {
                const float bh = r.getHeight() * h[i];
                g.setColour (theme::redDim);
                g.fillRect (r.getX() + (float) i * 2.0f, r.getCentreY() - bh * 0.5f, 1.2f, bh);
            }
            return;
        }
        case Kind::root: case Kind::category: case Kind::placeholder:
            return;
    }
}

class Item final : public TreeViewItem
{
public:
    Item (Explorer& e, Kind k, String text, File f = {}, Show s = Show::samples)
        : explorer (e), kind (k), label (std::move (text)), file (std::move (f)), show (s) {}

    Settings::Folder rootFolder {};   // for folders added in the settings (can be removed from the list)
    bool isRootFolder = false;
    String bankEntry;                 // for presets inside a bank

    bool mightContainSubItems() override
    {
        return kind == Kind::root || kind == Kind::category || kind == Kind::folder || kind == Kind::bank;
    }
    bool canBeSelected() const override { return kind != Kind::category && kind != Kind::placeholder && kind != Kind::root; }
    int getItemHeight() const override { return kind == Kind::category ? 24 : 18; }
    String getUniqueName() const override
    {
        return label + "|" + file.getFullPathName() + "|" + bankEntry + "|" + String ((int) kind);
    }

    void paintItem (Graphics& g, int width, int height) override
    {
        auto r = Rectangle<float> (0, 0, (float) width, (float) height);
        if (kind == Kind::category)
        {
            g.setColour (Colours::white);
            g.setFont (theme::font (9.0f, 700, 0.14f));
            g.drawText (label, r.reduced (2.0f, 0.0f), Justification::centredLeft, true);
            g.setColour (theme::red.withAlpha (0.45f));
            g.fillRect (r.removeFromBottom (4.0f).removeFromTop (1.0f).withTrimmedRight (6.0f));
            return;
        }
        if (isSelected())
        {
            g.setColour (theme::red.withAlpha (0.2f));
            g.fillRoundedRectangle (r.reduced (0.0f, 1.0f), 3.0f);
        }
        if (kind == Kind::placeholder)
        {
            g.setColour (theme::redDark);
            g.setFont (theme::font (8.0f, 500, 0.06f));
            g.drawText (label, r.reduced (2.0f, 0.0f), Justification::centredLeft, true);
            return;
        }
        drawIcon (g, kind, r.removeFromLeft (16.0f), isOpen());
        r.removeFromLeft (4.0f);
        g.setColour (isSelected() ? Colour (0xffff7070) : (kind == Kind::folder || kind == Kind::bank ? Colour (0xffe0d8d8) : Colour (0xffdd3333)));
        g.setFont (theme::font (9.0f, 500, 0.03f));
        g.drawText (label, r, Justification::centredLeft, true);
    }

    void itemOpennessChanged (bool isNowOpen) override
    {
        if (isNowOpen && getNumSubItems() == 0)
            populate();
    }

    void itemClicked (const MouseEvent& e) override
    {
        if (e.mods.isPopupMenu())
        {
            showMenu();
            return;
        }
        switch (kind)
        {
            case Kind::audio:
                explorer.proc.previewFile (file);
                explorer.setHint ("PREVIEW: " + file.getFileNameWithoutExtension() + " - DOUBLE-CLICK TO ADD");
                break;
            case Kind::preset:
            case Kind::bankPreset:
                explorer.setHint ("DOUBLE-CLICK TO LOAD " + label);
                break;
            case Kind::bank:
                explorer.setHint ("DOUBLE-CLICK TO OPEN IN THE BANK LIST");
                break;
            case Kind::folder:
                if (! e.mods.isAnyModifierKeyDown())
                    setOpen (! isOpen());
                break;
            case Kind::root: case Kind::category: case Kind::placeholder:
                break;
        }
    }

    void itemDoubleClicked (const MouseEvent&) override
    {
        switch (kind)
        {
            case Kind::audio:
                explorer.proc.stopPreview();
                explorer.proc.loadFiles ({ file });
                break;
            case Kind::preset:
                explorer.proc.loadFiles ({ file });
                break;
            case Kind::bank:
                explorer.proc.openBank (file);
                break;
            case Kind::bankPreset:
                explorer.proc.loadPresetFromBankFile (file, bankEntry, label);
                break;
            case Kind::root: case Kind::category: case Kind::folder: case Kind::placeholder:
                break;
        }
    }

    var getDragSourceDescription() override { return {}; }

    void populate()
    {
        clearSubItems();
        if (kind == Kind::folder)
        {
            Array<File> dirs, files;
            int count = 0;
            for (const auto& entry : RangedDirectoryIterator (file, false, "*", File::findFilesAndDirectories | File::ignoreHiddenFiles))
            {
                if (++count > maxEntriesPerFolder)
                    break;
                const auto& f = entry.getFile();
                if (entry.isDirectory())
                    dirs.add (f);
                else if (accepts (f))
                    files.add (f);
            }
            auto byName = [] (const File& a, const File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; };
            std::sort (dirs.begin(), dirs.end(), byName);
            std::sort (files.begin(), files.end(), byName);
            for (auto& d : dirs)
                addSubItem (new Item (explorer, Kind::folder, d.getFileName(), d, show));
            for (auto& f : files)
            {
                const auto k = loader::isBankFile (f) ? Kind::bank : loader::isPresetFile (f) ? Kind::preset : Kind::audio;
                auto name = f.getFileName();
                for (auto ext : { ".emxp.zip", ".emxb.zip", ".emxp", ".emxb" })
                    if (name.endsWithIgnoreCase (ext))
                    {
                        name = name.dropLastCharacters ((int) strlen (ext));
                        break;
                    }
                addSubItem (new Item (explorer, k, name, f, show));
            }
            if (! file.isDirectory())
                addSubItem (new Item (explorer, Kind::placeholder, "FOLDER NOT FOUND"));
            else if (dirs.isEmpty() && files.isEmpty())
                addSubItem (new Item (explorer, Kind::placeholder, show == Show::samples ? "NO AUDIO FILES" : "NOTHING HERE YET"));
        }
        else if (kind == Kind::bank)
        {
            const auto info = loader::readBankInfo (file);
            for (int i = 0; i < info.presetEntries.size(); ++i)
            {
                auto* it = new Item (explorer, Kind::bankPreset, info.presetNames[i], file, show);
                it->bankEntry = info.presetEntries[i];
                addSubItem (it);
            }
            if (info.error.isNotEmpty())
                addSubItem (new Item (explorer, Kind::placeholder, info.error.toUpperCase()));
        }
    }

private:
    bool accepts (const File& f) const
    {
        switch (show)
        {
            case Show::samples: return loader::isAudioFile (f);
            case Show::banks:   return loader::isBankFile (f);
            case Show::presets: return loader::isPresetFile (f) || loader::isBankFile (f);
        }
        return false;
    }

    void showMenu()
    {
        PopupMenu m;
        if (file != File())
            m.addItem ("Show in file browser", [f = file] { f.revealToUser(); });
        Component::SafePointer<Explorer> ex (&explorer);
        if (isRootFolder)
            m.addItem ("Remove from the list", [ex, which = rootFolder, path = file.getFullPathName()]
                       { if (ex != nullptr) ex->removeRootFolder (which, path); });
        m.addItem ("Rescan", [ex] { if (ex != nullptr) ex->rescan(); });
        if (m.getNumItems() > 0)
            m.showMenuAsync (PopupMenu::Options());
    }

    Explorer& explorer;
    Kind kind;
    String label;
    File file;
    Show show;
};
} // namespace

//== Folder + button =============================================================
FolderAddButton::FolderAddButton() : Button ("Add folder") { setTooltip ("Add a folder to the explorer"); }

void FolderAddButton::paintButton (Graphics& g, bool highlighted, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, {}, highlighted, down);
    auto r = getLocalBounds().toFloat().withSizeKeepingCentre (16.0f, 12.0f).translated (-1.0f, down ? 1.0f : 0.0f);
    Path p;
    p.startNewSubPath (r.getX(), r.getY() + 1.0f);
    p.lineTo (r.getX() + 5.0f, r.getY() + 1.0f);
    p.lineTo (r.getX() + 6.5f, r.getY() + 2.5f);
    p.lineTo (r.getRight(), r.getY() + 2.5f);
    p.lineTo (r.getRight(), r.getBottom());
    p.lineTo (r.getX(), r.getBottom());
    p.closeSubPath();
    g.setColour (highlighted ? Colours::white : Colour (0xffdddddd));
    g.strokePath (p, PathStrokeType (1.4f));
    const auto c = r.getCentre().translated (1.0f, 1.0f);
    g.setColour (theme::red);
    g.fillRect (c.x - 3.5f, c.y - 0.8f, 7.0f, 1.6f);
    g.fillRect (c.x - 0.8f, c.y - 3.5f, 1.6f, 7.0f);
}

//== Explorer ====================================================================
Explorer::Explorer (EmulonXProcessor& p) : proc (p)
{
    addButton.onClick = [this] { addFolder(); };
    addAndMakeVisible (addButton);

    tabSamples.setTooltip ("Audio files in your sample folders - click to preview, double-click to add as a zone");
    tabBanks.setTooltip ("Banks and presets - double-click to load");
    tabSamples.onClick = [this] { showTab (0); };
    tabBanks.onClick = [this] { showTab (1); };
    addAndMakeVisible (tabSamples);
    addAndMakeVisible (tabBanks);

    tree.setColour (TreeView::backgroundColourId, Colours::transparentBlack);
    tree.setColour (TreeView::linesColourId, theme::redDark.withAlpha (0.5f));
    tree.setColour (TreeView::selectedItemBackgroundColourId, Colours::transparentBlack);
    tree.setRootItemVisible (false);
    tree.setIndentSize (14);
    tree.setDefaultOpenness (false);
    addAndMakeVisible (tree);

    settings->addChangeListener (this);
    proc.addChangeListener (this);
    lastFilesWritten = proc.getFilesWrittenCounter();
    showTab (1);
}

Explorer::~Explorer()
{
    proc.stopPreview();
    settings->removeChangeListener (this);
    proc.removeChangeListener (this);
    tree.setRootItem (nullptr);
}

void Explorer::changeListenerCallback (ChangeBroadcaster* source)
{
    if (source == &proc)
    {
        if (proc.getFilesWrittenCounter() != lastFilesWritten)
        {
            lastFilesWritten = proc.getFilesWrittenCounter();
            rescan();
        }
        return;
    }
    rescan();   // settings changed (folders may have been added or removed)
}

void Explorer::showTab (int index)
{
    tab = index;
    tabSamples.setToggleState (tab == 0, dontSendNotification);
    tabBanks.setToggleState (tab == 1, dontSendNotification);
    hint = tab == 0 ? "CLICK TO PREVIEW - DOUBLE-CLICK TO ADD" : "DOUBLE-CLICK A PRESET TO LOAD IT";
    rescan();
}

void Explorer::rescan()
{
    if (rootItem != nullptr)
        openness[treeTab] = tree.getOpennessState (true);
    treeTab = tab;

    tree.setRootItem (nullptr);
    auto root = std::make_unique<Item> (*this, Kind::root, "root");

    auto addCategory = [&] (const String& title, Settings::Folder which, Show show, const String& emptyText)
    {
        auto* cat = new Item (*this, Kind::category, title);
        root->addSubItem (cat);
        const auto folders = settings->getFolders (which);
        for (auto& path : folders)
        {
            const File dir (path);
            auto* it = new Item (*this, Kind::folder, dir.getFileName().isNotEmpty() ? dir.getFileName() : path, dir, show);
            it->isRootFolder = true;
            it->rootFolder = which;
            cat->addSubItem (it);
        }
        if (folders.isEmpty())
            cat->addSubItem (new Item (*this, Kind::placeholder, emptyText));
        cat->setOpen (true);
    };

    if (tab == 0)
        addCategory ("SAMPLE FOLDERS", Settings::Folder::samples, Show::samples, "NO FOLDERS - PRESS THE FOLDER + BUTTON");
    else
    {
        addCategory ("BASE BANKS", Settings::Folder::base, Show::banks, "NONE YET - THEY ARRIVE WITH 1.0");
        addCategory ("OTHER BANKS", Settings::Folder::other, Show::banks, "NO FOLDERS - PRESS FOLDER +");
        addCategory ("USER", Settings::Folder::user, Show::presets, "NO FOLDERS - PRESS FOLDER +");
    }

    rootItem = std::move (root);
    tree.setRootItem (rootItem.get());
    if (openness[tab] != nullptr)
        tree.restoreOpennessState (*openness[tab], true);
    repaint();
}

void Explorer::addFolder()
{
    if (tab == 0)
    {
        chooseFolderFor (Settings::Folder::samples);
        return;
    }
    PopupMenu m;
    m.addSectionHeader ("Add a folder to");
    m.addItem ("Base banks", [this] { chooseFolderFor (Settings::Folder::base); });
    m.addItem ("Other banks", [this] { chooseFolderFor (Settings::Folder::other); });
    m.addItem ("User presets", [this] { chooseFolderFor (Settings::Folder::user); });
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&addButton));
}

void Explorer::chooseFolderFor (Settings::Folder which)
{
    chooser = std::make_unique<FileChooser> ("Choose a folder", File::getSpecialLocation (File::userHomeDirectory));
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                          [this, which] (const FileChooser& fc)
                          {
                              const auto dir = fc.getResult();
                              if (dir.isDirectory())
                              {
                                  settings->addFolder (which, dir);   // triggers a rescan
                                  showTab (which == Settings::Folder::samples ? 0 : 1);
                              }
                          });
}

void Explorer::removeRootFolder (Settings::Folder which, const String& path)
{
    // Called from inside a tree item, which the rescan deletes: do it afterwards.
    Component::SafePointer<Explorer> safe (this);
    MessageManager::callAsync ([safe, which, path]
    {
        if (safe != nullptr)
            safe->settings->removeFolder (which, path);
    });
}

void Explorer::resized()
{
    auto r = getLocalBounds().reduced (11, 9);
    auto head = r.removeFromTop (18);
    addButton.setBounds (head.removeFromRight (30).withSizeKeepingCentre (30, 22));
    r.removeFromTop (8);
    auto tabs = r.removeFromTop (24);
    const int w = (tabs.getWidth() - 6) / 2;
    tabSamples.setBounds (tabs.removeFromLeft (w));
    tabBanks.setBounds (tabs.removeFromRight (w));
    r.removeFromTop (8);
    hintArea = r.removeFromBottom (30).toFloat();
    r.removeFromBottom (6);
    treeArea = r.toFloat();
    tree.setBounds (r.reduced (4, 5));
}

void Explorer::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f);
    theme::drawPanel (g, r);
    theme::drawHeading (g, r.reduced (10.0f, 9.0f), "Explorer");
    theme::drawLed (g, treeArea, {});
    theme::drawLed (g, hintArea, {});
    g.setColour (theme::redDim);
    g.setFont (theme::font (8.0f, 500, 0.05f));
    g.drawFittedText (hint, hintArea.reduced (7.0f, 2.0f).toNearestInt(), Justification::centredLeft, 2, 1.0f);
}
} // namespace emx
