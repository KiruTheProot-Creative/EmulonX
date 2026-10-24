// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Inspector.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include "PreviewView.h"
#include "ui/Theme.h"

namespace emx::sd
{
using namespace juce;

namespace
{
String hex (Colour c) { return "#" + c.toDisplayString (false).toLowerCase(); }
var cloneVar (const var& v) { return JSON::parse (JSON::toString (v, true)); }

void paintLabel (Graphics& g, Rectangle<int> r, const String& text)
{
    g.setColour (theme::textMuted);
    g.setFont (theme::font (8.5f, 500, 0.05f));
    g.drawFittedText (text.toUpperCase(), r, Justification::centredLeft, 2, 0.8f);
}

constexpr int labelWidth = 96;

/** Calls `fn` after the current mouse click has finished (it may delete the control that was clicked). */
void later (std::function<void()> fn)
{
    MessageManager::callAsync (std::move (fn));
}

//== Rows ==========================================================================
class HeadingRow final : public Row
{
public:
    HeadingRow (String t, String s) : title (std::move (t)), sub (std::move (s)) {}
    int heightFor (int) const override { return 26; }
    void paint (Graphics& g) override
    {
        g.setColour (Colours::black.withAlpha (0.6f));
        g.fillRect (0, 3, getWidth(), 1);
        g.setColour (Colours::white.withAlpha (0.05f));
        g.fillRect (0, 4, getWidth(), 1);
        auto r = getLocalBounds().withTrimmedTop (8);
        g.setColour (theme::textStrong);
        const auto f = theme::font (9.5f, 700, 0.14f);
        g.setFont (f);
        g.drawText (title.toUpperCase(), r, Justification::centredLeft, false);
        if (sub.isNotEmpty())
        {
            GlyphArrangement ga;
            ga.addLineOfText (f, title.toUpperCase(), 0, 0);
            g.setColour (theme::textMuted);
            g.setFont (theme::font (7.0f, 500, 0.08f));
            g.drawText (sub.toUpperCase(), r.withTrimmedLeft ((int) ga.getBoundingBox (0, -1, true).getWidth() + 8), Justification::centredLeft, true);
        }
    }
private:
    String title, sub;
};

class NoteRow final : public Row
{
public:
    explicit NoteRow (String t, bool box = false) : text (std::move (t)), boxed (box) {}
    int heightFor (int width) const override
    {
        TextLayout tl;
        tl.createLayout (attributed(), (float) width - (boxed ? 18.0f : 0.0f));
        return (int) std::ceil (tl.getHeight()) + (boxed ? 16 : 4);
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        if (boxed)
        {
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRoundedRectangle (r, 5.0f);
            g.setColour (Colours::black);
            g.drawRoundedRectangle (r.reduced (0.5f), 5.0f, 1.0f);
            r = r.reduced (9.0f, 8.0f);
        }
        attributed().draw (g, r);
    }
private:
    AttributedString attributed() const
    {
        AttributedString s;
        s.setWordWrap (AttributedString::byWord);
        // **bold** parts are brighter
        bool bold = false;
        for (int start = 0;;)
        {
            const int i = text.indexOf (start, "**");
            const auto part = i < 0 ? text.substring (start) : text.substring (start, i);
            if (part.isNotEmpty())
                s.append (part, theme::font (8.0f, bold ? 700 : 500, 0.02f), bold ? theme::textCol : theme::textMuted);
            if (i < 0)
                break;
            bold = ! bold;
            start = i + 2;
        }
        s.setLineSpacing (2.0f);
        return s;
    }
    String text;
    bool boxed;
};

class ColourRow final : public Row
{
public:
    ColourRow (SkinDocument& d, String p, String l, bool linkable = true) : doc (d), path (std::move (p)), label (std::move (l)), canLink (linkable)
    {
        swatch.read = [this] { return doc.colour (path, Colours::magenta); };
        swatch.write = [this] (Colour c) { doc.set (path, hex (c), path + "-pick"); };
        swatch.setTooltip ("Click to pick a colour");
        addAndMakeVisible (swatch);
        if (canLink)
        {
            link.addItem ("OWN COLOUR", 1);
            for (int i = 0; i < SkinDocument::tokenTitles().size(); ++i)
                link.addItem (String (CharPointer_UTF8 ("\xe2\x86\x92 ")) + SkinDocument::tokenTitles()[i], 2 + i);
            link.setTooltip ("Link this colour to the palette: it then follows the palette colour");
            link.onChange = [this]
            {
                const int id = link.getSelectedId();
                if (id == 1 && doc.get (path).toString().startsWithChar ('@'))
                    doc.set (path, hex (doc.colour (path, Colours::magenta)));
                else if (id >= 2)
                    doc.set (path, "@" + SkinDocument::tokenNames()[id - 2]);
            };
            addAndMakeVisible (link);
        }
        refresh();
    }
    void refresh() override
    {
        const auto v = doc.get (path).toString();
        if (canLink)
            link.setSelectedId (v.startsWithChar ('@') ? 2 + SkinDocument::tokenNames().indexOf (v.substring (1)) : 1, dontSendNotification);
        swatch.repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (labelWidth).reduced (0, 3);
        swatch.setBounds (r.removeFromLeft (30));
        r.removeFromLeft (6);
        link.setBounds (r);
    }
    void paint (Graphics& g) override { paintLabel (g, getLocalBounds().withWidth (labelWidth - 6), label); }
private:
    SkinDocument& doc;
    String path, label;
    bool canLink;
    ColourSwatch swatch;
    ComboBox link;
};

class NumberRow final : public Row
{
public:
    NumberRow (SkinDocument& d, String p, String l, double mn, double mx, double st, String u, bool pct, double def)
        : doc (d), path (std::move (p)), label (std::move (l)), unit (std::move (u)), percent (pct), fallback (def)
    {
        slider.setSliderStyle (Slider::LinearHorizontal);
        slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
        slider.setRange (mn, mx, st);
        slider.setColour (Slider::thumbColourId, theme::red);
        slider.setColour (Slider::trackColourId, theme::red.withAlpha (0.7f));
        slider.setColour (Slider::backgroundColourId, Colour (0xff070203));
        slider.setDoubleClickReturnValue (true, def);
        slider.onValueChange = [this] { doc.set (path, slider.getValue(), path + "-drag"); repaint(); };
        addAndMakeVisible (slider);
        refresh();
    }
    void refresh() override
    {
        const auto v = doc.get (path);
        slider.setValue (v.isDouble() || v.isInt() || v.isInt64() ? (double) v : fallback, dontSendNotification);
        repaint();
    }
    void resized() override { slider.setBounds (getLocalBounds().withTrimmedLeft (labelWidth).withTrimmedRight (52).reduced (0, 4)); }
    void paint (Graphics& g) override
    {
        paintLabel (g, getLocalBounds().withWidth (labelWidth - 6), label);
        const double v = slider.getValue();
        const auto text = percent ? String (roundToInt (v * 100.0)) + "%"
                                  : String (v, slider.getInterval() < 0.5 ? (slider.getInterval() < 0.3 ? 2 : 1) : 0) + unit;
        g.setColour (theme::red);
        g.setFont (theme::font (8.5f, 500, 0.04f));
        g.drawText (text, getLocalBounds().removeFromRight (48), Justification::centredRight, false);
    }
private:
    SkinDocument& doc;
    String path, label, unit;
    bool percent;
    double fallback;
    Slider slider;
};

class ChoiceRow final : public Row
{
public:
    ChoiceRow (SkinDocument& d, String p, String l, Array<var> vals, StringArray labels, var def, std::function<void (const var&)> onPick)
        : doc (d), path (std::move (p)), label (std::move (l)), values (std::move (vals)), fallback (std::move (def)), picked (std::move (onPick))
    {
        for (int i = 0; i < values.size(); ++i)
        {
            auto* b = buttons.add (new TextButton (labels[i].toUpperCase()));
            b->setClickingTogglesState (false);
            b->onClick = [this, i]
            {
                const auto v = values[i];
                if (picked)
                    picked (v);
                else
                    doc.set (path, v);
            };
            addAndMakeVisible (b);
        }
        refresh();
    }
    void refresh() override
    {
        auto v = doc.get (path);
        if (v.isVoid())
            v = fallback;
        for (int i = 0; i < values.size(); ++i)
            buttons[i]->setToggleState (values[i].toString() == v.toString(), dontSendNotification);
    }
    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (label.isEmpty() ? 0 : labelWidth).reduced (0, 3);
        const int w = r.getWidth() / jmax (1, buttons.size());
        for (auto* b : buttons)
            b->setBounds (r.removeFromLeft (w).reduced (1, 0));
    }
    void paint (Graphics& g) override { if (label.isNotEmpty()) paintLabel (g, getLocalBounds().withWidth (labelWidth - 6), label); }
private:
    SkinDocument& doc;
    String path, label;
    Array<var> values;
    var fallback;
    std::function<void (const var&)> picked;
    OwnedArray<TextButton> buttons;
};

/** Loads a picture file into the skin; returns its name inside the skin, or empty (with a message). */
String importPicture (SkinDocument& doc, const File& f, const String& folder)
{
    if (f.getSize() > 8 * 1024 * 1024)
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Picture too big", "Please use pictures up to 8 MB.");
        return {};
    }
    MemoryBlock data;
    if (! f.loadFileAsData (data) || ! ImageFileFormat::loadFrom (data.getData(), data.getSize()).isValid())
    {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Can't use this picture", "Emulon X skins use PNG or JPG pictures.");
        return {};
    }
    return doc.addFile (folder, f.getFileName(), data);
}

