// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SkinDesigner.h"
#include "Theme.h"

namespace emx
{
using namespace juce;

//== One replaceable picture: preview, CHOOSE, X ===================================
class SkinDesigner::PartTile final : public Component, public SettableTooltipClient
{
public:
    PartTile (SkinDesigner& o, int p) : owner (o), part (p)
    {
        setTooltip (String (SkinDesign::partTitle (p)) + ": " + SkinDesign::partHelp (p));
        choose.setTooltip ("Pick a PNG or JPG for this part");
        clear.setTooltip ("Back to the EMULON CLASSIC look for this part");
        choose.onClick = [this] { owner.chooseImage (part); };
        clear.onClick = [this] { owner.design.images[part] = {}; owner.changed(); };
        addAndMakeVisible (choose);
        addAndMakeVisible (clear);
    }

    void refresh()
    {
        const auto& b = owner.design.images[part];
        preview = b.isEmpty() ? Image() : ImageFileFormat::loadFrom (b.data.getData(), b.data.getSize());
        if (preview.isValid() && (preview.getWidth() > 400 || preview.getHeight() > 400))   // small copy for drawing
            preview = preview.rescaled (jmax (1, preview.getWidth() * 200 / jmax (preview.getWidth(), preview.getHeight())),
                                        jmax (1, preview.getHeight() * 200 / jmax (preview.getWidth(), preview.getHeight())),
                                        Graphics::mediumResamplingQuality);
        clear.setEnabled (! b.isEmpty());
        repaint();
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto pic = r.removeFromTop (r.getHeight() - 38.0f).reduced (1.0f);
        theme::drawLed (g, pic, {});
        if (preview.isValid())
        {
            g.setImageResamplingQuality (Graphics::mediumResamplingQuality);
            g.drawImage (preview, pic.reduced (4.0f), RectanglePlacement::centred);
        }
        else
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (7.5f, 500, 0.05f));
            g.drawText ("CLASSIC", pic, Justification::centred, false);
        }
        g.setColour (theme::textStrong);
        g.setFont (theme::font (8.0f, 700, 0.08f));
        g.drawText (SkinDesign::partTitle (part), r.removeFromTop (16.0f), Justification::centred, true);
    }

    void resized() override
    {
        auto r = getLocalBounds().removeFromBottom (20);
        clear.setBounds (r.removeFromRight (24));
        r.removeFromRight (4);
        choose.setBounds (r);
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (e.mouseWasClicked())
            owner.chooseImage (part);
    }

private:
    SkinDesigner& owner;
    int part;
    Image preview;
    TextButton choose { "CHOOSE..." }, clear { "X" };
};

//== One colour: swatch (click to pick, right-click to clear) ======================
class SkinDesigner::ColourTile final : public Component, public SettableTooltipClient, private ChangeListener
{
public:
    ColourTile (SkinDesigner& o, int i) : owner (o), index (i)
    {
        setTooltip (String (SkinDesign::colourTitle (i)) + " - click to pick a colour, right-click to let it follow the accent");
        setMouseCursor (MouseCursor::PointingHandCursor);
    }

    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        auto dot = r.removeFromTop (r.getHeight() - 15.0f).withSizeKeepingCentre (24.0f, 24.0f);
        const auto it = owner.design.colours.find (SkinDesign::colourKey (index));
        const bool set = it != owner.design.colours.end();
        if (set)
        {
            g.setColour (it->second);
            g.fillEllipse (dot);
            g.setColour (theme::textStrong.withAlpha (0.8f));
            g.drawEllipse (dot, 1.5f);
        }
        else
        {
            g.setColour (theme::textMuted);
            g.drawEllipse (dot.reduced (1.0f), 1.0f);
            g.setFont (theme::font (7.0f, 500, 0.0f));
            g.drawText ("AUTO", dot, Justification::centred, false);
        }
        g.setColour (theme::textStrong);
        g.setFont (theme::font (7.5f, 700, 0.06f));
        g.drawText (SkinDesign::colourTitle (index), r, Justification::centred, false);
    }

    void mouseUp (const MouseEvent& e) override
    {
        if (! e.mouseWasClicked())
            return;
        const String key = SkinDesign::colourKey (index);
        if (e.mods.isPopupMenu())
        {
            owner.design.colours.erase (key);
            owner.changed();
            return;
        }
        auto sel = std::make_unique<ColourSelector> (ColourSelector::showColourAtTop | ColourSelector::showSliders | ColourSelector::showColourspace);
        const auto it = owner.design.colours.find (key);
        sel->setCurrentColour (it != owner.design.colours.end() ? it->second : theme::red, dontSendNotification);
        sel->setSize (300, 280);
        sel->addChangeListener (this);
        CallOutBox::launchAsynchronously (std::move (sel), getScreenBounds(), nullptr);
    }