class PictureRow final : public Row
{
public:
    PictureRow (SkinDocument& d, String p, String l, std::function<void()> rb) : doc (d), path (std::move (p)), label (std::move (l)), rebuild (std::move (rb))
    {
        choose.onClick = [this]
        {
            chooser = std::make_unique<FileChooser> ("Choose a picture", File::getSpecialLocation (File::userPicturesDirectory), "*.png;*.jpg;*.jpeg");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
            {
                if (! fc.getResult().existsAsFile())
                    return;
                const auto name = importPicture (doc, fc.getResult(), "images");
                if (name.isNotEmpty())
                {
                    doc.set (path, name);
                    if (rebuild) later (rebuild);
                }
            });
        };
        remove.onClick = [this] { doc.set (path, ""); if (rebuild) later (rebuild); };
        addAndMakeVisible (choose);
        addAndMakeVisible (remove);
        refresh();
    }
    void refresh() override
    {
        const auto has = doc.get (path).toString().isNotEmpty();
        choose.setButtonText (has ? "REPLACE..." : "CHOOSE...");
        remove.setVisible (has);
        const auto name = doc.get (path).toString();
        if (name != thumbName)
        {
            thumbName = name;
            const auto data = doc.fileData (name);
            thumb = data.getSize() > 0 ? ImageFileFormat::loadFrom (data.getData(), data.getSize()) : Image();
        }
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds().withTrimmedLeft (labelWidth).reduced (0, 3);
        r.removeFromLeft (32);
        choose.setBounds (r.removeFromLeft (92));
        r.removeFromLeft (6);
        remove.setBounds (r.removeFromLeft (76));
    }
    void paint (Graphics& g) override
    {
        paintLabel (g, getLocalBounds().withWidth (labelWidth - 6), label);
        const auto box = Rectangle<float> ((float) labelWidth, 3.0f, 26.0f, (float) getHeight() - 6.0f);
        g.setColour (Colours::black);
        g.fillRoundedRectangle (box, 3.0f);
        if (thumb.isValid())
            g.drawImage (thumb, box.reduced (1.0f), RectanglePlacement::centred);
        else
        {
            g.setColour (theme::textMuted);
            g.setFont (theme::font (6.5f, 500));
            g.drawText ("PNG\nJPG", box.toNearestInt(), Justification::centred, false);
        }
    }