private:
    void changeListenerCallback (ChangeBroadcaster* b) override
    {
        if (auto* sel = dynamic_cast<ColourSelector*> (b))
        {
            owner.design.colours[SkinDesign::colourKey (index)] = sel->getCurrentColour().withAlpha (1.0f);
            owner.changed();
        }
    }
    SkinDesigner& owner;
    int index;
};

//== The designer ==================================================================
SkinDesigner::SkinDesigner (Settings& s) : settings (s)
{
    // Start from the working copy, or from the custom skin in use, or from scratch.
    String err;
    const auto work = settings.getSkinsFolder().getChildFile (workFileName);
    if (! (work.existsAsFile() && SkinDesign::load (work, design, err)))
    {
        const auto current = settings.getSkinsFolder().getChildFile (settings.getCustomSkin());
        if (settings.getSkin() == 3 && settings.getCustomSkin().isNotEmpty())
            SkinDesign::load (current, design, err);
    }

    nameField.setPlaceholder ("SKIN NAME");
    nameField.setTooltip ("The name of your skin - click to type");
    nameField.onTextChange = [this] { design.name = nameField.getText().trim().substring (0, 60); changed(); };
    authorField.setPlaceholder ("YOUR NAME");
    authorField.setTooltip ("Who made it - click to type");
    authorField.onTextChange = [this] { design.author = authorField.getText().trim().substring (0, 60); changed(); };
    hueButton.setTooltip ("May the colour shift in Settings > Customization move this skin?");
    hueButton.onClick = [this] { design.hueShift = ! design.hueShift; changed(); };
    knobButton.setTooltip ("Pointer: your knob picture stays still and a pointer turns on it. Picture: the knob picture itself turns (point its marker straight up).");
    knobButton.onClick = [this] { design.knobRotates = ! design.knobRotates; changed(); };
    fontButton.setTooltip ("Pick a font file (.ttf or .otf) - it is packed into the skin");
    fontButton.onClick = [this] { chooseFont(); };
    fontClear.setTooltip ("Back to the standard font");
    fontClear.onClick = [this] { design.font = {}; changed(); };
    startOver.setTooltip ("Clear everything and start from the EMULON CLASSIC look");
    startOver.onClick = [this] { design = SkinDesign(); changed(); setStatus ("Started over."); };
    loadFolderButton.setTooltip ("Take every picture, font and skin.json from a folder (e.g. a template you edited) - or open a .emxd to keep working on it");
    loadFolderButton.onClick = [this] { loadFolder(); };
    templateButton.setTooltip ("Save all pictures (yours, or the Classic ones) into a folder, to edit them in any image program");
    templateButton.onClick = [this] { saveTemplate(); };
    exportButton.setTooltip ("Save the skin as one .emxd file to share - it is also added to your skins");
    exportButton.onClick = [this] { exportSkin(); };

    for (auto* c : { (Component*) &nameField, (Component*) &authorField, (Component*) &hueButton, (Component*) &knobButton,
                     (Component*) &fontButton, (Component*) &fontClear, (Component*) &startOver, (Component*) &loadFolderButton,
                     (Component*) &templateButton, (Component*) &exportButton })
        addAndMakeVisible (c);
    for (int p = 0; p < SkinDesign::numParts; ++p)
        addAndMakeVisible (tiles.add (new PartTile (*this, p)));
    for (int i = 0; i < SkinDesign::numColours; ++i)
        addAndMakeVisible (colourTiles.add (new ColourTile (*this, i)));
    refreshControls();
}

SkinDesigner::~SkinDesigner()
{
    if (isTimerRunning())
        timerCallback();   // don't lose the last change
}

void SkinDesigner::refreshControls()
{
    if (! nameField.isBeingEdited())   nameField.setText (design.name, dontSendNotification);
    if (! authorField.isBeingEdited()) authorField.setText (design.author, dontSendNotification);
    hueButton.setButtonText (design.hueShift ? "COLOUR SHIFT: ALLOWED" : "COLOUR SHIFT: OFF");
    hueButton.setToggleState (design.hueShift, dontSendNotification);
    knobButton.setButtonText (design.knobRotates ? "KNOB: PICTURE TURNS" : "KNOB: POINTER TURNS");
    fontName = "STANDARD FONT";
    if (! design.font.isEmpty())
        if (auto tf = Typeface::createSystemTypefaceFor (design.font.data.getData(), design.font.data.getSize()))
            fontName = tf->getName().toUpperCase();
    fontClear.setEnabled (! design.font.isEmpty());
    for (auto* t : tiles) t->refresh();
    repaint();
}

void SkinDesigner::changed()
{
    refreshControls();
    startTimer (250);
}