private:
    SkinDocument& doc;
    String path, label, thumbName { "-" };
    std::function<void()> rebuild;
    TextButton choose { "CHOOSE..." }, remove { "REMOVE" };
    std::unique_ptr<FileChooser> chooser;
    Image thumb;
};

/** The overlay texture tiles: none, the built-in metals, your own. */
class TexturePicker final : public Row
{
public:
    TexturePicker (SkinDocument& d, String prefix, std::function<void()> rb) : doc (d), base (std::move (prefix)), rebuild (std::move (rb))
    {
        setMouseCursor (MouseCursor::PointingHandCursor);
    }
    int heightFor (int width) const override
    {
        const int tileH = (int) ((float) (width - 12) / 3.0f / 1.4f);
        return 2 * (tileH + 6);
    }
    void paint (Graphics& g) override
    {
        const auto current = doc.get (base + ".tex").toString();
        for (auto& t : tiles())
        {
            const auto r = t.r.toFloat();
            g.setColour (Colour (0xff0b090c));
            g.fillRoundedRectangle (r, 5.0f);
            const auto img = thumbFor (t.id);
            if (img.isValid())
            {
                Graphics::ScopedSaveState state (g);
                Path clip;
                clip.addRoundedRectangle (r, 5.0f);
                g.reduceClipRegion (clip);
                g.drawImage (img, r, RectanglePlacement::centred | RectanglePlacement::fillDestination);
            }
            else if (t.id == "none")
            {
                g.setColour (Colour (0xff1a171c));
                for (float x = r.getX() - r.getHeight(); x < r.getRight(); x += 12.0f)
                    g.drawLine (x, r.getBottom(), x + r.getHeight(), r.getY(), 6.0f);
            }
            const bool on = t.id == current || (t.id == "none" && current.isEmpty());
            if (t.id == "add")
            {
                Path p;
                p.addRoundedRectangle (r.reduced (0.5f), 5.0f);
                Path dashed;
                const float dashes[] { 4.0f, 3.0f };
                PathStrokeType (1.0f).createDashedStroke (dashed, p, dashes, 2);
                g.setColour (Colour (0xff6a5f66));
                g.fillPath (dashed);
                g.setColour (theme::textMuted);
                g.setFont (theme::font (7.5f, 700, 0.08f));
                g.drawFittedText (t.title, t.r.reduced (4), Justification::centred, 2);
                continue;
            }
            g.setColour (Colours::black.withAlpha (0.7f));
            g.fillRect (r.withTop (r.getBottom() - 14.0f).reduced (1.0f, 0.0f));
            g.setColour (Colours::white);
            g.setFont (theme::font (7.0f, 700, 0.08f));
            g.drawText (t.title, r.withTop (r.getBottom() - 14.0f), Justification::centred, false);
            g.setColour (on ? theme::red : Colours::black);
            g.drawRoundedRectangle (r.reduced (on ? 1.0f : 0.5f), 5.0f, on ? 2.0f : 1.0f);
        }
    }
    void mouseUp (const MouseEvent& e) override
    {
        if (! e.mouseWasClicked())
            return;
        for (auto& t : tiles())
        {
            if (! t.r.contains (e.getPosition()))
                continue;
            if (t.id == "add")
            {
                chooser = std::make_unique<FileChooser> ("Choose a grayscale texture", File::getSpecialLocation (File::userPicturesDirectory), "*.png;*.jpg;*.jpeg");
                chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
                {
                    if (! fc.getResult().existsAsFile())
                        return;
                    const auto name = importPicture (doc, fc.getResult(), "images");
                    if (name.isEmpty())
                        return;
                    doc.set (base + ".custom", name, base);
                    pick ("custom");
                });
                return;
            }
            pick (t.id);
            return;
        }
    }
private:
    struct Tile { String id, title; Rectangle<int> r; };
    std::vector<Tile> tiles() const
    {
        std::vector<Tile> t { { "none", "NONE", {} } };
        for (int i = 0; i < SkinDocument::builtInTextures().size(); ++i)
            t.push_back ({ SkinDocument::builtInTextures()[i], SkinDocument::builtInTextureTitles()[i], {} });
        const bool hasOwn = doc.get (base + ".custom").toString().isNotEmpty();
        if (hasOwn)
            t.push_back ({ "custom", "YOURS", {} });
        t.push_back ({ "add", hasOwn ? "REPLACE YOURS" : "+ ADD YOUR OWN", {} });
        const int w = (getWidth() - 12) / 3, h = (int) ((float) w / 1.4f);
        for (size_t i = 0; i < t.size(); ++i)
            t[i].r = { (int) (i % 3) * (w + 6), (int) (i / 3) * (h + 6), w, h };
        return t;
    }
    Image thumbFor (const String& id) const
    {
        static std::map<String, Image> builtIn;
        if (SkinDocument::builtInTextures().contains (id))
        {
            auto& img = builtIn[id];
            if (! img.isValid())
            {
                const auto data = SkinDocument::builtInTexture (id);
                img = ImageFileFormat::loadFrom (data.getData(), data.getSize()).rescaled (160, 160);
            }
            return img;
        }
        if (id == "custom")
        {
            const auto data = doc.fileData (doc.get (base + ".custom").toString());
            return data.getSize() > 0 ? ImageFileFormat::loadFrom (data.getData(), data.getSize()) : Image();
        }
        return {};
    }
    void pick (const String& id)
    {
        if (doc.get (base + ".strength").isVoid())
            doc.set (base + ".strength", 0.6, base);
        if (doc.get (base + ".blend").isVoid())
            doc.set (base + ".blend", "overlay", base);
        doc.set (base + ".tex", id, base);
        if (rebuild) later (rebuild);
    }
    SkinDocument& doc;
    String base;
    std::function<void()> rebuild;
    std::unique_ptr<FileChooser> chooser;
};