void SkinDesigner::timerCallback()
{
    stopTimer();
    String err;
    const auto work = settings.getSkinsFolder().getChildFile (workFileName);
    if (design.save (work, err))
        settings.useCustomSkin (workFileName);   // show it right away
    else
        setStatus (err);
}

void SkinDesigner::setStatus (const String& s) { status = s; repaint (statusRow); }

void SkinDesigner::chooseImage (int part)
{
    chooser = std::make_unique<FileChooser> (String ("Choose a picture: ") + SkinDesign::partTitle (part),
                                             File::getSpecialLocation (File::userPicturesDirectory), "*.png;*.jpg;*.jpeg");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, part] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == File())
            return;
        MemoryBlock mb;
        if (! f.loadFileAsData (mb) || ! ImageFileFormat::loadFrom (mb.getData(), mb.getSize()).isValid())
        {
            setStatus ("Can't read " + f.getFileName() + " as a picture.");
            return;
        }
        auto ext = f.getFileExtension().substring (1).toLowerCase();
        design.images[part] = { std::move (mb), ext == "jpeg" ? String ("jpg") : ext };
        changed();
        setStatus (String (SkinDesign::partTitle (part)) + ": " + f.getFileName());
    });
}

void SkinDesigner::chooseFont()
{
    chooser = std::make_unique<FileChooser> ("Choose a font", File::getSpecialLocation (File::userDocumentsDirectory), "*.ttf;*.otf");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == File())
            return;
        MemoryBlock mb;
        if (! f.loadFileAsData (mb) || Typeface::createSystemTypefaceFor (mb.getData(), mb.getSize()) == nullptr)
        {
            setStatus ("Can't use " + f.getFileName() + " as a font.");
            return;
        }
        design.font = { std::move (mb), f.getFileExtension().substring (1).toLowerCase() };
        changed();
        setStatus ("Font: " + f.getFileName() + " - check that its licence lets you share it with the skin.");
    });
}

void SkinDesigner::loadFolder()
{
    chooser = std::make_unique<FileChooser> ("Choose a skin folder (or a .emxd file)", File::getSpecialLocation (File::userDocumentsDirectory), "*.emxd");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories | FileBrowserComponent::canSelectFiles,
                          [this] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == File())
            return;
        SkinDesign d;
        String err;
        if (! SkinDesign::load (f, d, err))
        {
            setStatus (err + " in " + f.getFileName() + ".");
            return;
        }
        design = std::move (d);
        changed();
        setStatus ("Loaded " + f.getFileName() + ".");
    });
}

void SkinDesigner::saveTemplate()
{
    chooser = std::make_unique<FileChooser> ("Where should the template folder go?", File::getSpecialLocation (File::userDocumentsDirectory));
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [this] (const FileChooser& fc)
    {
        const auto parent = fc.getResult();
        if (parent == File())
            return;
        const auto dir = parent.getNonexistentChildFile (design.name.isNotEmpty() && design.name != "My Skin"
                                                             ? design.name + " (skin files)" : String ("Emulon X Skin Template"), "", false);
        // every part: yours, or the Classic one
        SkinDesign t = design;
        static const theme::Img order[SkinDesign::numParts] { theme::Img::background, theme::Img::frame, theme::Img::knob, theme::Img::slider,
                                                              theme::Img::panel, theme::Img::inset, theme::Img::display, theme::Img::button,
                                                              theme::Img::buttonOn, theme::Img::logo };
        for (int p = 0; p < SkinDesign::numParts; ++p)
        {
            if (! t.images[p].isEmpty())
                continue;
            String ext;
            auto mb = theme::defaultArtworkFile (order[p], ext);
            if (mb.isEmpty())
            {
                MemoryOutputStream png;
                PNGImageFormat().writeImageToStream (theme::defaultPartImage (order[p]), png);
                mb = png.getMemoryBlock();
                ext = "png";
            }
            t.images[p] = { std::move (mb), ext };
        }
        String err;
        if (! t.saveFolder (dir, err))
        {
            setStatus (err);
            return;
        }
        String howTo;
        howTo << "EMULON X - SKIN FILES\r\n=====================\r\n\r\n"
              << "1. Edit the pictures in any image program. Keep the file names (you can switch between .png and .jpg).\r\n"
              << "   Delete a picture to keep the EMULON CLASSIC look for that part.\r\n"
              << "2. Optional: put a font file in here (any name, .ttf or .otf).\r\n"
              << "3. Optional: open skin.json in a text editor for the name, author and colours (#rrggbb).\r\n"
              << "4. In Emulon X: Settings > Skin designer > LOAD FOLDER... and pick this folder.\r\n"
              << "   Check how it looks, then press EXPORT .EMXD... to make one file you can share.\r\n\r\nTHE PARTS\r\n---------\r\n";
        for (int p = 0; p < SkinDesign::numParts; ++p)
            howTo << SkinDesign::partKey (p) << ": " << SkinDesign::partHelp (p) << "\r\n";
        howTo << "\r\n9-slice parts (panel, inset, display, button, button-on) keep their corners and stretch the middle.\r\n"
              << "They are drawn at half their pixel size, so they stay sharp at 200 % interface size.\r\n";
        dir.getChildFile ("HOW TO.txt").replaceWithText (howTo);
        dir.revealToUser();
        setStatus ("Template saved in " + dir.getFullPathName());
    });
}