class FontRow final : public Row
{
public:
    explicit FontRow (SkinDocument& d, std::function<void()> rb) : doc (d), rebuild (std::move (rb))
    {
        box.addItem ("SAIRA (EMULON X)", 1);
        box.addItem ("FREDOKA (ROUND AND CHUNKY)", 2);
        box.addItem ("TAHOMA (EARLY-2000S DESKTOP)", 3);
        box.addSectionHeading ("FONTS ON THIS COMPUTER");
        installed = Font::findAllTypefaceNames();
        for (auto n : { "Tahoma", "Saira", "Fredoka" })
            installed.removeString (n, true);
        for (int i = 0; i < installed.size(); ++i)
            box.addItem (installed[i], 100 + i);
        box.setTooltip ("Fonts on this computer only show on computers that have them - use a font file to send it with the skin");
        box.onChange = [this]
        {
            const int id = box.getSelectedId();
            const String name = id == 1 ? "Saira" : id == 2 ? "Fredoka" : id == 3 ? "Tahoma" : installed[id - 100];
            if (name.isNotEmpty())
                doc.set ("font", name);
        };
        addAndMakeVisible (box);
        fileButton.onClick = [this]
        {
            chooser = std::make_unique<FileChooser> ("Choose a font file", File::getSpecialLocation (File::userHomeDirectory), "*.ttf;*.otf");
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
            {
                const auto f = fc.getResult();
                MemoryBlock data;
                if (! f.existsAsFile() || f.getSize() > 16 * 1024 * 1024 || ! f.loadFileAsData (data))
                    return;
                if (Typeface::createSystemTypefaceFor (data.getData(), data.getSize()) == nullptr)
                {
                    AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Can't use this font", "Please choose a TTF or OTF font file.");
                    return;
                }
                doc.set ("fontFile", doc.addFile ("fonts", f.getFileName(), data));
                if (rebuild) later (rebuild);
            });
        };
        removeFile.onClick = [this] { doc.set ("fontFile", ""); if (rebuild) later (rebuild); };
        addAndMakeVisible (fileButton);
        addAndMakeVisible (removeFile);
        refresh();
    }
    int heightFor (int) const override { return 64; }
    void refresh() override
    {
        const auto name = doc.get ("font").toString();
        const int i = installed.indexOf (name, true);
        box.setSelectedId (name.equalsIgnoreCase ("Fredoka") ? 2 : name.equalsIgnoreCase ("Tahoma") ? 3 : (i >= 0 && ! name.equalsIgnoreCase ("Saira")) ? 100 + i : 1,
                           dontSendNotification);
        const auto file = doc.get ("fontFile").toString();
        box.setEnabled (file.isEmpty());
        fileButton.setButtonText (file.isEmpty() ? "USE A FONT FILE..." : "OTHER FONT FILE...");
        removeFile.setVisible (file.isNotEmpty());
        repaint();
    }
    void resized() override
    {
        auto r = getLocalBounds();
        box.setBounds (r.removeFromTop (28).withTrimmedLeft (labelWidth).reduced (0, 3));
        r.removeFromTop (6);
        auto b = r.removeFromTop (26).withTrimmedLeft (labelWidth);
        fileButton.setBounds (b.removeFromLeft (130));
        b.removeFromLeft (6);
        removeFile.setBounds (b.removeFromLeft (80));
    }
    void paint (Graphics& g) override
    {
        paintLabel (g, Rectangle<int> (0, 0, labelWidth - 6, 28), "Typeface");
        const auto file = doc.get ("fontFile").toString();
        paintLabel (g, Rectangle<int> (0, 34, labelWidth - 6, 26), file.isEmpty() ? "Font file" : file.fromLastOccurrenceOf ("/", false, false));
    }
private:
    SkinDocument& doc;
    std::function<void()> rebuild;
    ComboBox box;
    StringArray installed;
    TextButton fileButton { "USE A FONT FILE..." }, removeFile { "REMOVE" };
    std::unique_ptr<FileChooser> chooser;
};

/** Builds the rows for a part. */
struct Builder
{
    SkinDocument& doc;
    std::function<void()> rebuild;
    std::vector<std::unique_ptr<Row>> rows;

    void heading (const String& t, const String& sub = {}) { rows.push_back (std::make_unique<HeadingRow> (t, sub)); }
    void note (const String& t, bool box = false) { rows.push_back (std::make_unique<NoteRow> (t, box)); }
    void colour (const String& path, const String& label) { rows.push_back (std::make_unique<ColourRow> (doc, path, label)); }
    void number (const String& path, const String& label, double mn, double mx, double st, const String& unit, double def)
    {
        rows.push_back (std::make_unique<NumberRow> (doc, path, label, mn, mx, st, unit, false, def));
    }
    void percent (const String& path, const String& label, double def)
    {
        rows.push_back (std::make_unique<NumberRow> (doc, path, label, 0.0, 1.0, 0.01, "", true, def));
    }
    void choice (const String& path, const String& label, Array<var> values, StringArray labels, var def, bool rebuilds,
                 std::function<void (const var&)> extra = {})
    {
        auto& d = doc;
        auto rb = rebuild;
        rows.push_back (std::make_unique<ChoiceRow> (doc, path, label, values, labels, def, [&d, path, rebuilds, rb, extra] (const var& v)
        {
            if (extra)
                extra (v);
            d.set (path, v);
            if (rebuilds && rb)
                later (rb);
        }));
    }
    void toggle (const String& path, const String& label, bool def, const String& onText = "ON", const String& offText = "OFF")
    {
        choice (path, label, { true, false }, { onText, offText }, def, false);
    }
    void picture (const String& path, const String& label) { rows.push_back (std::make_unique<PictureRow> (doc, path, label, rebuild)); }

    void fill (const String& path)
    {
        const auto type = doc.get (path + ".type").toString();
        auto& d = doc;
        choice (path + ".type", "Fill", { "solid", "gradient", "image" }, { "Solid", "Gradient", "Image" }, "solid", true, [&d, path] (const var& v)
        {
            if (v.toString() == "gradient" && (d.get (path + ".b").isVoid() || d.get (path + ".b") == d.get (path + ".a")))
                d.set (path + ".b", hex (d.colour (path + ".a", Colours::grey).darker (0.6f)), path);
            if (v.toString() == "image" && d.get (path + ".fit").isVoid())
                d.set (path + ".fit", "stretch", path);
        });
        if (type == "image")
        {
            picture (path + ".img", "Picture");
            choice (path + ".fit", "Fit", { "stretch", "cover", "tile" }, { "Stretch", "Cover", "Tile" }, "stretch", false);
            colour (path + ".a", "Base colour");
            note ("The base colour shows behind see-through parts of the picture and is used for the readability check.");
        }
        else if (type == "gradient")
        {
            colour (path + ".a", "Top");
            colour (path + ".b", "Bottom");
            number (path + ".angle", "Direction", 0, 360, 5, String (CharPointer_UTF8 ("\xc2\xb0")), 180);
        }
        else
            colour (path + ".a", "Colour");
    }

    void shape (const String& base, double maxRadius, double maxOutline)
    {
        heading ("Shape");
        number (base + ".radius", "Corner size", 0, maxRadius, 0.5, "px", 6);
        percent (base + ".shape", "Roundness", 0.5);
        number (base + ".outline", "Outline", 0, maxOutline, 0.25, "px", 1);
        colour (base + ".outlineColor", "Outline colour");
        note ("Roundness: 0% cuts the corners straight, 50% is a normal round corner, 100% is an extra-smooth squircle.");
    }

    void overlay (const String& base)
    {
        heading ("Overlay texture", "blends over the whole part");
        rows.push_back (std::make_unique<TexturePicker> (doc, base + ".overlay", rebuild));
        const auto tex = doc.get (base + ".overlay.tex").toString();
        if (tex.isNotEmpty() && tex != "none")
        {
            percent (base + ".overlay.strength", "Strength", 0.6);
            choice (base + ".overlay.blend", "Blend", { "overlay", "soft-light" }, { "Overlay", "Soft light" }, "overlay", false);
        }
        note ("**Making your own texture:** use a **grayscale** picture (no colour). It's blended with **Overlay**, so **mid-grey** "
              "leaves the part's colour as it is, lighter areas add shine and darker areas add scratches and shade. Keep most of it "
              "near mid-grey. It's stretched over the whole part, so a square picture of about 512-1024 px works best.", true);
    }
};
} // namespace

//== Colour swatch ==================================================================
namespace
{
class ColourPicker final : public Component, private ChangeListener
{
public:
    ColourPicker (Colour c, std::function<void (Colour)> fn) : onChange (std::move (fn))
    {
        selector.setCurrentColour (c, dontSendNotification);
        selector.setColour (ColourSelector::backgroundColourId, Colour (0xff141217));
        selector.setColour (ColourSelector::labelTextColourId, theme::textMuted);
        selector.addChangeListener (this);
        addAndMakeVisible (selector);
        setSize (300, 320);
    }
    ~ColourPicker() override { selector.removeChangeListener (this); }
    void resized() override { selector.setBounds (getLocalBounds()); }
private:
    void changeListenerCallback (ChangeBroadcaster*) override { if (onChange) onChange (selector.getCurrentColour()); }
    ColourSelector selector { ColourSelector::showColourAtTop | ColourSelector::showSliders | ColourSelector::showColourspace | ColourSelector::editableColour };
    std::function<void (Colour)> onChange;
};
} // namespace

void ColourSwatch::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    const auto c = read ? read() : Colours::magenta;
    g.setColour (c);
    g.fillRoundedRectangle (r, 4.0f);
    g.setColour (isMouseOver() ? Colours::white.withAlpha (0.7f) : Colours::black);
    g.drawRoundedRectangle (r, 4.0f, 1.0f);
}