void SkinDesigner::exportSkin()
{
    timerCallback();   // the working copy is up to date and in use
    const auto safe = File::createLegalFileName (design.name.isNotEmpty() ? design.name : String ("My Skin"));
    chooser = std::make_unique<FileChooser> ("Export the skin", File::getSpecialLocation (File::userDocumentsDirectory).getChildFile (safe + ".emxd"), "*.emxd");
    chooser->launchAsync (FileBrowserComponent::saveMode | FileBrowserComponent::canSelectFiles | FileBrowserComponent::warnAboutOverwriting,
                          [this] (const FileChooser& fc)
    {
        auto f = fc.getResult();
        if (f == File())
            return;
        if (! f.hasFileExtension ("emxd"))
            f = f.withFileExtension ("emxd");
        String err;
        if (! design.save (f, err))
        {
            setStatus (err);
            return;
        }
        // add it to your skins and use it
        const auto installed = settings.getSkinsFolder().getChildFile (f.getFileName());
        if (installed != f)
            f.copyFileTo (installed);
        settings.useCustomSkin (installed.getFileName());
        setStatus ("Exported " + f.getFullPathName() + " - and added to your skins.");
    });
}

void SkinDesigner::resized()
{
    auto r = getLocalBounds();
    rowName = r.removeFromTop (24);
    {
        auto row = rowName;
        row.removeFromLeft (46);
        nameField.setBounds (row.removeFromLeft (210));
        row.removeFromLeft (40);
        authorField.setBounds (row.removeFromLeft (170));
    }
    r.removeFromTop (8);
    rowFont = r.removeFromTop (26);
    {
        auto row = rowFont;
        hueButton.setBounds (row.removeFromLeft (170));
        row.removeFromLeft (6);
        knobButton.setBounds (row.removeFromLeft (160));
        row.removeFromLeft (10);
        fontClear.setBounds (row.removeFromRight (26));
        row.removeFromRight (4);
        fontButton.setBounds (row.removeFromRight (110));
    }
    r.removeFromTop (10);
    const int cols = 5, gap = 8;
    const int tw = (r.getWidth() - (cols - 1) * gap) / cols, th = 92;
    for (int i = 0; i < tiles.size(); ++i)
        tiles[i]->setBounds (r.getX() + (i % cols) * (tw + gap), r.getY() + (i / cols) * (th + gap), tw, th);
    r.removeFromTop (2 * (th + gap) + 2);
    colourRow = r.removeFromTop (44);
    {
        auto row = colourRow.withTrimmedLeft (60);
        const int cw = row.getWidth() / colourTiles.size();
        for (int i = 0; i < colourTiles.size(); ++i)
            colourTiles[i]->setBounds (row.getX() + i * cw, row.getY(), cw, row.getHeight());
    }
    r.removeFromTop (8);
    auto buttons = r.removeFromTop (28);
    const int bw = (buttons.getWidth() - 3 * 8) / 4;
    for (auto* b : { &startOver, &loadFolderButton, &templateButton, &exportButton })
    {
        b->setBounds (buttons.removeFromLeft (bw));
        buttons.removeFromLeft (8);
    }
    r.removeFromTop (6);
    statusRow = r.removeFromTop (16);
}

void SkinDesigner::paint (Graphics& g)
{
    g.setColour (theme::textStrong);
    g.setFont (theme::font (8.5f, 700, 0.1f));
    g.drawText ("NAME", rowName.withWidth (44), Justification::centredLeft, false);
    g.drawText ("BY", rowName.withTrimmedLeft (46 + 210 + 8).withWidth (30), Justification::centredLeft, false);
    g.drawText ("COLOURS", colourRow.withWidth (58), Justification::centredLeft, false);
    // the font in use, between the toggles and the font buttons
    {
        auto a = rowFont.withTrimmedLeft (170 + 6 + 160 + 10).withTrimmedRight (26 + 4 + 110 + 6).toFloat();
        theme::drawLed (g, a, "FONT: " + fontName, 8.5f);
    }
    g.setColour (theme::textMuted);
    g.setFont (theme::font (8.0f, 500, 0.04f));
    g.drawText (status.isNotEmpty() ? status : String ("Changes show right away. Pictures you don't choose keep the Classic look, tinted to your accent colour."),
                statusRow, Justification::centredLeft, true);
}
} // namespace emx