void ColourSwatch::mouseUp (const MouseEvent& e)
{
    if (! e.mouseWasClicked() || ! read)
        return;
    SafePointer<ColourSwatch> safe (this);
    CallOutBox::launchAsynchronously (std::make_unique<ColourPicker> (read(), [safe] (Colour c)
                                      {
                                          if (safe != nullptr && safe->write)
                                          {
                                              safe->write (c);
                                              safe->repaint();
                                          }
                                      }),
                                      getScreenBounds(), nullptr);
}

//== Inspector ======================================================================
std::vector<std::unique_ptr<Row>> Inspector::rowsFor (const String& part, SkinDocument& doc, std::function<void()> rebuild)
{
    Builder b { doc, std::move (rebuild), {} };
    const auto mu = String (CharPointer_UTF8 ("\xc2\xb0"));
    if (part == "window")
    {
        b.heading ("Fill");
        b.fill ("window.fill");
        b.heading ("Pattern");
        b.choice ("window.texture", "Pattern", { "none", "grid", "noise" }, { "None", "Grid", "Noise" }, "none", true);
        if (doc.get ("window.texture").toString() == "grid" || doc.get ("window.texture").toString() == "noise")
        {
            b.colour ("window.textureColor", "Colour");
            b.percent ("window.textureOpacity", "Strength", 0.12);
        }
        b.overlay ("window");
    }
    else if (part == "frame")
    {
        b.heading ("Frame");
        b.number ("frame.width", "Width", 0, 8, 0.5, "px", 3);
        b.colour ("frame.color", "Colour");
    }
    else if (part == "topbar")
    {
        b.heading ("Bar");
        b.fill ("topbar.fill");
        b.number ("topbar.line", "Bottom line", 0, 4, 0.5, "px", 1);
        b.colour ("topbar.lineColor", "Line colour");
        b.heading ("Logo");
        b.colour ("topbar.logoColor", "Letters");
        b.percent ("topbar.logoGlow", "Glow", 0.8);
        b.picture ("topbar.logo", "Own logo");
        b.note ("Your own logo replaces the Emulon X logo: a wide PNG with a see-through background works best.");
        b.overlay ("topbar");
    }
    else if (part == "panel")
    {
        b.heading ("Fill");
        b.fill ("panel.fill");
        b.shape ("panel", 20, 5);
        b.heading ("Shadow");
        b.number ("panel.shadowY", "Distance", 0, 12, 0.5, "px", 3);
        b.number ("panel.shadowBlur", "Softness", 0, 24, 1, "px", 10);
        b.percent ("panel.shadowOpacity", "Strength", 0.7);
        b.overlay ("panel");
    }
    else if (part == "heading")
    {
        b.heading ("Panel titles");
        b.colour ("heading.color", "Colour");
    }
    else if (part == "inset")
    {
        b.heading ("Fill");
        b.fill ("inset.fill");
        b.percent ("inset.innerShadow", "Depth", 0.6);
        b.shape ("inset", 20, 4);
        b.overlay ("inset");
    }
    else if (part == "display")
    {
        b.heading ("Screen");
        b.fill ("display.fill");
        b.toggle ("display.scanlines", "Scan lines", true);
        b.heading ("Text");
        b.colour ("display.textColor", "Text");
        b.percent ("display.glow", "Glow", 0.55);
        b.shape ("display", 14, 4);
        b.overlay ("display");
    }
    else if (part == "list")
    {
        b.heading ("Lists");
        b.colour ("list.color", "Item text");
    }
    else if (part == "button")
    {
        b.heading ("Face");
        b.fill ("button.fill");
        b.percent ("button.bevel", "Shine", 0.5);
        b.colour ("button.textColor", "Text");
        b.shape ("button", 14, 4);
        b.heading ("Raised edge");
        b.number ("button.shadowY", "Height", 0, 6, 0.5, "px", 0);
        b.number ("button.shadowBlur", "Softness", 0, 12, 1, "px", 0);
        b.percent ("button.shadowOpacity", "Darkness", 0);
        b.overlay ("button");
    }
    else if (part == "buttonOn")
    {
        b.heading ("Face");
        b.fill ("buttonOn.fill");
        b.colour ("buttonOn.textColor", "Text");
        b.percent ("buttonOn.glow", "Glow", 0.45);
        b.shape ("buttonOn", 14, 4);
        b.overlay ("buttonOn");
    }
    else if (part == "tab")
    {
        b.heading ("Tabs");
        auto& d = doc;
        b.choice ("tab.same", "Look", { true, false }, { "Like buttons", "Own look" }, true, true, [&d] (const var& v)
        {
            if (! (bool) v && d.get ("tab.fill").isVoid())
            {
                // start the own look from the buttons
                d.set ("tab.fill", cloneVar (d.get ("button.fill")), "tab-own");
                d.set ("tab.onFill", cloneVar (d.get ("buttonOn.fill")), "tab-own");
                for (auto k : { "textColor", "radius", "shape", "outline", "outlineColor" })
                    if (! d.get (String ("button.") + k).isVoid())
                        d.set (String ("tab.") + k, d.get (String ("button.") + k), "tab-own");
            }
        });
        if (doc.get ("tab.same").isVoid() || (bool) doc.get ("tab.same"))
            b.note ("Tabs copy the buttons, including their picture and overlay texture. Choose Own look to style them separately.");
        else
        {
            b.heading ("Tab");
            b.fill ("tab.fill");
            b.colour ("tab.textColor", "Text");
            b.heading ("Active tab");
            b.fill ("tab.onFill");
            b.shape ("tab", 14, 4);
            b.overlay ("tab");
        }
    }
    else if (part == "knob")
    {
        b.heading ("Style");
        b.choice ("knob.style", "Style", { "cap", "ring", "picture" }, { "Cap", "Ring", "Picture" }, "cap", true);
        if (doc.get ("knob.style").toString() == "picture")
        {
            b.heading ("Picture");
            b.picture ("knob.picture", "Knob picture");
            b.note ("A square PNG that turns with the value, pointing up at the middle position.");
        }
        else
        {
            b.heading ("Cap");
            b.colour ("knob.capA", "Light side");
            b.colour ("knob.capB", "Dark side");
            b.heading ("Ring");
            b.number ("knob.ring", "Thickness", 0, 8, 0.25, "px", 0);
            b.colour ("knob.ringTrack", "Track");
            b.colour ("knob.ringColor", "Value");
            b.heading ("Pointer");
            b.colour ("knob.pointer", "Colour");
            b.percent ("knob.pointerGlow", "Glow", 0.9);
        }
        b.overlay ("knob");
    }
    else if (part == "fader")
    {
        b.heading ("Track");
        b.colour ("fader.track", "Track");
        b.colour ("fader.fill", "Value fill");
        b.heading ("Handle");
        b.colour ("fader.thumbA", "Top");
        b.colour ("fader.thumbB", "Bottom");
        b.picture ("fader.thumbPic", "Handle picture");
        b.overlay ("fader");
    }
    else if (part == "label")
    {
        b.heading ("Captions");
        b.colour ("label.color", "Colour");
    }
    else if (part == "type")
    {
        b.heading ("Font");
        b.rows.push_back (std::make_unique<FontRow> (doc, b.rebuild));
        b.toggle ("upper", "All caps", true);
        b.note ("Emulon X sizes every font to the same capital height, so all labels still fit. A font file travels inside the "
                "skin; fonts on this computer only show where they're installed.");
    }
    juce::ignoreUnused (mu);
    return std::move (b.rows);
}

Inspector::Inspector (SkinDocument& d) : doc (d)
{
    viewport.setViewedComponent (&content, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollBarThickness (8);
    addAndMakeVisible (viewport);
    doc.addChangeListener (this);
    rebuild();
}

Inspector::~Inspector() { doc.removeChangeListener (this); }

void Inspector::show (const String& key)
{
    part = key;
    viewport.setViewPosition (0, 0);
    rebuild();
}

void Inspector::rebuild()
{
    SafePointer<Inspector> safe (this);
    rows = rowsFor (part, doc, [safe] { if (safe != nullptr) safe->rebuild(); });
    for (auto& r : rows)
        content.addAndMakeVisible (*r);
    seenStructure = doc.getStructureRevision();
    layoutRows();
    repaint();
}

void Inspector::changeListenerCallback (ChangeBroadcaster*)
{
    if (doc.getStructureRevision() != seenStructure)
    {
        rebuild();
        return;
    }
    for (auto& r : rows)
        r->refresh();
}

void Inspector::layoutRows()
{
    const int w = jmax (100, viewport.getWidth() - 12);
    int y = 0;
    for (auto& r : rows)
    {
        const int h = r->heightFor (w);
        r->setBounds (0, y, w, h);
        y += h + 4;
    }
    content.setSize (w, y + 8);
}

void Inspector::resized()
{
    viewport.setBounds (getLocalBounds().withTrimmedTop (42));
    layoutRows();
}

void Inspector::paint (Graphics& g)
{
    theme::drawLed (g, getLocalBounds().removeFromTop (30).toFloat(), partTitle (part).toUpperCase(), 10.0f);
}
} // namespace emx::sd
