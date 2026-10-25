// SPDX-License-Identifier: AGPL-3.0-or-later
#include "HubUi.h"
#include "HubAssets.h"
#include "Settings.h"
#include "SkinDocument.h"
#include "PreviewView.h"

namespace emx::hub
{
using namespace juce;
using look::Style;

namespace
{
const String middot = String::fromUTF8 ("\xc2\xb7");
constexpr int innerX = 72, innerY = 71, innerW = 1136, innerB = 649;
constexpr int headH = 50, leftW = 386;
const Rectangle<int> innerArea { innerX, innerY, innerW, innerB - innerY };

Image hubAssetImage (const String& name)
{
    for (int i = 0; i < HubAssets::namedResourceListSize; ++i)
        if (String (HubAssets::getNamedResourceOriginalFilename (HubAssets::namedResourceList[i])) == name)
        {
            int size = 0;
            if (auto* data = HubAssets::getNamedResource (HubAssets::namedResourceList[i], size))
                return ImageCache::getFromMemory (data, size);
        }
    return {};
}

double now() { return Time::getMillisecondCounterHiRes() / 1000.0; }

float ease (float t) { t = jlimit (0.0f, 1.0f, t); return 1.0f - std::pow (1.0f - t, 3.0f); }

enum class Icon { none, close, left, right, bell, gear, folder };

Path iconPath (Icon i, Rectangle<float> r)
{
    Path p;
    const auto c = r.getCentre();
    const float s = jmin (r.getWidth(), r.getHeight());
    switch (i)
    {
        case Icon::close:
            p.addLineSegment ({ c.x - s * 0.28f, c.y - s * 0.28f, c.x + s * 0.28f, c.y + s * 0.28f }, s * 0.11f);
            p.addLineSegment ({ c.x + s * 0.28f, c.y - s * 0.28f, c.x - s * 0.28f, c.y + s * 0.28f }, s * 0.11f);
            break;
        case Icon::left:
        case Icon::right:
        {
            const float d = i == Icon::left ? -1.0f : 1.0f;
            Path a;
            a.startNewSubPath (c.x - d * s * 0.12f, c.y - s * 0.25f);
            a.lineTo (c.x + d * s * 0.14f, c.y);
            a.lineTo (c.x - d * s * 0.12f, c.y + s * 0.25f);
            PathStrokeType (s * 0.1f, PathStrokeType::mitered, PathStrokeType::square).createStrokedPath (p, a);
            break;
        }
        case Icon::bell:
        {
            // the outline bell from the prototype (24 x 24 units)
            Path b;
            b.startNewSubPath (18, 8);
            b.cubicTo (18, 4.7f, 15.3f, 2, 12, 2);
            b.cubicTo (8.7f, 2, 6, 4.7f, 6, 8);
            b.cubicTo (6, 15, 3, 17, 3, 17);
            b.lineTo (21, 17);
            b.cubicTo (21, 17, 18, 15, 18, 8);
            b.startNewSubPath (13.7f, 21);
            b.quadraticTo (12, 22.5f, 10.3f, 21);
            Path stroked;
            PathStrokeType (2.0f, PathStrokeType::curved, PathStrokeType::rounded).createStrokedPath (stroked, b);
            stroked.applyTransform (AffineTransform::scale (s / 24.0f).translated (c.x - s * 0.5f, c.y - s * 0.5f));
            p = stroked;
            break;
        }
        case Icon::gear:
        {
            const float ro = s * 0.42f, ri = s * 0.3f;
            for (int k = 0; k < 16; ++k)
            {
                const float a = MathConstants<float>::twoPi * (float) k / 16.0f;
                const float rr = (k % 2 == 0) ? ro : ri;
                const auto pt = c.getPointOnCircumference (rr, a - 0.1f);
                const auto pt2 = c.getPointOnCircumference (rr, a + 0.1f);
                if (k == 0) p.startNewSubPath (pt); else p.lineTo (pt);
                p.lineTo (pt2);
            }
            p.closeSubPath();
            p.addEllipse (Rectangle<float> (s * 0.28f, s * 0.28f).withCentre (c));
            p.setUsingNonZeroWinding (false);
            break;
        }
        case Icon::folder:
            p.startNewSubPath (r.getX(), r.getY() + s * 0.15f);
            p.lineTo (r.getX() + s * 0.38f, r.getY() + s * 0.15f);
            p.lineTo (r.getX() + s * 0.48f, r.getY() + s * 0.28f);
            p.lineTo (r.getX() + s, r.getY() + s * 0.28f);
            p.lineTo (r.getX() + s, r.getY() + s * 0.85f);
            p.lineTo (r.getX(), r.getY() + s * 0.85f);
            p.closeSubPath();
            break;
        case Icon::none: break;
    }
    return p;
}

class IconButton final : public HubButton
{
public:
    IconButton (Icon i, Style s = Style::grey) : HubButton ({}, s), icon (i) {}
    Icon icon;
    Colour iconColour { 0xffff4a4a };
    bool flat = false;
    void paintButton (Graphics& g, bool hover, bool down) override
    {
        if (! flat)
            HubButton::paintButton (g, hover, down);
        g.setColour (hover ? Colours::white : iconColour);
        g.fillPath (iconPath (icon, getLocalBounds().toFloat().reduced ((float) getHeight() * 0.22f)));
    }
};

/** Any component as a FlowItem with a fixed height. */
struct Wrap final : FlowItem
{
    Wrap (Component* c, int h) : comp (c), height (h) { addAndMakeVisible (c); }
    Component* comp;
    int height;
    int heightFor (int) override { return height; }
    void resized() override { comp->setBounds (getLocalBounds()); }
};

/** A painted strip (headings, notes...) with its own height function. */
struct Painted final : FlowItem
{
    std::function<int (int)> h;
    std::function<void (Graphics&, Rectangle<float>)> draw;
    std::function<void()> onClick;
    int heightFor (int w) override { return h ? h (w) : 0; }
    void paint (Graphics& g) override { if (draw) draw (g, getLocalBounds().toFloat()); }
    void mouseUp (const MouseEvent& e) override { if (onClick && e.mouseWasClicked()) onClick(); }
};

String folderText (const File& f) { return f == File() ? String() : f.getFullPathName(); }
} // namespace

//== Overlays and dialogs ===========================================================
struct HubComponent::Overlay : public Component
{
    std::function<void()> onClose;
};

struct HubComponent::Dialog : public Component
{
    Dialog (const String& h, const String& s = {}) : heading (h), sub (s)
    {
        body.gap = 10;
        addAndMakeVisible (body);
    }
    String heading, sub;
    Column body;
    OwnedArray<HubButton> actions;
    int width = 600;
    bool busy = false;
    std::function<void()> onClose;
    std::vector<std::shared_ptr<void>> keepAlive;   // editors and buttons used by the dialog

    HubButton& action (const String& label, Style s, std::function<void()> f)
    {
        auto* b = actions.add (new HubButton (label, s, s == Style::hot ? 11.5f : 10.0f));
        b->onClick = std::move (f);
        addAndMakeVisible (b);
        return *b;
    }
    TextItem& para (const String& t, bool small = false)
    {
        TextItem::Options o;
        o.font = look::font (small ? 10.6f : 12.5f);
        o.colour = small ? look::muted : look::body;
        o.paragraphs = true;
        return body.add (std::make_unique<TextItem> (t, o));
    }
    int preferredHeight() { return 22 + 22 + 14 + body.heightFor (width - 44) + 16 + 38 + 20; }
    void paint (Graphics& g) override
    {
        look::metal (g, getLocalBounds().toFloat().reduced (1.0f), 10.0f);
        g.setColour (look::text);
        g.setFont (look::font (14.0f, 700, 0.16f));
        const auto hb = Rectangle<int> (22, 22, width - 44, 22);
        g.drawText (heading, hb, Justification::centredLeft, true);
        if (sub.isNotEmpty())
        {
            GlyphArrangement ga;
            ga.addLineOfText (look::font (14.0f, 700, 0.16f), heading, 0, 0);
            const int x = 22 + roundToInt (ga.getBoundingBox (0, -1, true).getWidth()) + 10;
            g.setColour (look::muted);
            g.setFont (look::font (10.5f, 500, 0.08f));
            g.drawText (sub, x, 22, width - x - 22, 22, Justification::centredLeft, true);
        }
    }
    void resized() override
    {
        body.setBounds (22, 58, getWidth() - 44, body.heightFor (getWidth() - 44));
        int x = getWidth() - 22;
        for (int i = actions.size(); --i >= 0;)
        {
            auto* b = actions[i];
            const int w = jmax (b->idealWidth() + 18, b->style == Style::hot ? 150 : 90);
            b->setBounds (x - w, getHeight() - 20 - 38, w, 38);
            x -= w + 10;
        }
    }
};

namespace
{
/** "WHERE SHOULD IT GO?" row: label, path box, BROWSE..., note. */
struct FolderRow final : FlowItem
{
    FolderRow (const String& l, const String& n, const File& f) : label (l), note (n)
    {
        styleEditor (path, look::font (12.0f), true);
        path.setText (folderText (f), false);
        addAndMakeVisible (path);
        addAndMakeVisible (browse);
    }
    String label, note;
    TextEditor path;
    HubButton browse { "BROWSE...", Style::grey, 9.5f };
    int noteHeight (int w) const
    {
        if (note.isEmpty()) return 0;
        AttributedString s;
        s.append (note, look::font (10.4f), look::muted);
        TextLayout tl;
        tl.createLayout (s, (float) w);
        return roundToInt (tl.getHeight()) + 4;
    }
    int heightFor (int w) override { return 16 + 4 + 32 + noteHeight (w); }
    void paint (Graphics& g) override
    {
        g.setColour (look::muted);
        g.setFont (look::font (10.4f, 600, 0.12f));
        g.drawText (label.toUpperCase(), 0, 0, getWidth(), 16, Justification::centredLeft);
        if (note.isNotEmpty())
        {
            AttributedString s;
            s.append (note, look::font (10.4f), look::muted);
            TextLayout tl;
            tl.createLayout (s, (float) getWidth());
            tl.draw (g, { 0.0f, 56.0f, (float) getWidth(), tl.getHeight() });
        }
    }
    void resized() override
    {
        browse.setBounds (getWidth() - 96, 20, 96, 32);
        path.setBounds (0, 20, getWidth() - 104, 32);
    }
};

/** A setting with an ON / OFF switch on the right. */
struct SwitchRow final : FlowItem
{
    SwitchRow (const String& t, const String& n, bool on) : title (t), note (n)
    {
        sw.setClickingTogglesState (true);
        sw.setToggleState (on, dontSendNotification);
        sw.onClick = [this] { sw.setButtonText (sw.getToggleState() ? "ON" : "OFF"); if (onChange) onChange (sw.getToggleState()); };
        sw.setButtonText (on ? "ON" : "OFF");
        addAndMakeVisible (sw);
    }
    String title, note;
    HubButton sw { {}, Style::grey, 10.0f };
    std::function<void (bool)> onChange;
    int heightFor (int) override { return note.isEmpty() ? 30 : 44; }
    void paint (Graphics& g) override
    {
        g.setColour (look::text);
        g.setFont (look::font (12.5f, 600, 0.03f));
        g.drawText (title, 0, 0, getWidth() - 90, 22, Justification::centredLeft, true);
        g.setColour (look::muted);
        g.setFont (look::font (10.4f));
        g.drawText (note, 0, 22, getWidth() - 90, 18, Justification::centredLeft, true);
    }
    void resized() override { sw.setBounds (getWidth() - 74, 2, 74, 26); }
};

/** The download / install box at the bottom of the left column. */
struct InstallBox final : Component
{
    InstallBox() { addAndMakeVisible (big); }
    String status, folder;
    Colour dotColour { 0xff555555 };
    HubButton big { {}, Style::hot, 12.5f };
    OwnedArray<HubButton> links;
    void addLink (const String& t, std::function<void()> f)
    {
        auto* b = links.add (new HubButton (t, Style::grey, 10.8f));
        b->link = true;
        b->onClick = std::move (f);
        addAndMakeVisible (b);
    }
    TextLayout statusLayout (int w) const
    {
        AttributedString s;
        s.append (status, look::font (11.6f), look::muted);
        TextLayout tl;
        tl.createLayout (s, (float) w - 24 - 18);
        return tl;
    }
    /** The links wrap onto more lines when they don't fit. */
    int linkRows (int w) const
    {
        int rows = links.isEmpty() ? 0 : 1, x = 12;
        for (auto* l : links)
        {
            const int lw = l->idealWidth() + 4;
            if (x > 12 && x + lw > w - 12) { ++rows; x = 12; }
            x += lw + 14;
        }
        return rows;
    }
    int preferredHeight (int w) const
    {
        return 12 + jmax (16, roundToInt (statusLayout (w).getHeight())) + 8 + (folder.isNotEmpty() ? 22 : 0) + 40 + linkRows (w) * 24 + (links.isEmpty() ? 0 : 2) + 12;
    }
    void paint (Graphics& g) override
    {
        look::metal (g, getLocalBounds().toFloat(), 9.0f);
        const auto tl = statusLayout (getWidth());
        look::dot (g, { 18.0f, 20.0f }, dotColour);
        tl.draw (g, { 30.0f, 12.0f, (float) getWidth() - 42.0f, tl.getHeight() });
        if (folder.isNotEmpty())
        {
            const float y = 12.0f + jmax (16.0f, tl.getHeight()) + 8.0f;
            g.setColour (Colour (0xffe0b050));
            g.fillPath (iconPath (Icon::folder, { 14.0f, y + 2.0f, 12.0f, 12.0f }));
            g.setColour (look::muted);
            g.setFont (look::font (10.4f));
            g.drawText (folder, 32, (int) y, getWidth() - 46, 16, Justification::centredLeft, true);
        }
    }
    void resized() override
    {
        const auto tl = statusLayout (getWidth());
        int y = 12 + jmax (16, roundToInt (tl.getHeight())) + 8 + (folder.isNotEmpty() ? 22 : 0);
        big.setBounds (12, y, getWidth() - 24, 40);
        y += 48;
        int x = 12;
        for (auto* l : links)
        {
            const int w = l->idealWidth() + 4;
            if (x > 12 && x + w > getWidth() - 12) { x = 12; y += 24; }
            l->setBounds (x, y, w, 20);
            x += w + 14;
        }
    }
};

/** Screenshots: a big picture with arrows, its caption, and thumbnails. */
struct ShotViewer final : Component
{
    ShotViewer()
    {
        prev.onClick = [this] { if (onShow) onShow (index - 1); };
        next.onClick = [this] { if (onShow) onShow (index + 1); };
        addAndMakeVisible (prev);
        addAndMakeVisible (next);
        thumbsView.setViewedComponent (&thumbs, false);
        thumbsView.setScrollBarsShown (false, true, false, true);
        thumbsView.setScrollBarThickness (6);
        addAndMakeVisible (thumbsView);
    }
    struct Thumbs final : Component
    {
        ShotViewer* owner = nullptr;
        void paint (Graphics& g) override
        {
            for (int i = 0; i < owner->count; ++i)
            {
                const auto r = Rectangle<float> ((float) i * 130.0f + 1.0f, 2.0f, 123.0f, 77.0f);
                const auto img = owner->getImage ? owner->getImage (i) : Image();
                g.setColour (Colours::black);
                g.fillRoundedRectangle (r, 5.0f);
                {
                    Graphics::ScopedSaveState s (g);
                    Path clip;
                    clip.addRoundedRectangle (r, 5.0f);
                    g.reduceClipRegion (clip);
                    g.setOpacity (i == owner->index ? 1.0f : (hover == i ? 0.9f : 0.6f));
                    if (img.isValid())
                        g.drawImage (img, r, RectanglePlacement::fillDestination);
                }
                g.setOpacity (1.0f);
                if (i == owner->index)
                {
                    g.setColour (look::red);
                    g.drawRoundedRectangle (r.expanded (1.5f), 6.0f, 2.0f);
                }
                else
                {
                    g.setColour (Colours::black);
                    g.drawRoundedRectangle (r, 5.0f, 1.0f);
                }
            }
        }
        int at (Point<int> p) const { const int i = p.x / 130; return (p.x % 130) < 124 && i < owner->count ? i : -1; }
        void mouseMove (const MouseEvent& e) override { const int h = at (e.getPosition()); if (h != hover) { hover = h; repaint(); } }
        void mouseExit (const MouseEvent&) override { hover = -1; repaint(); }
        void mouseUp (const MouseEvent& e) override { const int i = at (e.getPosition()); if (i >= 0 && owner->onShow) owner->onShow (i); }
        int hover = -1;
    };
    int index = 0, count = 0;
    std::function<Image (int)> getImage;
    std::function<void (int)> onShow;
    std::unique_ptr<TextItem> caption;
    std::unique_ptr<HubButton> removeBtn, addBtn;
    IconButton prev { Icon::left, Style::dark }, next { Icon::right, Style::dark };
    Viewport thumbsView;
    Thumbs thumbs;
    Rectangle<int> screenArea, captionArea;

    void paint (Graphics& g) override
    {
        auto viewer = getLocalBounds().withHeight (screenArea.getBottom() + 8).toFloat();
        look::metal (g, viewer, 9.0f);
        g.setColour (Colours::black);
        g.fillRoundedRectangle (screenArea.toFloat(), 5.0f);
        if (count == 0)
        {
            g.setColour (look::muted);
            g.setFont (look::font (13.0f));
            g.drawText ("No screenshots yet", screenArea, Justification::centred);
        }
        else if (auto img = getImage ? getImage (index) : Image(); img.isValid())
        {
            Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (screenArea);
            g.setOpacity (1.0f);
            g.setImageResamplingQuality (Graphics::highResamplingQuality);
            g.drawImage (img, screenArea.toFloat(), RectanglePlacement::centred);
        }
        else
        {
            g.setColour (look::muted.withAlpha (0.6f));
            g.setFont (look::font (11.0f, 600, 0.2f));
            g.drawText ("LOADING...", screenArea, Justification::centred);
        }
        look::led (g, captionArea.toFloat());
        if (count > 0)
        {
            g.setColour (look::red);
            g.setFont (look::font (11.0f, 500, 0.06f));
            g.drawText (String (index + 1) + " / " + String (count), captionArea.reduced (12, 0), Justification::centredRight);
        }
    }
    void resized() override
    {
        const int thumbsH = 86, capH = 34, gap = 10;
        const int viewerH = getHeight() - thumbsH - capH - gap * 2;
        screenArea = Rectangle<int> (0, 0, getWidth(), viewerH).reduced (8);
        captionArea = { 0, viewerH + gap, getWidth(), capH };
        prev.setBounds (screenArea.getX() + 13, screenArea.getCentreY() - 24, 34, 47);
        next.setBounds (screenArea.getRight() - 47, screenArea.getCentreY() - 24, 34, 47);
        prev.setVisible (count > 1);
        next.setVisible (count > 1);
        auto cap = captionArea.reduced (12, 0).withTrimmedRight (60);
        if (removeBtn != nullptr)
            removeBtn->setBounds (cap.removeFromRight (80).reduced (0, 6));
        if (caption != nullptr)
            caption->setBounds (cap.withSizeKeepingCentre (cap.getWidth(), jmin (cap.getHeight() - 4, caption->heightFor (cap.getWidth()) + 2)));
        auto strip = Rectangle<int> (0, captionArea.getBottom() + gap, getWidth(), thumbsH);
        thumbs.owner = this;
        const int addW = addBtn != nullptr ? 130 : 0;
        thumbs.setSize (jmax (strip.getWidth() - addW, count * 130), 82);
        if (addBtn != nullptr)
        {
            thumbsView.setBounds (strip.withTrimmedRight (addW));
            addBtn->setBounds (strip.getRight() - 123, strip.getY() + 2, 123, 77);
        }
        else
            thumbsView.setBounds (strip);
    }
};

/** The grids of skins and banks. */
struct CardGrid final : Component
{
    struct Card
    {
        String id, name, tag;
        Colour tagColour = look::muted;
        std::function<void (Graphics&, Rectangle<float>)> pic;
        bool add = false;
    };
    struct Group { String title; std::vector<Card> cards; };
    std::vector<Group> groups;
    String selected, note;
    std::function<void (const String&)> onPick;
    std::function<void()> onAdd;
    int hover = -1;

    struct Placed { Rectangle<float> r; const Card* card; String title; };
    std::vector<Placed> place (int width) const
    {
        std::vector<Placed> out;
        float y = 0;
        const float gap = 10.0f, cw = ((float) width - gap * 2) / 3.0f, ch = (cw - 12.0f) * 10.0f / 16.0f + 12.0f + 26.0f;
        for (auto& gr : groups)
        {
            if (gr.cards.empty())
                continue;
            out.push_back ({ { 0, y, (float) width, 16 }, nullptr, gr.title });
            y += 16 + 10;
            for (size_t i = 0; i < gr.cards.size(); ++i)
            {
                const int col = (int) i % 3;
                if (col == 0 && i > 0) y += ch + gap;
                out.push_back ({ { (float) col * (cw + gap), y, cw, ch }, &gr.cards[i], {} });
            }
            y += ch + 14;
        }
        if (note.isNotEmpty())
            out.push_back ({ { 0, y, (float) width, 40 }, nullptr, "\n" + note });
        return out;
    }
    int heightFor (int width) const
    {
        const auto p = place (width);
        return p.empty() ? 0 : roundToInt (p.back().r.getBottom()) + 4;
    }
    void paint (Graphics& g) override
    {
        const auto placed = place (getWidth());
        for (int i = 0; i < (int) placed.size(); ++i)
        {
            const auto& p = placed[(size_t) i];
            if (p.card == nullptr)
            {
                if (p.title.startsWith ("\n"))
                {
                    AttributedString s;
                    s.append (p.title.substring (1), look::font (10.4f), look::muted);
                    s.setLineSpacing (3.0f);
                    TextLayout tl;
                    tl.createLayout (s, p.r.getWidth());
                    tl.draw (g, p.r);
                    continue;
                }
                g.setColour (look::muted);
                g.setFont (look::font (11.0f, 700, 0.18f));
                g.drawText (p.title.toUpperCase(), p.r, Justification::centredLeft);
                GlyphArrangement ga;
                ga.addLineOfText (look::font (11.0f, 700, 0.18f), p.title.toUpperCase(), 0, 0);
                const float x = p.r.getX() + ga.getBoundingBox (0, -1, true).getWidth() + 10.0f;
                g.setGradientFill (ColourGradient::horizontal (look::red.withAlpha (0.5f), x, Colours::transparentBlack, p.r.getRight()));
                g.fillRect (x, p.r.getCentreY(), p.r.getRight() - x, 1.0f);
                continue;
            }
            const auto& c = *p.card;
            auto r = p.r;
            const bool on = c.id == selected && ! c.add;
            if (on)
            {
                Path path;
                path.addRoundedRectangle (r, 8.0f);
                DropShadow (look::red.withAlpha (0.45f), 13, {}).drawForPath (g, path);
            }
            g.setGradientFill (ColourGradient::vertical (Colour (38, 35, 42).withAlpha (0.9f), r.getY(), Colour (12, 11, 14).withAlpha (0.95f), r.getBottom()));
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (Colours::white.withAlpha (0.1f));
            g.drawHorizontalLine ((int) r.getY() + 1, r.getX() + 8, r.getRight() - 8);
            g.setColour (on ? look::red : (hover == i ? look::red.withAlpha (0.4f) : Colours::black));
            g.drawRoundedRectangle (r.reduced (on ? 1.0f : 0.5f), 8.0f, on ? 2.0f : 1.0f);
            auto inner = r.reduced (6.0f);
            const auto pic = inner.removeFromTop ((inner.getWidth()) * 10.0f / 16.0f);
            if (c.add)
            {
                g.setColour (Colour (0xff0b0809));
                g.fillRoundedRectangle (pic, 5.0f);
                drawDashed (g, pic, Colour (0xff8a5050));
                g.setColour (look::muted);
                g.setFont (look::font (11.5f, 700, 0.12f));
                g.drawText (c.name, pic, Justification::centred);
                continue;
            }
            {
                Graphics::ScopedSaveState s (g);
                Path clip;
                clip.addRoundedRectangle (pic, 5.0f);
                g.reduceClipRegion (clip);
                g.setColour (Colours::black);
                g.fillRect (pic);
                if (c.pic) c.pic (g, pic);
            }
            g.setColour (Colours::black);
            g.drawRoundedRectangle (pic, 5.0f, 1.0f);
            auto row = inner.withTrimmedTop (6.0f).reduced (3.0f, 0).withHeight (20.0f);
            if (c.tag.isNotEmpty())
            {
                g.setFont (look::font (8.8f, 700, 0.08f));
                GlyphArrangement ga;
                ga.addLineOfText (look::font (8.8f, 700, 0.08f), c.tag, 0, 0);
                const auto tr = row.removeFromRight (ga.getBoundingBox (0, -1, true).getWidth() + 14.0f).withSizeKeepingCentre (ga.getBoundingBox (0, -1, true).getWidth() + 14.0f, 17.0f);
                g.setColour (Colour (0xff0b0809));
                g.fillRoundedRectangle (tr, 8.5f);
                g.setColour (Colours::black);
                g.drawRoundedRectangle (tr, 8.5f, 1.0f);
                g.setColour (c.tagColour);
                g.drawText (c.tag, tr, Justification::centred);
            }
            g.setColour (look::text);
            g.setFont (look::font (11.7f, 700, 0.07f));
            g.drawText (c.name, row.withTrimmedRight (6), Justification::centredLeft, true);
        }
    }
    int at (Point<int> pos) const
    {
        const auto placed = place (getWidth());
        for (int i = 0; i < (int) placed.size(); ++i)
            if (placed[(size_t) i].card != nullptr && placed[(size_t) i].r.contains (pos.toFloat()))
                return i;
        return -1;
    }
    void mouseMove (const MouseEvent& e) override
    {
        const int h = at (e.getPosition());
        setMouseCursor (h >= 0 ? MouseCursor::PointingHandCursor : MouseCursor::NormalCursor);
        if (h != hover) { hover = h; repaint(); }
    }
    void mouseExit (const MouseEvent&) override { hover = -1; repaint(); }
    void mouseUp (const MouseEvent& e) override
    {
        const int i = at (e.getPosition());
        if (i < 0) return;
        const auto placed = place (getWidth());
        const auto* c = placed[(size_t) i].card;
        if (c->add) { if (onAdd) onAdd(); }
        else if (onPick) onPick (c->id);
    }
};

/** The right column for skins and banks: a heading row (submit button) and the scrolling grid. */
struct GridPanel final : Component
{
    GridPanel()
    {
        view.setViewedComponent (&grid, false);
        view.setScrollBarsShown (true, false);
        view.setScrollBarThickness (7);
        addAndMakeVisible (view);
    }
    CardGrid grid;
    Viewport view;
    std::unique_ptr<HubButton> submit;
    void resized() override
    {
        auto r = getLocalBounds();
        if (submit != nullptr)
            submit->setBounds (r.getRight() - 200, 0, 200, 28);
        view.setBounds (r);
        grid.setSize (r.getWidth() - 10, grid.heightFor (r.getWidth() - 10));
    }
};

/** The red bar at the top in dev mode. */
struct DevBar final : Component
{
    String who;
    int files = 0, pictures = 0;
    HubButton discard { "DISCARD", Style::grey, 9.5f }, save { "SAVE TO GITHUB", Style::hot, 9.5f }, logout { "LOG OUT", Style::grey, 9.5f };
    DevBar()
    {
        for (auto* b : { &discard, &save, &logout })
        {
            b->glow = false;
            addAndMakeVisible (b);
        }
    }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();
        Path p;
        p.addRoundedRectangle (r, 7.0f);
        DropShadow (look::red.withAlpha (0.35f), 13, {}).drawForPath (g, p);
        g.setGradientFill (ColourGradient::vertical (Colour (0xff3a0808), r.getY(), Colour (0xff1a0303), r.getBottom()));
        g.fillPath (p);
        g.setColour (Colour (0x4dff7878));
        g.drawHorizontalLine (1, 8, r.getRight() - 8);
        g.setColour (Colours::black);
        g.strokePath (p, PathStrokeType (1.0f));
        look::dot (g, { 18.0f, r.getCentreY() }, look::red);
        g.setColour (look::text);
        g.setFont (look::font (11.5f, 700, 0.16f));
        g.drawText ("DEV MODE", 30, 0, 90, getHeight(), Justification::centredLeft);
        g.setColour (look::muted);
        g.setFont (look::font (11.0f));
        g.drawText ("signed in as " + who, 120, 0, 220, getHeight(), Justification::centredLeft, true);
        g.setColour (Colours::white.withAlpha (0.5f));
        g.setFont (look::font (10.2f));
        g.drawText ("Click any text with a dashed outline to edit it.", 330, 0, 300, getHeight(), Justification::centredLeft, true);
        const int n = files + pictures;
        g.setColour (n > 0 ? Colour (0xffffd0a0) : look::muted);
        String t = n == 0 ? String ("No changes") : String (files) + " file" + (files == 1 ? "" : "s");
        if (pictures > 0) t << " + " << pictures << " picture" << (pictures == 1 ? "" : "s");
        if (n > 0) t << " changed";
        g.drawText (t, getWidth() - 340 - 170, 0, 170, getHeight(), Justification::centredRight, true);
    }
    void resized() override
    {
        int x = getWidth() - 10;
        for (auto* b : { &logout, &save, &discard })
        {
            const int w = b->idealWidth() + 6;
            b->setBounds (x - w, getHeight() / 2 - 12, w, 24);
            x -= w + 8;
        }
    }
};
} // namespace

namespace
{
TextItem::Options opts (float px, int weight, Colour c, bool upper = false, float kern = 0.0f)
{
    TextItem::Options o;
    o.font = look::font (px, weight, kern);
    o.colour = c;
    o.upper = upper;
    return o;
}

/** Text boxes inside a Column can be found again after a rebuild (they're nested in rows and bullets). */
Component* findById (Component* root, const String& id)
{
    if (root == nullptr) return nullptr;
    if (root->getComponentID() == id) return root;
    for (int i = 0; i < root->getNumChildComponents(); ++i)
        if (auto* c = findById (root->getChildComponent (i), id))
            return c;
    return nullptr;
}
} // namespace

//== The Hub ========================================================================
HubComponent::HubComponent (Model& m) : model (m)
{
    setOpaque (true);
    kiruLogo = hubAssetImage ("kiru-logo.png");
    backgroundImage = theme::image (theme::Img::background);
    for (auto [label, t] : { std::pair { "EMULON X", Tab::emx }, { "EMULON X SD", Tab::sd }, { "EMULON SKINS", Tab::skins }, { "EMULON BANKS", Tab::banks } })
    {
        auto* b = tabs.add (new HubButton (label, Style::grey, 12.3f));
        b->onClick = [this, t = t] { showTab (t); };
        addAndMakeVisible (b);
    }
    bell = std::make_unique<IconButton> (Icon::bell);
    gear = std::make_unique<IconButton> (Icon::gear);
    static_cast<IconButton*> (gear.get())->iconColour = Colour (0xffb9a7a7);
    bell->onClick = [this] { toggleMenu(); };
    gear->onClick = [this] { openSettings(); };
    addAndMakeVisible (*bell);
    addAndMakeVisible (*gear);
    for (auto* b : { &replay, &devBtn })
    {
        b->plain = true;
        addAndMakeVisible (b);
    }
    replay.onClick = [this] { startIntro (false); };
    devBtn.onClick = [this] { devLogin(); };
    leftView.setScrollBarsShown (true, false);
    leftView.setScrollBarThickness (7);
    addAndMakeVisible (leftView);

    struct FrameLayer final : Component
    {
        std::function<void (Graphics&)> draw;
        void paint (Graphics& g) override { if (draw) draw (g); }
    };
    auto frame = std::make_unique<FrameLayer>();
    frame->setInterceptsMouseClicks (false, false);
    frame->draw = [this] (Graphics& g) { paintFrameLayer (g); };
    addAndMakeVisible (*frame);
    frameLayer = std::move (frame);

    const auto token = model.settings.get ("githubToken");
    if (token.isNotEmpty())
        Net::get().setToken (token);

    model.addChangeListener (this);
    model.images.addChangeListener (this);
    rebuild();
    startTimerHz (30);
}

HubComponent::~HubComponent()
{
    model.removeChangeListener (this);
    model.images.removeChangeListener (this);
    leftView.setViewedComponent (nullptr, false);
}

String HubComponent::debugState() const
{
    return "tab=" + String ((int) tab) + " dev=" + String ((int) dev) + " unread=" + String (model.unreadCount());
}

std::unique_ptr<TextItem> HubComponent::text (const String& t, TextItem::Options o, std::function<void (const String&)> edit, const String& id)
{
    auto item = std::make_unique<TextItem> (t, o);
    if (dev && edit)
    {
        item->makeEditable ([this, edit] (const String& v) { edit (v); edited(); });
        item->setComponentID (id);
    }
    return item;
}

void HubComponent::edited()
{
    model.sendChangeMessage();
}

//== Intro ==========================================================================
void HubComponent::startIntro (bool skip)
{
    closeOverlays();
    if (skip)
    {
        introStage = 0;
        rebuild();
        return;
    }
    introStage = 1;
    introStart = stageStart = now();
    skipStage = false;
    loadText = "CHECKING FOR UPDATES...";
    if (! loaded)
        refreshNow (false);
    for (int i = 0; i < getNumChildComponents(); ++i)
        if (getChildComponent (i) != frameLayer.get())
            getChildComponent (i)->setVisible (false);
    repaint();
}

void HubComponent::refreshNow (bool fromTimer)
{
    const int unreadBefore = model.unreadCount();
    const auto seenBefore = model.settings.list ("seenNotes");
    lastRefresh = now();
    SafePointer<HubComponent> safe (this);
    model.refresh ([safe, fromTimer, unreadBefore, seenBefore]
    {
        if (safe == nullptr)
            return;
        auto& self = *safe;
        if (Net::get().tokenRejected.exchange (false))
        {
            // GitHub no longer accepts the saved sign-in: forget it (sign in again for dev mode or submitting)
            self.model.settings.set ("githubToken", "");
            self.model.settings.set ("githubLogin", "");
            self.flash ("Your GitHub sign-in stopped working, so you're signed out now. Sign in again for dev mode.");
        }
        if (Net::get().tokenRejected.exchange (false))
        {
            // GitHub no longer accepts the saved sign-in: forget it (sign in again for dev mode or submitting)
            self.model.settings.set ("githubToken", "");
            self.model.settings.set ("githubLogin", "");
            if (self.dev)
                self.setDevMode (false, {});
            self.flash ("Your GitHub sign-in stopped working, so you're signed out. Sign in again for dev mode.");
        }
        // first start: the skins that are already there aren't news
        if (! self.model.settings.flag ("knewSkins", false))
        {
            StringArray known;
            for (auto& s : self.model.skins)
                if (s.community) known.add (s.id);
            self.model.settings.setList ("knownSkins", known);
            self.model.settings.set ("knewSkins", true);
        }
        self.loaded = true;
        if (self.checkStatus == "Checking...")
        {
            const auto have = self.model.installedVersion (self.tab == Tab::sd ? "sd" : "emx");
            const auto& rel = self.tab == Tab::sd ? self.model.relSd : self.model.relEmx;
            self.checkStatus = ! self.model.releasesLive ? "Couldn't reach GitHub: " + self.model.releasesError
                             : ! rel.found ? String ("Not released yet")
                             : isNewer (rel.version, have) ? "Update found: " + rel.version : "Up to date (" + rel.version + ")";
            Timer::callAfterDelay (2500, [safe] { if (safe != nullptr) { safe->checkStatus = {}; safe->rebuild(); } });
        }
        self.rebuild();
        if (fromTimer)
            for (auto& n : self.model.notes())
                if (! seenBefore.contains (n.id) && self.model.settings.flag ("notify", true))
                {
                    if (self.isShowing())
                        self.toast (n);
                    else if (self.onSystemNotification)
                        self.onSystemNotification (n.title, n.text);
                    break;
                }
        ignoreUnused (unreadBefore);
    });
}

void HubComponent::timerCallback()
{
    const double t = now();
    if (introStage == 1)
    {
        if (skipStage || t - stageStart > 3.1)
        {
            introStage = 2;
            stageStart = t;
            skipStage = false;
        }
        repaint();
    }
    else if (introStage == 2)
    {
        const double e = t - stageStart;
        loadText = e < 0.62 ? "CHECKING FOR UPDATES..." : e < 1.24 ? "LOADING SKINS..." : e < 1.86 ? "LOADING BANKS..." : "READY";
        if ((skipStage || e > 2.5) && loaded)
        {
            introStage = 3;
            stageStart = t;
            for (int i = 0; i < getNumChildComponents(); ++i)
                getChildComponent (i)->setVisible (true);
            rebuild();
        }
        repaint();
    }
    else if (introStage == 3)
    {
        if (t - stageStart > 0.6)
        {
            introStage = 0;
            SafePointer<HubComponent> safe (this);
            Timer::callAfterDelay (1600, [safe]
            {
                if (safe == nullptr || safe->introStage != 0) return;
                const auto seen = safe->model.settings.list ("seenNotes");
                for (auto& n : safe->model.notes())
                    if (! seen.contains (n.id)) { safe->toast (n); break; }
            });
        }
        repaint();
    }
    if (toastBox != nullptr && toastUntil > 0 && t > toastUntil)
    {
        dispose (toastBox);
        toastUntil = 0;
    }
    if (flashUntil > 0)
    {
        if (t > flashUntil) flashUntil = 0;
        repaint (Rectangle<int> (W / 2 - 300, 120, 600, 50));
    }
    // look for news every 30 minutes
    if (loaded && t - lastRefresh > 30 * 60 && ! model.job().active && model.changedFiles().isEmpty())
        refreshNow (true);
}

void HubComponent::mouseUp (const MouseEvent&)
{
    if (introStage == 1 || introStage == 2)
        skipStage = true;
}

//== Painting =======================================================================
void HubComponent::paint (Graphics& g)
{
    g.fillAll (Colour (0xff0a0606));
    g.setImageResamplingQuality (Graphics::highResamplingQuality);
    g.setOpacity (1.0f);
    g.drawImage (backgroundImage, getLocalBounds().toFloat(), RectanglePlacement::fillDestination);
    if (introStage == 1 || introStage == 2)
        return;
    // header logo and lines
    theme::drawLogo (g, Rectangle<float> ((float) innerX + 8, (float) innerY + 0, 150, 34), RectanglePlacement::xLeft | RectanglePlacement::yMid);
    g.setColour (Colours::black);
    g.fillRect (innerX, innerY + headH - 3, innerW, 3);
    g.setColour (Colours::white.withAlpha (0.06f));
    g.fillRect (innerX, innerY + headH, innerW, 1);
    g.setColour (Colours::black);
    g.fillRect (innerX + leftW - 3, innerY + headH + 1, 3, innerB - innerY - headH - 1);
    g.setColour (Colours::white.withAlpha (0.05f));
    g.fillRect (innerX + leftW, innerY + headH + 1, 1, innerB - innerY - headH - 1);
}

void HubComponent::paintFrameLayer (Graphics& g)
{
    // the frame from the plugin, over the pages
    g.setOpacity (1.0f);
    g.drawImage (theme::image (theme::Img::frame), getLocalBounds().toFloat(), RectanglePlacement::stretchToFit);
    if (introStage == 1 || introStage == 2)
        return;

    // the bell's badge
    if (introStage == 0 || introStage == 3)
        if (const int unread = model.unreadCount(); unread > 0 && bell->isVisible())
        {
            const auto b = bell->getBounds().toFloat();
            const auto r = Rectangle<float> (19, 19).withCentre ({ b.getRight() - 1, b.getY() + 1 });
            Path p;
            p.addEllipse (r);
            DropShadow (look::red.withAlpha (0.8f), 8, {}).drawForPath (g, p);
            g.setColour (look::red);
            g.fillEllipse (r);
            g.setColour (Colours::white);
            g.setFont (look::font (9.5f, 900));
            g.drawText (String (unread), r, Justification::centred);
        }

    // footer: where the texts came from
    const bool live = model.textsLive;
    const auto src = live ? String ("TEXTS: LIVE FROM GITHUB") : String ("TEXTS: SAVED COPY - GITHUB NOT REACHABLE");
    g.setFont (look::font (10.0f, 500, 0.14f));
    g.setColour (Colours::white.withAlpha (0.45f));
    g.drawText (src, W - 77 - 340, 682, 340, 18, Justification::centredRight);
    GlyphArrangement ga;
    ga.addLineOfText (look::font (10.0f, 500, 0.14f), src, 0, 0);
    look::dot (g, { (float) (W - 77) - ga.getBoundingBox (0, -1, true).getWidth() - 14.0f, 691.0f }, live ? look::ok : look::red, 8.0f);
}

void HubComponent::restack()
{
    if (frameLayer != nullptr) frameLayer->toFront (false);
    replay.toFront (false);
    devBtn.toFront (false);
    if (devBar != nullptr) devBar->toFront (false);
    for (auto* o : { menu.get(), toastBox.get(), modal.get() })
        if (o != nullptr) o->toFront (false);
}

void HubComponent::paintOverChildren (Graphics& g)
{
    if (flashUntil > 0)
    {
        g.setFont (look::font (12.4f, 500));
        GlyphArrangement ga;
        ga.addLineOfText (g.getCurrentFont(), flashText, 0, 0);
        const float w = ga.getBoundingBox (0, -1, true).getWidth() + 30.0f;
        const auto r = Rectangle<float> (w, 32).withCentre ({ W * 0.5f, 144.0f + 16.0f });
        Path p;
        p.addRoundedRectangle (r, 16.0f);
        DropShadow (look::red.withAlpha (0.4f), 13, {}).drawForPath (g, p);
        g.setColour (Colour (0xff1a0606));
        g.fillPath (p);
        g.setColour (look::red.withAlpha (0.5f));
        g.strokePath (p, PathStrokeType (1.0f));
        g.setColour (look::text);
        g.drawText (flashText, r, Justification::centred);
    }

    if (introStage == 0)
        return;
    const double t = now() - stageStart;
    if (introStage == 3)
    {
        // the page fades in
        g.setOpacity (1.0f - ease ((float) t / 0.6f));
        g.drawImage (backgroundImage, getLocalBounds().toFloat(), RectanglePlacement::fillDestination);
        g.setOpacity (1.0f - ease ((float) t / 0.6f));
        g.drawImage (theme::image (theme::Img::frame), getLocalBounds().toFloat(), RectanglePlacement::stretchToFit);
        return;
    }
    if (introStage == 1)
    {
        const float in = ease ((float) t / 0.6f), out = t > 2.6 ? 1.0f - ease ((float) (t - 2.6) / 0.5f) : 1.0f;
        g.setGradientFill (ColourGradient (Colour (0xff1a0303), W * 0.5f, H * 0.5f, Colours::black, W * 0.5f + W * 0.35f, H * 0.5f, true));
        g.fillAll();
        const float scale = 0.9f + 0.1f * ease ((float) t / 2.4f);
        const float size = 384.0f * scale;
        const auto r = Rectangle<float> (size, size).withCentre ({ W * 0.5f, H * 0.5f });
        if (kiruLogo.isValid())
        {
            // a red glow that grows
            const float glow = ease ((float) t / 2.4f);
            g.setColour (Colour (0xffff0000).withAlpha (0.25f * glow * in * out));
            g.fillEllipse (r.reduced (size * 0.12f).expanded (20.0f * glow));
            g.setOpacity (in * out);
            g.drawImage (kiruLogo, r, RectanglePlacement::centred);
        }
        g.setOpacity (1.0f);
        g.setColour (Colours::white.withAlpha (0.35f * in * out));
        g.setFont (look::font (10.9f, 500, 0.25f));
        g.drawText ("CLICK TO SKIP", 0, roundToInt (H * 0.85f), W, 20, Justification::centred);
        return;
    }
    // stage 2: the Emulon X logo and the loading bar over the blurred window
    const float in = ease ((float) t / 0.6f);
    g.setColour (Colour (5, 2, 3).withAlpha (0.55f * in));
    g.fillAll();
    const float scale = 0.96f + 0.04f * ease ((float) t / 2.0f);
    const auto logoArea = Rectangle<float> (589.0f * scale, 135.0f * scale).withCentre ({ W * 0.5f, H * 0.5f - 60.0f });
    {
        Graphics::ScopedSaveState s (g);
        g.setOpacity (in);
        theme::drawLogo (g, logoArea, RectanglePlacement::centred);
    }
    const auto bar = Rectangle<float> (435, 18).withCentre ({ W * 0.5f, logoArea.getBottom() + 50.0f });
    look::led (g, bar, 5.0f);
    const float p = jmin (1.0f, ease ((float) t / 2.2f)) * (loaded ? 1.0f : 0.92f);
    auto fill = bar.reduced (3.0f).withWidth ((bar.getWidth() - 6.0f) * p);
    Path fp;
    fp.addRoundedRectangle (fill, 3.0f);
    DropShadow (look::red.withAlpha (0.8f), 10, {}).drawForPath (g, fp);
    g.setGradientFill (ColourGradient::horizontal (Colour (0xff7a0d0d), fill.getX(), look::red, fill.getRight()));
    g.fillPath (fp);
    g.setColour (look::muted.withAlpha (in));
    g.setFont (look::font (13.0f, 500, 0.3f));
    g.drawText (loadText, 0, roundToInt (bar.getBottom() + 14), W, 20, Justification::centred);
    g.setColour (Colours::white.withAlpha (0.35f * in));
    g.setFont (look::font (10.9f, 500, 0.25f));
    g.drawText ("CLICK TO SKIP", 0, roundToInt (H * 0.85f), W, 20, Justification::centred);
}

//== Layout =========================================================================
void HubComponent::resized()
{
    // header
    int x = innerX + 8 + 150 + 16;
    for (auto* b : tabs)
    {
        const int w = b->idealWidth() + 10;
        b->setBounds (x, innerY + 1, w, 32);
        x += w + 7;
    }
    gear->setBounds (innerX + innerW - 8 - 34, innerY + 1, 34, 32);
    bell->setBounds (gear->getX() - 8 - 34, innerY + 1, 34, 32);
    replay.setBounds (77, 682, 120, 18);
    devBtn.setBounds (200, 682, 100, 18);
    if (devBar != nullptr)
        devBar->setBounds (77, 15, W - 154, 39);
    layoutBody();
    if (frameLayer != nullptr)
        frameLayer->setBounds (getLocalBounds());
    if (modal != nullptr)
        modal->setBounds (getLocalBounds());
}

void HubComponent::layoutBody()
{
    const int top = innerY + headH + 1;
    const auto leftArea = Rectangle<int> (innerX, top, leftW - 3, innerB - top).reduced (17, 16);
    int boxH = 0;
    if (auto* ib = dynamic_cast<InstallBox*> (installBox.get()))
    {
        boxH = ib->preferredHeight (leftArea.getWidth());
        ib->setBounds (leftArea.getX(), leftArea.getBottom() - boxH, leftArea.getWidth(), boxH);
    }
    const auto viewArea = leftArea.withTrimmedBottom (boxH > 0 ? boxH + 12 : 0);
    leftView.setBounds (viewArea.expanded (4, 0).withTrimmedLeft (4));
    if (leftCol != nullptr)
    {
        const int w = viewArea.getWidth() - 8;
        leftCol->setSize (w, jmax (leftCol->heightFor (w), 10));
    }
    if (right != nullptr)
        right->setBounds (Rectangle<int> (innerX + leftW + 1, top, innerW - leftW - 1, innerB - top).reduced (17, 16));
}

//== Pages ==========================================================================
String HubComponent::signature() const
{
    String s;
    s << (int) tab << "|" << skinSel << "|" << bankSel << "|" << shotIndex[0] << shotIndex[1] << "|" << (int) showLog << (int) dev << "|" << checkStatus;
    for (auto& f : Model::textFiles())
        s << model.fileText (f).hashCode64();
    s << "|" << (int) model.job().active << model.job().key << "|" << model.relEmx.version << model.relSd.version << model.relHub.version << model.testEmx.version << model.testSd.version << model.testHub.version;
    s << "|" << model.installedVersion ("emx") << model.installedVersion ("sd") << (int) model.newPictures.size() << (int) model.skins.size() << (int) model.banks.size();
    for (auto& sk : model.skins) s << model.installedVersion ("skin:" + sk.id);
    for (auto& b : model.banks) s << model.installedVersion ("bank:" + b.id) << (int) model.notifyWhenOut (b.id) << (int) b.released;
    s << (int) model.textsLive << model.unreadCount();
    return s;
}

void HubComponent::changeListenerCallback (ChangeBroadcaster* source)
{
    if (source == &model.images)
    {
        repaint();
        return;
    }
    const auto sig = signature();
    if (sig != lastSignature)
        rebuild();
    else
        updateInstallBox();
    repaint();
}

void HubComponent::updateInstallBox()
{
    auto* ib = dynamic_cast<InstallBox*> (installBox.get());
    if (ib == nullptr)
        return;
    const auto& j = model.job();
    if (j.active)
    {
        ib->big.progress = j.progress;
        ib->big.setButtonText (j.label);
        ib->big.repaint();
    }
}

void HubComponent::rebuildNow()
{
    if (introStage == 1 || introStage == 2)
        return;
    lastSignature = signature();
    // remember the focused text box and the scroll position, so typing isn't interrupted
    focusKey = {};
    if (auto* f = Component::getCurrentlyFocusedComponent())
        if (auto* ed = dynamic_cast<TextEditor*> (f))
            if (auto* ti = dynamic_cast<TextItem*> (ed->getParentComponent()); ti != nullptr && isParentOf (ti))
            {
                focusKey = ti->getComponentID();
                focusCaret = ed->getCaretPosition();
            }
    const auto newPage = String ((int) tab) + skinSel + bankSel + String ((int) dev);
    leftScroll = newPage == pageKey ? leftView.getViewPosition() : Point<int>();
    pageKey = newPage;

    for (int i = 0; i < tabs.size(); ++i)
    {
        tabs[i]->setToggleState (i == (int) tab, dontSendNotification);
        if (i < 2)
            if (const auto n = (i == 0 ? model.emx.name : model.sd.name).toUpperCase().trim(); n.isNotEmpty())
                tabs[i]->setButtonText (n);
    }
    devBtn.setVisible (! dev && introStage != 1 && introStage != 2);
    if (dev)
    {
        if (devBar == nullptr)
        {
            auto bar = std::make_unique<DevBar>();
            bar->discard.onClick = [this] { model.discardChanges(); flash ("Changes discarded."); };
            bar->save.onClick = [this] { openSave(); };
            bar->logout.onClick = [this]
            {
                if (! model.changedFiles().isEmpty() || ! model.newPictures.empty())
                {
                    auto d = std::make_unique<Dialog> ("UNSAVED CHANGES");
                    d->para ("You changed texts that aren't saved to GitHub yet. Log out anyway? Your changes stay here until you close the Hub.");
                    d->action ("CANCEL", Style::grey, [this] { closeDialog(); });
                    d->action ("LOG OUT", Style::hot, [this] { closeDialog(); setDevMode (false, {}); });
                    showDialog (std::move (d));
                    return;
                }
                setDevMode (false, {});
            };
            addAndMakeVisible (*bar);
            devBar = std::move (bar);
        }
        auto* bar = static_cast<DevBar*> (devBar.get());
        bar->who = devUser;
        bar->files = model.changedFiles().size();
        bar->pictures = (int) model.newPictures.size();
        const bool any = bar->files + bar->pictures > 0;
        bar->discard.setEnabled (any);
        bar->save.setEnabled (any);
        bar->repaint();
    }
    else
        devBar = nullptr;

    leftView.setViewedComponent (nullptr, false);
    leftCol = std::make_unique<Column>();
    leftCol->gap = 12;
    leftCol->padding = BorderSize<int> (0, 0, 6, 0);
    leftCol->onRelayout = [this] { if (leftCol != nullptr) leftCol->setSize (leftCol->getWidth(), leftCol->heightFor (leftCol->getWidth())); };
    installBox = nullptr;
    right = nullptr;
    keep.clear();

    if (tab == Tab::emx) buildProduct (model.emx);
    else if (tab == Tab::sd) buildProduct (model.sd);
    else if (tab == Tab::skins) buildSkins();
    else buildBanks();

    leftView.setViewedComponent (leftCol.get(), false);
    if (installBox != nullptr) addAndMakeVisible (*installBox);
    if (right != nullptr) addAndMakeVisible (*right);
    layoutBody();
    leftView.setViewPosition (leftScroll);
    restack();
    if (devBar != nullptr) devBar->setBounds (77, 15, W - 154, 39);

    if (focusKey.isNotEmpty())
        if (auto* c = findById (leftCol.get(), focusKey); c != nullptr || (c = findById (right.get(), focusKey)) != nullptr || (c = findById (menu.get(), focusKey)) != nullptr)
            if (auto* ed = c->getNumChildComponents() > 0 ? dynamic_cast<TextEditor*> (c->getChildComponent (0)) : nullptr)
            {
                ed->grabKeyboardFocus();
                ed->setCaretPosition (focusCaret);
            }
    repaint();
}


void HubComponent::buildProduct (Product& p)
{
    const bool isEmx = p.key == "emx";
    auto& col = *leftCol;
    const auto S = "p:" + p.key + ":";

    // title: EMULON with a red X
    if (dev)
        col.add (text (p.name, opts (26.0f, 900, look::text, false, 0.08f), [&p] (const String& v) { if (v.isNotEmpty()) p.name = v; }, S + "name"));
    else
    {
        auto title = std::make_unique<Painted>();
        const auto name = p.name.toUpperCase();
        title->h = [] (int) { return 30; };
        title->draw = [name] (Graphics& g, Rectangle<float> r)
        {
            AttributedString s;
            const auto f = look::font (26.0f, 900, 0.08f);
            auto words = StringArray::fromTokens (name, " ", "");
            for (int i = 0; i < words.size(); ++i)
                s.append (words[i] + (i + 1 < words.size() ? " " : ""), f, words[i] == "X" ? look::red : look::text);
            s.setJustification (Justification::centredLeft);
            TextLayout tl;
            tl.createLayout (s, r.getWidth());
            tl.draw (g, r);
        };
        col.add (std::move (title));
    }
    col.add (text (p.sub, opts (11.6f, 600, look::muted, ! dev, 0.08f), [&p] (const String& v) { p.sub = v; }, S + "sub"), 2);

    // tags and the version (from GitHub Releases)
    auto chips = std::make_unique<ChipsItem>();
    const auto& rel = isEmx ? model.relEmx : model.relSd;
    const auto have = model.installedVersion (p.key);
    if (dev)
    {
        auto tagsRow = std::make_unique<RowItem>();
        auto label = std::make_unique<Painted>();
        label->draw = [] (Graphics& g, Rectangle<float> r) { g.setColour (look::muted); g.setFont (look::font (10.4f, 700, 0.08f)); g.drawText ("TAGS", r, Justification::centredLeft); };
        auto tagsEd = text (p.tags, opts (11.0f, 500, look::text), [&p] (const String& v) { p.tags = v; }, S + "tags");
        tagsEd->setComponentID (S + "tags");
        tagsRow->add (label.get(), 40, 24);
        tagsRow->add (tagsEd.get(), 300, 24);
        keep.push_back (std::move (label));
        keep.push_back (std::move (tagsEd));
        col.add (std::move (tagsRow));
    }
    else
        for (auto t : StringArray::fromTokens (p.tags, ",", ""))
            if ((t = t.trim()).isNotEmpty())
                chips->chips.push_back ({ t, t.equalsIgnoreCase ("free") ? look::ok : look::muted });
    String ver;
    if (rel.found && isNewer (rel.version, have) && have.isNotEmpty()) ver = "v" + rel.version + " available";
    else if (have.isNotEmpty()) ver = "v" + have;
    else if (rel.found) ver = "v" + rel.version;
    else if (p.extra["version"].isNotEmpty()) ver = p.extra["version"];
    if (ver.isNotEmpty())
        chips->chips.push_back ({ ver + (dev ? "  AUTO" : ""), look::red });
    const auto& test = model.release (p.key, true);
    if (dev && test.found)
        chips->chips.push_back ({ "Test build " + test.version, look::warm });
    col.add (std::move (chips));

    TextItem::Options d = opts (13.0f, 500, look::body);
    d.paragraphs = true;
    d.placeholder = "Description";
    col.add (text (p.descMd, d, [&p] (const String& v) { p.descMd = v; }, S + "desc"));

    // features
    auto feats = std::make_unique<Column>();
    feats->gap = 4;
    for (int i = 0; i < p.feats.size(); ++i)
    {
        auto t = text (p.feats[i], opts (12.4f, 500, Colour (0xffd6caca)), [&p, i] (const String& v) { if (i < p.feats.size()) p.feats.set (i, v); }, S + "feat" + String (i));
        std::unique_ptr<Component> rm;
        if (dev)
        {
            auto b = std::make_unique<IconButton> (Icon::close, Style::dark);
            b->flat = true;
            b->iconColour = Colour (0xffff7a7a);
            b->onClick = [this, &p, i] { p.feats.remove (i); edited(); };
            rm = std::move (b);
        }
        feats->add (std::make_unique<BulletItem> (std::move (t), std::move (rm)));
    }
    if (dev)
    {
        auto add = std::make_unique<HubButton> ("+ ADD FEATURE", Style::grey, 9.5f);
        add->onClick = [this, &p] { p.feats.add ("New feature"); edited(); };
        auto row = std::make_unique<RowItem>();
        row->add (add.get(), 130, 24);
        keep.push_back (std::move (add));
        feats->add (std::move (row));
    }
    col.add (std::move (feats));

    // what's new
    if (showLog || dev)
    {
        auto box = std::make_unique<Column>();
        box->padding = BorderSize<int> (10, 12, 10, 12);
        box->gap = 4;
        box->background = [] (Graphics& g, Rectangle<float> r) { look::led (g, r); };
        if (dev)
        {
            TextItem::Options hint = opts (10.1f, 500, look::warm);
            box->add (std::make_unique<TextItem> ("WHAT'S NEW - one line per item, starting with \"- \". **Bold** lines are version headings.", hint));
            TextItem::Options lo = opts (11.0f, 500, Colour (0xffe7c9c9));
            lo.multiline = true;
            box->add (text (p.logMd, lo, [&p] (const String& v) { p.logMd = v; }, S + "log"));
        }
        else
        {
            auto lines = StringArray::fromLines (p.logMd);
            for (auto l : lines)
            {
                l = l.trim();
                if (! l.startsWith ("- ")) continue;
                l = l.substring (2);
                if (l.startsWith ("**") && l.endsWith ("**"))
                    box->add (std::make_unique<TextItem> (l.substring (2, l.length() - 2).toUpperCase(), opts (11.0f, 700, look::red, false, 0.06f)), 8);
                else
                {
                    auto o = opts (11.0f, 500, Colour (0xffe7c9c9));
                    box->add (std::make_unique<BulletItem> (std::make_unique<TextItem> (l, o)));
                }
            }
            if (rel.found && rel.notes.isNotEmpty() && lines.isEmpty())
                box->add (std::make_unique<TextItem> (rel.notes, opts (11.0f, 500, Colour (0xffe7c9c9))));
        }
        col.add (std::move (box));
    }
    if (dev && isEmx)
        col.add (std::make_unique<TextItem> ("The version number and update alerts come from your GitHub Releases automatically (tags v..., sd-v..., hub-v...).", opts (10.1f, 500, look::warm)));

    // install box
    auto ib = std::make_unique<InstallBox>();
    const auto& j = model.job();
    const bool busy = j.active && j.key == p.key;
    const bool update = rel.found && have.isNotEmpty() && isNewer (rel.version, have);
    const auto size = formatBytes (rel.assetSize);
    if (busy)
    {
        ib->status = "Working... you can keep using the Hub";
        ib->dotColour = look::red;
        ib->big.setButtonText (j.label);
        ib->big.progress = j.progress;
        ib->big.setEnabled (false);
    }
    else if (update)
    {
        const auto onDisk = formatBytes (model.installedSize (p.key));
        ib->status = "Installed: " + have + (onDisk.isNotEmpty() ? " (" + onDisk + ")" : String()) + " " + middot + " update available"
                     + (size.isNotEmpty() ? " (" + size + " download)" : String());
        ib->dotColour = look::red;
        ib->big.setButtonText ("UPDATE TO " + rel.version.toUpperCase());
    }
    else if (have.isNotEmpty())
    {
        const auto onDisk = formatBytes (model.installedSize (p.key));
        ib->status = "Installed: " + have + (onDisk.isNotEmpty() ? " (" + onDisk + ")" : String()) + (rel.found ? " " + middot + " up to date" : "");
        ib->dotColour = look::ok;
        ib->big.setButtonText (isEmx ? "LAUNCH EMULON X" : "LAUNCH EMULON X SD");
    }
    else if (rel.found && rel.assetUrl.isNotEmpty())
    {
        ib->status = "Not installed" + (size.isNotEmpty() ? " " + middot + " " + size : String());
        ib->big.setButtonText ("DOWNLOAD & INSTALL");
    }
    else
    {
        ib->status = ! model.releasesLive ? "Couldn't reach GitHub to look for downloads: " + model.releasesError : String ("Not released yet - turn on notifications and the Hub tells you when it's out");
        ib->dotColour = look::red;
        ib->big.setButtonText ("NOT AVAILABLE YET");
        ib->big.style = Style::quiet;
        ib->big.setEnabled (false);
    }
    if (have.isNotEmpty())
        ib->folder = folderText (model.installedFolder (p.key));
    const bool launchable = have.isNotEmpty() && ! update && ! busy;
    ib->big.onClick = [this, key = p.key, launchable] { if (launchable) launch (key); else install (key); };
    if (update)
        ib->addLink ("Launch", [this, key = p.key] { launch (key); });
    ib->addLink (showLog ? "Hide" : "What's new", [this] { showLog = ! showLog; rebuild(); });
    ib->addLink (checkStatus.isNotEmpty() ? checkStatus : String ("Check for updates"), [this] { checkStatus = "Checking..."; rebuild(); refreshNow (false); });
    if (have.isNotEmpty() && ! busy)
        ib->addLink ("Reinstall", [this, key = p.key] { install (key); });
    if (busy)
        ib->addLink ("Cancel", [this] { model.cancelJob(); });
    if (dev && test.found && ! busy)
        ib->addLink ("Install test build", [this, key = p.key] { install ("test:" + key); });
    installBox = std::move (ib);

    // screenshots
    const int pi = isEmx ? 0 : 1;
    auto viewer = std::make_unique<ShotViewer>();
    viewer->count = p.shotFiles.size();
    shotIndex[pi] = jlimit (0, jmax (0, viewer->count - 1), shotIndex[pi]);
    viewer->index = shotIndex[pi];
    viewer->getImage = [this, &p] (int i) { return model.images.get (model.hubImageUrl (p.shotFiles[i])); };
    viewer->onShow = [this, pi, n = viewer->count] (int i) { if (n > 0) { shotIndex[pi] = (i + n) % n; rebuild(); } };
    if (viewer->count > 0)
    {
        auto o = opts (12.4f, 500, look::red, false, 0.06f);
        const int i = viewer->index;
        viewer->caption = text (p.shotCaptions[i], o, [&p, i] (const String& v) { if (i < p.shotCaptions.size()) p.shotCaptions.set (i, v); }, S + "cap" + String (i));
        viewer->addAndMakeVisible (*viewer->caption);
        if (dev)
        {
            viewer->removeBtn = std::make_unique<HubButton> ("REMOVE", Style::grey, 9.0f);
            viewer->removeBtn->onClick = [this, &p, pi, i] { p.shotFiles.remove (i); p.shotCaptions.remove (i); shotIndex[pi] = jmax (0, i - 1); edited(); };
            viewer->addAndMakeVisible (*viewer->removeBtn);
        }
    }
    if (dev)
    {
        viewer->addBtn = std::make_unique<HubButton> ("+ ADD", Style::grey, 10.0f);
        viewer->addBtn->dashed = true;
        viewer->addBtn->onClick = [this, &p, pi]
        {
            pickPicture ([this, &p, pi] (const String& name)
            {
                p.shotFiles.add (name);
                p.shotCaptions.add ("New screenshot");
                shotIndex[pi] = p.shotFiles.size() - 1;
                edited();
            });
        };
        viewer->addAndMakeVisible (*viewer->addBtn);
    }
    right = std::move (viewer);
}

void HubComponent::drawImageCover (Graphics& g, Rectangle<float> r, const Image& img, bool fit, float radius) const
{
    Graphics::ScopedSaveState s (g);
    Path clip;
    clip.addRoundedRectangle (r, radius);
    g.reduceClipRegion (clip);
    g.setColour (Colours::black);
    g.fillRect (r);
    if (img.isValid())
    {
        g.setOpacity (1.0f);
        g.setImageResamplingQuality (Graphics::highResamplingQuality);
        g.drawImage (img, r, fit ? RectanglePlacement::centred : RectanglePlacement::fillDestination);
    }
    else
    {
        g.setColour (look::muted.withAlpha (0.5f));
        g.setFont (look::font (9.5f, 600, 0.2f));
        g.drawText ("NO PICTURE", r, Justification::centred);
    }
}

void HubComponent::drawCover (Graphics& g, Rectangle<float> r, const BankItem& b, bool small) const
{
    Graphics::ScopedSaveState s (g);
    if (b.image.isNotEmpty())
        drawImageCover (g, r, model.images.get (model.hubImageUrl (b.image)), false, small ? 5.0f : 7.0f);
    else
    {
        const float h = (float) ((b.colour % 360 + 360) % 360) / 360.0f;
        ColourGradient cg (Colour::fromHSL (h, 0.85f, 0.45f, 1.0f), r.getX() + r.getWidth() * 0.3f, r.getY() + r.getHeight() * 0.2f,
                           Colours::black, r.getX() + r.getWidth() * 1.05f, r.getBottom(), true);
        cg.addColour (0.7, Colour::fromHSL (h, 0.7f, 0.12f, 1.0f));
        g.setGradientFill (cg);
        g.fillRect (r);
        // a waveform
        Path wave;
        const float hue = (float) b.colour;
        for (int i = 0; i < 48; ++i)
        {
            const float x = (float) i / 47.0f;
            const float y = 0.5f + (std::sin ((float) i * 0.55f + hue) * 0.22f * std::sin (x * MathConstants<float>::pi) + std::sin ((float) i * 1.7f) * 0.08f);
            const Point<float> pt (r.getX() + x * r.getWidth(), r.getY() + r.getHeight() * (0.18f + 0.45f * y));
            if (i == 0) wave.startNewSubPath (pt); else wave.lineTo (pt);
        }
        g.setColour (Colours::white.withAlpha (0.55f));
        g.strokePath (wave, PathStrokeType (1.4f));
    }
    const float u = small ? 0.8f : 1.15f;
    if (b.isSoon())
    {
        g.setFont (look::font (9.0f * u, 800, 0.12f));
        GlyphArrangement ga;
        ga.addLineOfText (g.getCurrentFont(), "COMING SOON", 0, 0);
        const auto tr = Rectangle<float> (ga.getBoundingBox (0, -1, true).getWidth() + 14.0f * u, 17.0f * u)
                            .withPosition (r.getRight() - 7 * u - (ga.getBoundingBox (0, -1, true).getWidth() + 14.0f * u), r.getY() + 7 * u);
        Path p;
        p.addRoundedRectangle (tr, 4.0f);
        DropShadow (look::red.withAlpha (0.7f), 8, {}).drawForPath (g, p);
        g.setColour (look::red);
        g.fillPath (p);
        g.setColour (Colours::white);
        g.drawText ("COMING SOON", tr, Justification::centred);
    }
    auto textArea = r.reduced (9.0f * u, 8.0f * u);
    g.setColour (Colours::black.withAlpha (0.6f));
    g.setFont (look::font (14.0f * u, 700, 0.1f));
    g.drawText (b.name, textArea.translated (0, 1.5f), Justification::bottomLeft, true);
    g.setColour (Colours::white);
    g.drawText (b.name, textArea, Justification::bottomLeft, true);
    g.setColour (Colours::white.withAlpha (0.85f));
    g.setFont (look::font (8.5f * u, 600, 0.14f));
    g.drawText (b.official ? "OFFICIAL" : "BANK", textArea.withTrimmedBottom (18.0f * u), Justification::bottomLeft, true);
}

void HubComponent::buildSkins()
{
    auto& list = model.skins;
    if (list.empty())
        return;
    auto it = std::find_if (list.begin(), list.end(), [this] (auto& s) { return s.id == skinSel; });
    if (it == list.end()) { it = list.begin(); skinSel = it->id; }
    auto& s = *it;
    const bool canEdit = dev && ! s.community;
    const auto S = "skin:" + s.id + ":";
    auto& col = *leftCol;

    col.add (canEdit ? text (s.name, opts (22.0f, 800, look::text, false, 0.08f), [&s] (const String& v) { if (v.isNotEmpty()) s.name = v; }, S + "name")
                     : std::make_unique<TextItem> (s.name, opts (22.0f, 800, look::text, false, 0.08f)));
    if (canEdit)
    {
        auto byRow = std::make_unique<RowItem>();
        byRow->gap = 4;
        auto label = std::make_unique<TextItem> ("SKIN " + middot + " BY", opts (11.6f, 600, look::muted, false, 0.08f));
        byRow->add (label.get(), 82, 20);
        auto by = text (s.by, opts (11.6f, 600, look::muted, false, 0.08f), [&s] (const String& v) { s.by = v; }, S + "by");
        byRow->add (by.get(), 240, 20);
        keep.push_back (std::move (label));
        keep.push_back (std::move (by));
        col.add (std::move (byRow), 2);
    }
    else
        col.add (std::make_unique<TextItem> ("SKIN" + (s.by.isNotEmpty() ? " " + middot + " BY " + s.by.toUpperCase() : String()), opts (11.6f, 600, look::muted, false, 0.08f)), 2);

    auto pic = std::make_unique<PictureItem>();
    const auto url = s.community ? s.preview : model.hubImageUrl (s.image);
    pic->draw = [this, url] (Graphics& g, Rectangle<float> r)
    {
        look::led (g, r, 7.0f, false);
        drawImageCover (g, r.reduced (1.0f), model.images.get (url), true, 6.0f);
    };
    if (canEdit)
    {
        auto change = std::make_unique<HubButton> ("CHANGE PICTURE...", Style::grey, 9.0f);
        change->onClick = [this, &s] { pickPicture ([this, &s] (const String& n) { s.image = n; edited(); }); };
        change->setSize (130, 24);
        pic->addAndMakeVisible (*change);
        pic->overlay = change.get();
        keep.push_back (std::move (change));
    }
    col.add (std::move (pic));

    auto chips = std::make_unique<ChipsItem>();
    if (s.included) chips->chips.push_back ({ "Included", look::ok });
    else { chips->chips.push_back ({ "Community", look::muted }); chips->chips.push_back ({ ".emxd", look::muted }); }
    if (s.community && s.version.isNotEmpty()) chips->chips.push_back ({ "v" + s.version, look::red });
    if (canEdit)
    {
        auto b = std::make_unique<HubButton> (s.included ? "MAKE IT A DOWNLOAD" : "MARK AS INCLUDED", Style::grey, 9.0f);
        b->onClick = [this, &s] { s.included = ! s.included; edited(); };
        b->setSize (b->idealWidth() + 8, 22);
        chips->addAndMakeVisible (*b);
        chips->extras.push_back (b.get());
        keep.push_back (std::move (b));
    }
    col.add (std::move (chips));

    TextItem::Options d = opts (13.0f, 500, look::body);
    d.paragraphs = true;
    d.placeholder = "Description";
    col.add (canEdit ? text (s.descMd, d, [&s] (const String& v) { s.descMd = v; }, S + "desc") : std::make_unique<TextItem> (s.descMd, d));

    if (canEdit && ! s.included)
    {
        auto row = std::make_unique<RowItem>();
        auto l1 = std::make_unique<TextItem> ("SIZE", opts (10.4f, 600, look::muted, false, 0.08f));
        auto e1 = text (s.size, opts (11.0f, 500, look::text), [&s] (const String& v) { s.size = v; }, S + "size");
        auto l2 = std::make_unique<TextItem> ("DOWNLOAD LINK", opts (10.4f, 600, look::muted, false, 0.08f));
        auto e2 = text (s.download, opts (11.0f, 500, look::text), [&s] (const String& v) { s.download = v; }, S + "dl");
        row->add (l1.get(), 34, 22); row->add (e1.get(), 80, 22); row->add (l2.get(), 100, 22); row->add (e2.get(), 330, 22);
        for (auto* c : { l1.release(), e1.release(), l2.release(), e2.release() }) keep.emplace_back (c);
        col.add (std::move (row));
    }
    if (canEdit)
    {
        auto rm = std::make_unique<HubButton> ("REMOVE THIS SKIN", Style::grey, 9.0f);
        rm->textColour = Colour (0xffff8a8a);
        rm->onClick = [this, id = s.id]
        {
            auto& l = model.skins;
            l.erase (std::remove_if (l.begin(), l.end(), [&id] (auto& k) { return k.id == id && ! k.community; }), l.end());
            skinSel = {};
            edited();
        };
        auto row = std::make_unique<RowItem>();
        row->add (rm.get(), 140, 24);
        keep.push_back (std::move (rm));
        col.add (std::move (row));
    }
    if (s.community)
        col.add (std::make_unique<TextItem> ("A community skin, made in Emulon X SD and checked before it was added.", opts (10.4f, 500, look::muted)));

    // install box
    auto ib = std::make_unique<InstallBox>();
    const auto key = "skin:" + s.id;
    const auto have = model.installedVersion (key);
    const auto& j = model.job();
    if (j.active && j.key == key)
    {
        ib->status = "Downloading...";
        ib->dotColour = look::red;
        ib->big.setButtonText (j.label);
        ib->big.progress = j.progress;
        ib->big.setEnabled (false);
        ib->addLink ("Cancel", [this] { model.cancelJob(); });
    }
    else if (s.included)
    {
        ib->status = "Already part of Emulon X " + middot + " pick it in Settings > Customization";
        ib->dotColour = look::ok;
        ib->big.setButtonText ("INCLUDED");
        ib->big.style = Style::quiet;
        ib->big.setEnabled (false);
    }
    else if (have.isNotEmpty())
    {
        const bool update = s.community && isNewer (s.version, have);
        ib->status = update ? "Installed: " + have + " " + middot + " a newer version is here" : String ("Installed ") + middot + String (" pick it in Emulon X under Settings > Customization");
        ib->dotColour = update ? look::red : look::ok;
        ib->folder = folderText (model.installedFolder (key));
        ib->big.setButtonText (update ? "UPDATE THIS SKIN" : "INSTALLED " + middot + " READY IN SETTINGS");
        if (! update) { ib->big.style = Style::quiet; ib->big.setEnabled (false); }
        const auto file = File (model.settings.get ("installed." + key).fromFirstOccurrenceOf ("|", false, false));
        ib->addLink ("Use it in Emulon X", [this, file]
        {
            SharedResourcePointer<emx::Settings> st;
            st->useCustomSkin (file);
            flash ("Emulon X uses this skin the next time it opens its window.");
        });
        ib->addLink ("Show the file", [file] { file.revealToUser(); });
    }
    else
    {
        ib->status = s.download.isEmpty() ? String ("No download yet") : "Not installed" + (s.size.isNotEmpty() ? " " + middot + " " + s.size : String());
        ib->big.setButtonText ("GET THIS SKIN");
        ib->big.setEnabled (s.download.isNotEmpty());
    }
    ib->big.onClick = [this, key] { install (key); };
    installBox = std::move (ib);

    // the grid
    auto panel = std::make_unique<GridPanel>();
    auto card = [this] (const SkinItem& k)
    {
        CardGrid::Card c;
        c.id = k.id;
        c.name = k.name;
        const bool inst = model.installedVersion ("skin:" + k.id).isNotEmpty();
        c.tag = k.included ? "INCLUDED" : inst ? "INSTALLED" : "GET";
        c.tagColour = k.included || inst ? look::ok : look::muted;
        const auto u = k.community ? k.preview : model.hubImageUrl (k.image);
        c.pic = [this, u] (Graphics& g, Rectangle<float> r) { drawImageCover (g, r, model.images.get (u), false, 5.0f); };
        return c;
    };
    CardGrid::Group included { "Included with Emulon X", {} }, community { "From the community", {} };
    for (auto& k : list)
        (k.included ? included : community).cards.push_back (card (k));
    if (dev)
    {
        CardGrid::Card add;
        add.add = true;
        add.name = "+ NEW SKIN";
        community.cards.push_back (add);
    }
    if (community.cards.empty())
        community.title = {};
    panel->grid.groups = { included, community };
    panel->grid.selected = skinSel;
    panel->grid.note = community.cards.empty()
        ? "No community skins yet. Make one in Emulon X SD and send it in with SUBMIT A SKIN - after a quick check it shows up here for everyone."
        : "Community skins come from the EmulonX-Skins collection on GitHub. Make your own in Emulon X SD and send it in with SUBMIT A SKIN.";
    panel->grid.onPick = [this] (const String& id) { skinSel = id; rebuild(); };
    panel->grid.onAdd = [this]
    {
        SkinItem k;
        k.id = "new-" + String (Random::getSystemRandom().nextInt (1000000));
        k.name = "NEW SKIN";
        k.by = devUser;
        k.descMd = "Describe the skin here.";
        auto pos = std::find_if (model.skins.begin(), model.skins.end(), [] (auto& x) { return x.community; });
        model.skins.insert (pos, k);
        skinSel = k.id;
        edited();
    };
    panel->submit = std::make_unique<HubButton> ("SUBMIT A SKIN", Style::hot, 10.5f);
    panel->submit->onClick = [this] { openSubmit(); };
    panel->addAndMakeVisible (*panel->submit);
    right = std::move (panel);
}

void HubComponent::buildBanks()
{
    auto& list = model.banks;
    if (list.empty())
        return;
    auto it = std::find_if (list.begin(), list.end(), [this] (auto& b) { return b.id == bankSel; });
    if (it == list.end()) { it = list.begin(); bankSel = it->id; }
    auto& b = *it;
    const bool canEdit = dev && ! b.fromIndex;
    const auto S = "bank:" + b.id + ":";
    auto& col = *leftCol;

    col.add (canEdit ? text (b.name, opts (22.0f, 800, look::text, false, 0.08f), [&b] (const String& v) { if (v.isNotEmpty()) b.name = v; }, S + "name")
                     : std::make_unique<TextItem> (b.name, opts (22.0f, 800, look::text, false, 0.08f)));
    if (canEdit)
    {
        auto byRow = std::make_unique<RowItem>();
        byRow->gap = 4;
        auto label = std::make_unique<TextItem> ("BANK " + middot + " BY", opts (11.6f, 600, look::muted, false, 0.08f));
        byRow->add (label.get(), 82, 20);
        auto by = text (b.by, opts (11.6f, 600, look::muted, false, 0.08f), [&b] (const String& v) { b.by = v; }, S + "by");
        byRow->add (by.get(), 240, 20);
        keep.push_back (std::move (label));
        keep.push_back (std::move (by));
        col.add (std::move (byRow), 2);
    }
    else
        col.add (std::make_unique<TextItem> ("BANK" + (b.by.isNotEmpty() ? " " + middot + " BY " + b.by.toUpperCase() : String()), opts (11.6f, 600, look::muted, false, 0.08f)), 2);

    auto pic = std::make_unique<PictureItem>();
    const auto copy = b;
    pic->draw = [this, copy] (Graphics& g, Rectangle<float> r)
    {
        Graphics::ScopedSaveState s (g);
        Path clip;
        clip.addRoundedRectangle (r, 7.0f);
        g.reduceClipRegion (clip);
        drawCover (g, r, copy, false);
        g.setColour (Colours::black);
        g.drawRoundedRectangle (r.reduced (0.5f), 7.0f, 1.0f);
    };
    if (canEdit)
    {
        auto change = std::make_unique<HubButton> ("CHANGE PICTURE...", Style::grey, 9.0f);
        change->onClick = [this, &b] { pickPicture ([this, &b] (const String& n) { b.image = n; edited(); }); };
        change->setSize (130, 24);
        pic->addAndMakeVisible (*change);
        pic->overlay = change.get();
        keep.push_back (std::move (change));
    }
    col.add (std::move (pic));

    if (canEdit)
    {
        auto row = std::make_unique<RowItem>();
        auto off = std::make_unique<HubButton> ("OFFICIAL", Style::grey, 9.0f);
        off->setToggleState (b.official, dontSendNotification);
        off->onClick = [this, &b] { b.official = ! b.official; edited(); };
        auto soon = std::make_unique<HubButton> ("COMING SOON", Style::grey, 9.0f);
        soon->setToggleState (b.soon, dontSendNotification);
        soon->onClick = [this, &b] { b.soon = ! b.soon; edited(); };
        row->add (off.get(), 80, 24);
        row->add (soon.get(), 100, 24);
        keep.push_back (std::move (off));
        keep.push_back (std::move (soon));
        if (b.image.isNotEmpty())
        {
            auto drawn = std::make_unique<HubButton> ("USE DRAWN COVER", Style::grey, 9.0f);
            drawn->onClick = [this, &b] { b.image = {}; edited(); };
            row->add (drawn.get(), 130, 24);
            keep.push_back (std::move (drawn));
        }
        else
        {
            auto hueLabel = std::make_unique<TextItem> ("COVER COLOUR", opts (10.0f, 600, look::muted, false, 0.08f));
            auto hue = std::make_unique<Slider> (Slider::LinearHorizontal, Slider::NoTextBox);
            hue->setRange (0, 360, 1);
            hue->setValue (b.colour, dontSendNotification);
            hue->setColour (Slider::thumbColourId, look::red);
            hue->setColour (Slider::trackColourId, look::redDark);
            hue->onValueChange = [this, &b, h = hue.get()] { b.colour = (int) h->getValue(); repaint(); if (leftCol) leftCol->repaint(); };
            hue->onDragEnd = [this] { edited(); };
            row->add (hueLabel.get(), 104, 24);
            row->add (hue.get(), 110, 24);
            keep.push_back (std::move (hueLabel));
            keep.push_back (std::move (hue));
        }
        col.add (std::move (row));

        auto fields = std::make_unique<RowItem>();
        auto add = [this, &fields] (const String& l, int lw, const String& v, int w, std::function<void (const String&)> f, const String& id)
        {
            auto lab = std::make_unique<TextItem> (l, opts (10.4f, 600, look::muted, false, 0.08f));
            auto ed = text (v, opts (11.0f, 500, look::text), std::move (f), id);
            fields->add (lab.get(), lw, 22);
            fields->add (ed.get(), w, 22);
            keep.push_back (std::move (lab));
            keep.push_back (std::move (ed));
        };
        add ("PRESETS", 64, b.presets, 50, [&b] (const String& v) { b.presets = v; }, S + "presets");
        add ("SIZE", 34, b.size, 80, [&b] (const String& v) { b.size = v; }, S + "size");
        add ("DOWNLOAD LINK", 100, b.download, 330, [&b] (const String& v) { b.download = v; }, S + "dl");
        col.add (std::move (fields));
    }
    else
    {
        auto chips = std::make_unique<ChipsItem>();
        if (b.official) chips->chips.push_back ({ "Official", look::red });
        if (! b.isSoon() && b.presetsText().isNotEmpty()) chips->chips.push_back ({ b.presetsText() + " presets", look::muted });
        if (! b.isSoon() && b.sizeText().isNotEmpty()) chips->chips.push_back ({ b.sizeText(), look::muted });
        if (b.ver().isNotEmpty()) chips->chips.push_back ({ "v" + b.ver(), look::muted });
        col.add (std::move (chips));
    }

    TextItem::Options d = opts (13.0f, 500, look::body);
    d.paragraphs = true;
    d.placeholder = "Description";
    col.add (canEdit ? text (b.descMd, d, [&b] (const String& v) { b.descMd = v; }, S + "desc") : std::make_unique<TextItem> (b.descMd, d));
    if (canEdit)
    {
        auto rm = std::make_unique<HubButton> ("REMOVE THIS BANK", Style::grey, 9.0f);
        rm->textColour = Colour (0xffff8a8a);
        rm->onClick = [this, id = b.id]
        {
            auto& l = model.banks;
            l.erase (std::remove_if (l.begin(), l.end(), [&id] (auto& k) { return k.id == id && ! k.fromIndex; }), l.end());
            bankSel = {};
            edited();
        };
        auto row = std::make_unique<RowItem>();
        row->add (rm.get(), 140, 24);
        keep.push_back (std::move (rm));
        col.add (std::move (row));
    }
    if (dev && b.fromIndex)
        col.add (std::make_unique<TextItem> ("This bank comes from the EmulonX-Banks repository (banks/" + b.id + "/info.json), so it's edited there.", opts (10.1f, 500, look::warm)));

    // install box
    auto ib = std::make_unique<InstallBox>();
    const auto key = "bank:" + b.id;
    const auto have = model.installedVersion (key);
    const auto& j = model.job();
    if (j.active && j.key == key)
    {
        ib->status = "Downloading...";
        ib->dotColour = look::red;
        ib->big.setButtonText (j.label);
        ib->big.progress = j.progress;
        ib->big.setEnabled (false);
        ib->addLink ("Cancel", [this] { model.cancelJob(); });
    }
    else if (b.isSoon() || b.link().isEmpty())
    {
        const bool on = model.notifyWhenOut (b.id);
        ib->status = "Coming soon";
        ib->dotColour = look::red;
        ib->big.setButtonText (on ? "YOU'LL BE NOTIFIED" : "NOTIFY ME WHEN IT'S OUT");
        ib->big.style = on ? Style::quiet : Style::hot;
        ib->big.onClick = [this, id = b.id, on] { model.setNotifyWhenOut (id, ! on); };
    }
    else if (have.isNotEmpty())
    {
        const bool update = isNewer (b.ver(), have);
        ib->status = update ? "Installed: " + have + " " + middot + " a newer version is out" : String ("Installed ") + middot + String (" it's in your banks in Emulon X");
        ib->dotColour = update ? look::red : look::ok;
        ib->folder = folderText (model.installedFolder (key));
        ib->big.setButtonText (update ? "UPDATE THIS BANK" : "INSTALLED " + middot + " IN YOUR BANKS FOLDER");
        if (! update) { ib->big.style = Style::quiet; ib->big.setEnabled (false); }
        const auto file = File (model.settings.get ("installed." + key).fromFirstOccurrenceOf ("|", false, false));
        ib->addLink ("Show the file", [file] { file.revealToUser(); });
        ib->big.onClick = [this, key] { install (key); };
    }
    else
    {
        ib->status = "Not installed" + (b.sizeText().isNotEmpty() ? " " + middot + " " + b.sizeText() : String());
        ib->big.setButtonText ("DOWNLOAD BANK");
        ib->big.onClick = [this, key] { install (key); };
    }
    installBox = std::move (ib);

    auto panel = std::make_unique<GridPanel>();
    auto card = [this] (const BankItem& k)
    {
        CardGrid::Card c;
        c.id = k.id;
        c.name = k.name;
        const bool inst = model.installedVersion ("bank:" + k.id).isNotEmpty();
        c.tag = k.isSoon() || k.link().isEmpty() ? "SOON" : inst ? "INSTALLED" : "GET";
        c.tagColour = c.tag == "SOON" ? look::red : inst ? look::ok : look::muted;
        c.pic = [this, k] (Graphics& g, Rectangle<float> r) { drawCover (g, r, k, true); };
        return c;
    };
    CardGrid::Group official { "Official", {} }, more { "More banks", {} };
    for (auto& k : list)
        (k.official ? official : more).cards.push_back (card (k));
    if (dev)
    {
        CardGrid::Card add;
        add.add = true;
        add.name = "+ NEW BANK";
        more.cards.push_back (add);
    }
    panel->grid.groups = { official, more };
    panel->grid.selected = bankSel;
    panel->grid.note = "Banks install straight into your banks folder, so they show up in Emulon X right away.";
    panel->grid.onPick = [this] (const String& id) { bankSel = id; rebuild(); };
    panel->grid.onAdd = [this]
    {
        BankItem k;
        k.id = "new-" + String (Random::getSystemRandom().nextInt (1000000));
        k.name = "NEW BANK";
        k.by = devUser;
        k.soon = true;
        k.colour = Random::getSystemRandom().nextInt (360);
        k.descMd = "Describe the bank here.";
        auto pos = std::find_if (model.banks.begin(), model.banks.end(), [] (auto& x) { return x.fromIndex; });
        model.banks.insert (pos, k);
        bankSel = k.id;
        edited();
    };
    right = std::move (panel);
}

//== Tabs and targets ===============================================================
void HubComponent::showTab (Tab t)
{
    tab = t;
    showLog = false;
    rebuild();
}

void HubComponent::openTarget (const String& open)
{
    const auto t = open.upToFirstOccurrenceOf (":", false, false).trim().toLowerCase();
    const auto item = open.fromFirstOccurrenceOf (":", false, false).trim();
    if (t == "hub")
    {
        install ("hub");
        return;
    }
    Tab target = t == "sd" ? Tab::sd : t == "skins" ? Tab::skins : t == "banks" ? Tab::banks : Tab::emx;
    if (item.isNotEmpty())
    {
        if (target == Tab::skins)
            for (auto& s : model.skins) if (slugOf (s.name) == slugOf (item)) skinSel = s.id;
        if (target == Tab::banks)
            for (auto& b : model.banks) if (slugOf (b.name) == slugOf (item)) bankSel = b.id;
    }
    showTab (target);
}

void HubComponent::openNote (const Model::Note& n)
{
    closeOverlays();
    openTarget (n.open);
}

//== Overlays =======================================================================
void HubComponent::dispose (std::unique_ptr<Overlay>& o)
{
    // an overlay is often closed by one of its own buttons: hide it now, delete it a moment later
    if (o == nullptr)
        return;
    auto* old = o.release();
    old->setVisible (false);
    MessageManager::callAsync ([old] { delete old; });
}

void HubComponent::closeOverlays()
{
    dispose (menu);
    dispose (toastBox);
    dispose (modal);
    signInFlow = nullptr;
}

void HubComponent::flash (const String& t)
{
    flashText = t;
    flashUntil = now() + 2.8;
    repaint();
}

void HubComponent::toast (const Model::Note& n)
{
    struct Toast final : Overlay
    {
        Model::Note note;
        Image logo;
        IconButton x { Icon::close, Style::dark };
        HubButton go { "SHOW ME", Style::hot, 9.5f };
        Toast()
        {
            x.flat = true;
            x.iconColour = look::muted;
            go.glow = false;
            addAndMakeVisible (x);
            addAndMakeVisible (go);
        }
        void paint (Graphics& g) override
        {
            look::metal (g, getLocalBounds().toFloat().reduced (1.0f), 10.0f);
            if (logo.isValid())
            {
                g.setOpacity (1.0f);
                g.drawImage (logo, Rectangle<float> (14, 16, 36, 36), RectanglePlacement::centred);
            }
            g.setColour (look::text);
            g.setFont (look::font (12.4f, 700, 0.05f));
            g.drawText (note.title, 62, 12, getWidth() - 100, 20, Justification::centredLeft, true);
            g.setColour (look::muted);
            g.setFont (look::font (10.7f));
            g.drawText (note.text, 62, 31, getWidth() - 100, 16, Justification::centredLeft, true);
        }
        void resized() override
        {
            x.setBounds (getWidth() - 36, 12, 24, 24);
            go.setBounds (62, getHeight() - 36, 84, 24);
        }
    };
    auto t = std::make_unique<Toast>();
    t->note = n;
    t->logo = kiruLogo;
    t->x.onClick = [this] { dispose (toastBox); };
    t->go.onClick = [this, n] { dispose (toastBox); openNote (n); };
    t->setBounds (W - 91 - 351, 630 - 92, 351, 92);
    addAndMakeVisible (*t);
    dispose (toastBox);
    toastBox = std::move (t);
    toastUntil = now() + 9.0;
}

void HubComponent::toggleMenu()
{
    if (menu != nullptr)
    {
        dispose (menu);
        return;
    }
    dispose (toastBox);
    struct Menu final : Overlay
    {
        Viewport view;
        Column col;
        std::vector<std::unique_ptr<Component>> owned;
        Menu() { view.setViewedComponent (&col, false); view.setScrollBarsShown (true, false); view.setScrollBarThickness (6); addAndMakeVisible (view); }
        void paint (Graphics& g) override { look::metal (g, getLocalBounds().toFloat().reduced (1.0f), 10.0f); }
        void resized() override
        {
            view.setBounds (getLocalBounds().reduced (9));
            col.setSize (view.getWidth() - 8, col.heightFor (view.getWidth() - 8));
        }
    };
    auto m = std::make_unique<Menu>();
    auto& col = m->col;
    auto* menuPtr = m.get();
    col.gap = 5;
    col.onRelayout = [menuPtr] { menuPtr->resized(); };
    col.add (std::make_unique<TextItem> ("NOTIFICATIONS", opts (11.0f, 700, look::muted, false, 0.16f)));
    const auto notes = model.notes();
    const auto seen = model.settings.list ("seenNotes");
    for (auto& n : notes)
    {
        const bool isNews = n.id.startsWith ("news-");
        if (dev && isNews)
        {
            // edit the notification in place
            int idx = -1;
            for (int i = 0; i < (int) model.news.size(); ++i)
                if ("news-" + slugOf (model.news[(size_t) i].title) + "-" + model.news[(size_t) i].date == n.id) idx = i;
            if (idx < 0) continue;
            auto box = std::make_unique<Column>();
            box->padding = BorderSize<int> (8, 10, 8, 10);
            box->gap = 4;
            box->background = [] (Graphics& g, Rectangle<float> r) { g.setColour (Colour (0xff0c090b)); g.fillRoundedRectangle (r, 6.0f); g.setColour (Colours::black); g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f); };
            auto& item = model.news[(size_t) idx];
            box->add (text (item.title, opts (12.0f, 700, look::text, false, 0.04f), [this, idx] (const String& v) { if (idx < (int) model.news.size() && v.isNotEmpty()) model.news[(size_t) idx].title = v; }, "news-t" + String (idx)));
            box->add (text (item.text, opts (10.5f, 500, look::muted), [this, idx] (const String& v) { if (idx < (int) model.news.size()) model.news[(size_t) idx].text = v; }, "news-s" + String (idx)));
            auto row = std::make_unique<RowItem>();
            auto lab = std::make_unique<TextItem> ("OPENS", opts (9.5f, 600, look::muted, false, 0.08f));
            auto combo = std::make_unique<ComboBox>();
            combo->addItemList ({ "EMULON X", "EMULON X SD", "SKINS", "BANKS" }, 1);
            const StringArray values { "emulon-x", "sd", "skins", "banks" };
            const auto cur = item.open.upToFirstOccurrenceOf (":", false, false).trim();
            combo->setSelectedItemIndex (jmax (0, values.indexOf (cur)), dontSendNotification);
            combo->onChange = [this, idx, values, c = combo.get()] { if (idx < (int) model.news.size()) { model.news[(size_t) idx].open = values[c->getSelectedItemIndex()]; edited(); } };
            auto date = std::make_unique<TextItem> (item.date, opts (9.5f, 500, look::muted));
            auto rm = std::make_unique<HubButton> ("REMOVE", Style::grey, 8.5f);
            rm->textColour = Colour (0xffff8a8a);
            rm->onClick = [this, idx]
            {
                if (idx >= (int) model.news.size()) return;
                model.news.erase (model.news.begin() + idx);
                MessageManager::callAsync ([this] { dispose (menu); edited(); toggleMenu(); });
            };
            row->add (lab.get(), 44, 22); row->add (combo.get(), 120, 22); row->add (date.get(), 70, 22); row->add (rm.get(), 70, 22);
            m->owned.push_back (std::move (lab)); m->owned.push_back (std::move (combo)); m->owned.push_back (std::move (date)); m->owned.push_back (std::move (rm));
            box->add (std::move (row));
            col.add (std::move (box));
            continue;
        }
        auto row = std::make_unique<Painted>();
        const bool unread = ! seen.contains (n.id);
        row->h = [] (int) { return 48; };
        row->setMouseCursor (MouseCursor::PointingHandCursor);
        row->onClick = [this, n] { MessageManager::callAsync ([this, n] { openNote (n); }); };
        row->draw = [n, unread] (Graphics& g, Rectangle<float> r)
        {
            g.setColour (Colour (0xff0c090b));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (Colours::black);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
            look::dot (g, { r.getX() + 14.0f, r.getY() + 16.0f }, n.automatic ? look::ok : look::red);
            g.setColour (look::text);
            g.setFont (look::font (12.0f, 700, 0.04f));
            g.drawText (n.title, r.withTrimmedLeft (28).withHeight (26).translated (0, 4), Justification::centredLeft, true);
            g.setColour (look::muted);
            g.setFont (look::font (10.4f));
            g.drawText (n.text + (unread ? "  - NEW" : ""), r.withTrimmedLeft (28).withTrimmedTop (26).withHeight (16), Justification::centredLeft, true);
        };
        col.add (std::move (row));
    }
    if (notes.empty())
        col.add (std::make_unique<TextItem> ("Nothing new right now.", opts (11.0f, 500, look::muted)));
    if (dev)
    {
        auto add = std::make_unique<HubButton> ("+ ADD NOTIFICATION", Style::grey, 9.5f);
        add->onClick = [this]
        {
            model.news.push_back ({ "New announcement", "One short line", today(), "emulon-x" });
            MessageManager::callAsync ([this] { dispose (menu); edited(); toggleMenu(); });
        };
        auto row = std::make_unique<RowItem>();
        row->add (add.get(), 160, 26);
        m->owned.push_back (std::move (add));
        col.add (std::move (row));
    }
    col.add (std::make_unique<TextItem> ("These also pop up in Windows while the Hub sits quietly in the tray (you can turn that off with the gear button).", opts (10.0f, 500, look::muted)));
    const int h = jmin (460, col.heightFor (372) + 18);
    m->setBounds (W - 80 - 390, 130, 390, h);
    addAndMakeVisible (*m);
    menu = std::move (m);
    model.markAllSeen();
}

void HubComponent::showDialog (std::unique_ptr<Dialog> d)
{
    struct Modal final : Overlay
    {
        std::unique_ptr<Dialog> dialog;
        void paint (Graphics& g) override { g.fillAll (Colours::black.withAlpha (0.7f)); }
        void resized() override
        {
            if (dialog == nullptr) return;
            const int w = jmin (dialog->width, roundToInt (getWidth() * 0.9f));
            const int h = jmin (dialog->preferredHeight(), roundToInt (getHeight() * 0.92f));
            dialog->setBounds (Rectangle<int> (w, h).withCentre (getLocalBounds().getCentre()));
        }
        void mouseUp (const MouseEvent& e) override
        {
            if (dialog != nullptr && ! dialog->busy && ! dialog->getBounds().contains (e.getPosition()) && onClose)
                onClose();
        }
    };
    auto m = std::make_unique<Modal>();
    m->dialog = std::move (d);
    m->addAndMakeVisible (*m->dialog);
    m->onClose = [this] { closeDialog(); };
    m->setBounds (getLocalBounds());
    addAndMakeVisible (*m);
    if (modal != nullptr)
    {
        // the old dialog may be running one of its own buttons right now: delete it a moment later
        auto* old = modal.release();
        old->setVisible (false);
        MessageManager::callAsync ([old] { delete old; });
    }
    modal = std::move (m);
    dispose (menu);
}

void HubComponent::closeDialog()
{
    if (modal == nullptr)
        return;
    Overlay* which = modal.get();
    which->setVisible (false);
    MessageManager::callAsync ([safe = SafePointer<HubComponent> (this), which]
    {
        if (safe != nullptr && safe->modal.get() == which)
        {
            safe->modal = nullptr;
            safe->signInFlow = nullptr;
        }
    });
}

void HubComponent::message (const String& heading, const String& t)
{
    auto d = std::make_unique<Dialog> (heading);
    d->width = 520;
    d->para (t);
    d->action ("OK", Style::hot, [this] { closeDialog(); });
    showDialog (std::move (d));
}

//== Launching ======================================================================
void HubComponent::launch (const String& key)
{
    const auto app = model.appFile (key);
    if (! app.exists())
    {
        message ("CAN'T FIND IT", key == "emx" ? String ("The Emulon X standalone app isn't where it was installed. Reinstall Emulon X (with the standalone app) to fix it.")
                                               : String ("Emulon X SD isn't where it was installed. Reinstall it to fix it."));
        return;
    }
    if (app.startAsProcess())
        flash (key == "emx" ? String ("Starting Emulon X...") : String ("Starting Emulon X SD..."));
    else
        message ("CAN'T START IT", "Windows didn't start " + app.getFullPathName() + ".");
}

//== Installing =====================================================================
void HubComponent::install (const String& key)
{
    if (model.job().active)
    {
        flash ("Please wait until the current download is done.");
        return;
    }
    if (key == "test:hub")
    {
        auto d = std::make_unique<Dialog> ("HUB TEST BUILD", "draft release - only you can see it");
        d->width = 540;
        d->para ("Install the test build of the Hub (" + model.testHub.version + ")? The Hub closes, installs it and opens again. "
                 "When you're happy with it, publish the draft release on GitHub and everyone gets it.");
        d->action ("CANCEL", Style::grey, [this] { closeDialog(); });
        d->action ("INSTALL TEST BUILD", Style::hot, [this]
        {
            closeDialog();
            model.installApp ("hub", {}, [this] (bool ok, const String& msg)
            {
                if (ok && msg == "hub-restart") { if (onQuitForUpdate) onQuitForUpdate(); }
                else if (! ok) message ("THE TEST BUILD DIDN'T INSTALL", msg);
            }, true);
        });
        showDialog (std::move (d));
        return;
    }
    if (key == "hub")
    {
        if (! model.relHub.found || ! isNewer (model.relHub.version, hubVersion))
        {
            message ("EMULON X HUB", String ("You have the newest Hub (") + hubVersion + ").");
            return;
        }
        auto d = std::make_unique<Dialog> ("UPDATE THE HUB");
        d->width = 520;
        d->para ("Emulon X Hub " + model.relHub.version + " is ready. The Hub closes, updates itself and opens again.");
        d->action ("LATER", Style::grey, [this] { closeDialog(); });
        d->action ("UPDATE NOW", Style::hot, [this]
        {
            closeDialog();
            model.installApp ("hub", {}, [this] (bool ok, const String& msg)
            {
                if (ok && msg == "hub-restart") { if (onQuitForUpdate) onQuitForUpdate(); }
                else if (! ok) message ("THE UPDATE DIDN'T WORK", msg);
            });
        });
        showDialog (std::move (d));
        return;
    }
    openWhere (key);
}

void HubComponent::openWhere (const String& keyIn)
{
    const bool test = keyIn.startsWith ("test:");
    const auto key = test ? keyIn.substring (5) : keyIn;
    String title;
    String size;
    if (key == "emx") { title = "Install Emulon X " + model.relEmx.version; size = formatBytes (model.relEmx.assetSize); }
    else if (key == "sd") { title = "Install Emulon X SD " + model.relSd.version; size = formatBytes (model.relSd.assetSize); }
    else if (key.startsWith ("skin:")) { for (auto& s : model.skins) if ("skin:" + s.id == key) { title = "Install the skin " + s.name; size = s.size; } }
    else if (key.startsWith ("bank:")) { for (auto& b : model.banks) if ("bank:" + b.id == key) { title = "Download the bank " + b.name; size = b.sizeText(); } }
    if (key == "emx" && model.installedVersion ("emx").isNotEmpty() && isNewer (model.relEmx.version, model.installedVersion ("emx")))
        title = "Update Emulon X to " + model.relEmx.version;
    if (test)
    {
        const auto& rel = model.release (key, true);
        title = "Install the TEST build " + rel.version + " of " + (key == "emx" ? String ("Emulon X") : String ("Emulon X SD"))
                + " (a draft release - only you can see it)";
        size = formatBytes (rel.assetSize);
    }

    auto d = std::make_unique<Dialog> ("WHERE SHOULD IT GO?");
    d->para (title + (size.isNotEmpty() ? " " + middot + " " + size : String()));
    std::vector<FolderRow*> rows;
    for (auto& p : model.placesFor (key))
    {
        const bool installedHere = model.installedVersion (key).isNotEmpty();
        auto row = std::make_unique<FolderRow> (p.label, installedHere ? String ("It's installed here now, so this is filled in for you.") : p.note, p.folder);
        auto* r = row.get();
        row->browse.onClick = [this, r]
        {
            chooser = std::make_unique<FileChooser> ("Pick a folder", File (r->path.getText()).exists() ? File (r->path.getText()) : File::getSpecialLocation (File::userDocumentsDirectory));
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories, [r] (const FileChooser& fc)
            {
                if (fc.getResult() != File())
                    r->path.setText (fc.getResult().getFullPathName(), false);
            });
        };
        rows.push_back (r);
        d->body.add (std::move (row));
    }
   #if JUCE_WINDOWS
    if (key == "emx" || key == "sd")
        d->para ("Windows asks for permission to install (the installer needs to write to Program Files). The installer runs quietly in the background.", true);
   #endif
    d->action ("CANCEL", Style::grey, [this] { closeDialog(); });
    d->action (test ? "INSTALL TEST BUILD" : "INSTALL HERE", Style::hot, [this, key, rows, test]
    {
        Array<File> folders;
        for (auto* r : rows)
        {
            const auto t = r->path.getText().trim();
            if (! File::isAbsolutePath (t))
            {
                flash ("Please pick a folder, like C:\\Something");
                return;
            }
            folders.add (File (t));
        }
        if (key != "emx" && key != "sd")
        {
            const auto dir = folders[0];
            if (! dir.createDirectory() || ! dir.hasWriteAccess())
            {
                flash ("That folder can't be written to. Please pick another one.");
                return;
            }
        }
        closeDialog();
        auto done = [this, key] (bool ok, const String& msg)
        {
            if (! ok)
                message ("THAT DIDN'T WORK", msg);
            else
            {
                const auto what = key == "emx" ? String ("Emulon X") : key == "sd" ? String ("Emulon X SD") : key.startsWith ("skin:") ? String ("The skin") : String ("The bank");
                flash (msg.isNotEmpty() && ! CharacterFunctions::isDigit (msg[0]) ? msg : what + " is installed.");
            }
            rebuild();
        };
        if (key == "emx" || key == "sd")
            model.installApp (key, folders, done, test);
        else if (key.startsWith ("skin:"))
        {
            for (auto& s : model.skins) if ("skin:" + s.id == key) { model.installSkin (s, folders[0], done); break; }
        }
        else
            for (auto& b : model.banks) if ("bank:" + b.id == key) { model.installBank (b, folders[0], done); break; }
    });
    showDialog (std::move (d));
}

//== Settings =======================================================================
void HubComponent::openSettings()
{
    auto d = std::make_unique<Dialog> ("SETTINGS", String ("Emulon X Hub ") + hubVersion);
    d->width = 560;
    auto add = [&d] (const String& t, const String& n, bool on, std::function<void (bool)> f)
    {
        auto row = std::make_unique<SwitchRow> (t, n, on);
        row->onChange = std::move (f);
        d->body.add (std::move (row));
    };
    add ("Notifications", "Pop-ups for updates, new skins and banks", model.settings.flag ("notify", true), [this] (bool v) { model.settings.set ("notify", v); });
    add ("Keep running in the tray", "Closing the window keeps the Hub in the tray, so it can tell you about news", model.settings.flag ("tray", true),
         [this] (bool v) { model.settings.set ("tray", v); if (onSettingsChanged) onSettingsChanged(); });
   #if JUCE_WINDOWS
    add ("Start with Windows", "Opens quietly in the tray when you log in", model.settings.flag ("autostart", false),
         [this] (bool v) { model.settings.set ("autostart", v); if (onSettingsChanged) onSettingsChanged(); });
   #endif
    const auto login = model.settings.get ("githubLogin");
    if (Net::get().getToken().isNotEmpty())
        d->para ("GitHub: signed in" + (login.isNotEmpty() ? " as **" + login + "**" : String()) + ". The sign-in is only used to submit skins (and for dev mode).", true);
    else
        d->para ("GitHub: not signed in. You only need to sign in to submit a skin.", true);
    if (Net::get().getToken().isNotEmpty())
        d->action ("SIGN OUT OF GITHUB", Style::grey, [this]
        {
            Net::get().setToken ({});
            model.settings.set ("githubToken", "");
            model.settings.set ("githubLogin", "");
            setDevMode (false, {});
            closeDialog();
            flash ("Signed out of GitHub.");
        });
    if (dev && model.testHub.found)
        d->action ("HUB TEST BUILD " + model.testHub.version, Style::grey, [this] { closeDialog(); install ("test:hub"); });
    d->action ("CHECK FOR HUB UPDATES", Style::grey, [this] { closeDialog(); install ("hub"); });
    d->action ("DONE", Style::hot, [this] { closeDialog(); });
    showDialog (std::move (d));
}

//== GitHub sign-in, dev mode =======================================================
void HubComponent::signIn (std::function<void (String)> then)
{
    auto withUser = [this, then]
    {
        Net::get().api ("GET", "/user", var(), [this, then] (const Response& r)
        {
            const auto login = r.json().getProperty ("login", "").toString();
            if (! r.ok() || login.isEmpty())
            {
                // the saved sign-in ran out: sign in again
                Net::get().setToken ({});
                model.settings.set ("githubToken", "");
                signIn (then);
                return;
            }
            model.settings.set ("githubLogin", login);
            then (login);
        });
    };
    if (Net::get().getToken().isNotEmpty())
    {
        withUser();
        return;
    }
    if (model.clientId.isEmpty())
    {
        message ("SIGN IN WITH GITHUB", "Signing in isn't set up yet: the Hub needs its GitHub app ID in hub/hub.json (\"githubClientId\"). Kiru: create an OAuth app on GitHub (Settings > Developer settings > OAuth Apps), tick \"Enable Device Flow\", and put its Client ID there.");
        return;
    }
    auto d = std::make_unique<Dialog> ("SIGN IN WITH GITHUB");
    d->width = 540;
    auto& info = d->para ("Asking GitHub for a code...");
    auto codeBox = std::make_unique<Painted>();
    auto* code = codeBox.get();
    code->h = [] (int) { return 0; };
    d->body.add (std::move (codeBox));
    d->para ("No password or key is stored in the Hub - GitHub gives it a sign-in that you can take back any time in your GitHub settings (Applications).", true);
    auto* dialog = d.get();
    d->action ("CANCEL", Style::grey, [this] { closeDialog(); });
    auto& open = d->action ("OPEN GITHUB", Style::hot, {});
    open.setEnabled (false);
    showDialog (std::move (d));

    signInFlow = std::make_unique<GitHubSignIn>();
    signInFlow->onCode = [this, &info, code, &open, dialog] (const String& userCode, const String& url)
    {
        info.setText ("Type this code on the GitHub page that just opened (it's also copied, so you can paste it). Then come back here.");
        code->h = [] (int) { return 64; };
        code->draw = [userCode] (Graphics& g, Rectangle<float> r)
        {
            look::led (g, r.withSizeKeepingCentre (300, r.getHeight()), 8.0f);
            g.setColour (look::red);
            g.setFont (look::font (30.0f, 900, 0.25f));
            g.drawText (userCode, r, Justification::centred);
        };
        open.setEnabled (true);
        open.onClick = [userCode, url] { SystemClipboard::copyTextToClipboard (userCode); URL (url).launchInDefaultBrowser(); };
        SystemClipboard::copyTextToClipboard (userCode);
        URL (url).launchInDefaultBrowser();
        if (modal != nullptr) modal->resized();
        dialog->resized();
        dialog->repaint();
    };
    signInFlow->onDone = [this, withUser] (bool ok, const String& tokenOrError)
    {
        if (! ok)
        {
            message ("SIGN IN WITH GITHUB", "That didn't work: " + tokenOrError);
            return;
        }
        Net::get().setToken (tokenOrError);
        model.settings.set ("githubToken", tokenOrError);
        closeDialog();
        withUser();
    };
    signInFlow->start (model.clientId, "public_repo");
}

void HubComponent::devLogin()
{
    auto d = std::make_unique<Dialog> ("DEVELOPER LOGIN");
    d->width = 560;
    d->para ("Dev mode lets you edit the Hub's texts and pictures right here, and save them straight to the **hub** folder in the "
             + String (repoOwner) + "/" + mainRepo + " repository.");
    d->para ("You sign in with GitHub in your browser. Only accounts that can change the repository get dev mode.", true);
    d->action ("CANCEL", Style::grey, [this] { closeDialog(); });
    d->action ("SIGN IN WITH GITHUB", Style::hot, [this]
    {
        closeDialog();
        MessageManager::callAsync ([this]
        {
            signIn ([this] (String login)
            {
                Net::get().api ("GET", "/repos/" + String (repoOwner) + "/" + mainRepo, var(), [this, login] (const Response& r)
                {
                    const bool push = (bool) r.json().getProperty ("permissions", var()).getProperty ("push", false);
                    if (! push)
                    {
                        message ("DEVELOPER LOGIN", "Signed in as " + login + ", but this account can't change " + String (repoOwner) + "/" + mainRepo + ", so dev mode stays off. You can still submit skins.");
                        return;
                    }
                    setDevMode (true, login);
                    refreshNow (false);   // now the test builds (draft releases) show up too
                    flash ("Signed in - dev mode on");
                });
            });
        });
    });
    showDialog (std::move (d));
}

void HubComponent::setDevMode (bool on, const String& user)
{
    dev = on;
    devUser = user;
    rebuild();
}

void HubComponent::pickPicture (std::function<void (String)> done)
{
    chooser = std::make_unique<FileChooser> ("Pick a picture (PNG or JPG, up to 6 MB)", File::getSpecialLocation (File::userPicturesDirectory), "*.png;*.jpg;*.jpeg");
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this, done] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f == File()) return;
        const auto name = model.addPicture (f);
        if (name.isEmpty())
            flash ("Pictures up to 6 MB, please (PNG or JPG).");
        else
            done (name);
    });
}

void HubComponent::openSave()
{
    const auto files = model.changedFiles();
    StringArray pics;
    for (auto& [name, data] : model.newPictures)
    {
        bool used = false;
        for (auto& f : Model::textFiles())
            used = used || model.fileText (f).contains (name);
        if (used) pics.add (name);
    }
    if (files.isEmpty() && pics.isEmpty())
    {
        flash ("Nothing to save.");
        return;
    }
    auto d = std::make_unique<Dialog> ("SAVE TO GITHUB", String (repoOwner) + "/" + mainRepo + " - hub folder");
    d->width = 720;
    auto preview = std::make_shared<TextEditor>();
    preview->setMultiLine (true, false);
    preview->setReadOnly (true);
    preview->setScrollbarsShown (true);
    styleEditor (*preview, Font (FontOptions (Font::getDefaultMonospacedFontName(), 12.0f, Font::plain)), true);
    preview->setColour (TextEditor::textColourId, Colour (0xffffd0d0));
    auto tabsRow = std::make_unique<RowItem>();
    auto tabButtons = std::make_shared<OwnedArray<HubButton>>();
    for (auto& f : files)
    {
        auto* b = tabButtons->add (new HubButton ("hub/" + f, Style::grey, 9.0f));
        b->onClick = [this, f, preview, tabButtons, b]
        {
            for (auto* x : *tabButtons) x->setToggleState (x == b, dontSendNotification);
            preview->setText (model.fileText (f), false);
        };
        tabsRow->add (b, b->idealWidth() + 8, 24);
    }
    for (auto& p : pics)
    {
        auto* b = tabButtons->add (new HubButton ("hub/images/" + p + " - new picture", Style::dark, 9.0f));
        b->setEnabled (false);
        tabsRow->add (b, b->idealWidth() + 8, 24);
    }
    if (files.size() > 0)
    {
        tabButtons->getFirst()->setToggleState (true, dontSendNotification);
        preview->setText (model.fileText (files[0]), false);
    }
    d->body.add (std::move (tabsRow));
    if (files.size() > 0)
        d->body.add (std::make_unique<Wrap> (preview.get(), 230));
    auto msg = std::make_shared<TextEditor>();
    styleEditor (*msg, look::font (13.0f), true);
    msg->setText ("Update Hub texts", false);
    msg->setInputRestrictions (72);
    d->body.add (std::make_unique<TextItem> ("WHAT DID YOU CHANGE?", opts (10.4f, 600, look::muted, false, 0.12f)));
    d->body.add (std::make_unique<Wrap> (msg.get(), 32), 4);
    d->para ("These files are saved to GitHub with your account. Every Hub shows the new text the next time it starts.", true);
    auto* dialog = d.get();
    d->action ("CANCEL", Style::grey, [this] { closeDialog(); });
    const int n = files.size() + pics.size();
    auto& save = d->action ("SAVE " + String (n) + " FILE" + (n == 1 ? "" : "S"), Style::hot, {});
    save.onClick = [this, files, pics, msg, &save, dialog, preview, tabButtons]
    {
        std::vector<RepoFile> out;
        for (auto& f : files)
        {
            const auto t = model.fileText (f);
            out.push_back ({ "hub/" + f, MemoryBlock (t.toRawUTF8(), t.getNumBytesAsUTF8()) });
        }
        for (auto& p : pics)
            out.push_back ({ "hub/images/" + p, model.newPictures[p] });
        save.setEnabled (false);
        save.setButtonText ("SAVING...");
        save.progress = 0.3f;
        dialog->busy = true;
        auto message_ = msg->getText().trim().isEmpty() ? String ("Update Hub texts") : msg->getText().trim();
        commitFiles (repoOwner, mainRepo, "main", out, message_, [this, pics] (bool ok, String error)
        {
            if (! ok)
            {
                message ("SAVING DIDN'T WORK", error);
                return;
            }
            for (auto& p : pics) model.newPictures.erase (p);
            model.snapshot();
            closeDialog();
            rebuild();
            flash ("Saved to GitHub - everyone sees it next launch");
        });
    };
    dialog->keepAlive = { preview, msg, tabButtons };
    showDialog (std::move (d));
}

//== Submitting a skin ==============================================================
void HubComponent::openSubmit (const File& start, bool skipSignIn)
{
    if (! skipSignIn && Net::get().getToken().isEmpty())
    {
        auto d = std::make_unique<Dialog> ("SUBMIT A SKIN");
        d->width = 540;
        d->para ("To send a skin, sign in with your GitHub account first (it's free). Your skin is sent from your account, and your GitHub name goes on it as the author.");
        d->action ("CANCEL", Style::grey, [this] { closeDialog(); });
        d->action ("SIGN IN WITH GITHUB", Style::hot, [this, start]
        {
            closeDialog();
            MessageManager::callAsync ([this, start] { signIn ([this, start] (String) { openSubmit (start, true); }); });
        });
        showDialog (std::move (d));
        return;
    }
    struct State
    {
        File file;
        Image preview;
        String error;
        std::unique_ptr<TextEditor> name, desc, path;
    };
    auto st = std::make_shared<State>();
    auto d = std::make_unique<Dialog> ("SUBMIT A SKIN", "to the EmulonX-Skins collection");
    d->width = 760;
    st->path = std::make_unique<TextEditor>();
    st->name = std::make_unique<TextEditor>();
    st->desc = std::make_unique<TextEditor>();
    styleEditor (*st->path, look::font (12.0f), true);
    st->path->setReadOnly (true);
    st->path->setTextToShowWhenEmpty ("Pick the .emxd file you saved in Emulon X SD", look::red.withAlpha (0.4f));
    styleEditor (*st->name, look::font (13.0f), true);
    st->name->setInputRestrictions (60);
    styleEditor (*st->desc, look::font (12.0f), true);
    st->desc->setMultiLine (true, true);
    st->desc->setReturnKeyStartsNewLine (true);
    st->desc->setInputRestrictions (300);
    st->desc->setTextToShowWhenEmpty ("A sentence or two about your skin", look::red.withAlpha (0.4f));

    d->para ("Send a skin you made in **Emulon X SD**. Kiru checks it, and once it's added everyone can get it on the EMULON SKINS tab. You sign in with GitHub to send it.", true);
    auto fileRow = std::make_unique<RowItem>();
    auto browse = std::make_shared<HubButton> ("BROWSE...", Style::grey, 9.5f);
    fileRow->add (st->path.get(), 600, 32);
    fileRow->add (browse.get(), 100, 32);
    d->body.add (std::make_unique<TextItem> ("SKIN FILE (.emxd)", opts (10.4f, 600, look::muted, false, 0.12f)));
    d->body.add (std::move (fileRow), 4);

    auto pic = std::make_unique<Painted>();
    auto* picPtr = pic.get();
    pic->h = [st] (int) { return st->file == File() ? 0 : 200; };
    pic->draw = [st] (Graphics& g, Rectangle<float> r)
    {
        const auto area = r.withWidth (r.getHeight() * 880.0f / 540.0f);
        look::led (g, area, 6.0f, false);
        if (st->preview.isValid())
        {
            g.setOpacity (1.0f);
            g.drawImage (st->preview, area.reduced (2.0f), RectanglePlacement::centred);
        }
        auto t = r.withTrimmedLeft (area.getWidth() + 14.0f);
        g.setColour (st->error.isEmpty() ? look::muted : Colour (0xffff8a8a));
        AttributedString s;
        s.append (st->error.isEmpty() ? String ("PREVIEW\nThis picture is made from your skin and sent with it.") : st->error, look::font (10.6f), st->error.isEmpty() ? look::muted : Colour (0xffff8a8a));
        TextLayout tl;
        tl.createLayout (s, t.getWidth());
        tl.draw (g, t);
    };
    d->body.add (std::move (pic));
    d->body.add (std::make_unique<TextItem> ("NAME", opts (10.4f, 600, look::muted, false, 0.12f)));
    d->body.add (std::make_unique<Wrap> (st->name.get(), 32), 4);
    d->body.add (std::make_unique<TextItem> ("DESCRIPTION", opts (10.4f, 600, look::muted, false, 0.12f)));
    d->body.add (std::make_unique<Wrap> (st->desc.get(), 60), 4);
    auto& status = d->para ("Skins are shared under the CC BY 4.0 licence: anyone may use and change them, and your name stays on it.", true);
    auto* dialog = d.get();

    auto load = [st, picPtr, dialog, this] (const File& f)
    {
        st->file = f;
        st->error = {};
        st->path->setText (f.getFullPathName(), false);
        if (f.getSize() > 20 * 1024 * 1024)
            st->error = "This skin is bigger than 20 MB. Please use smaller pictures in Emulon X SD.";
        emx::sd::SkinDocument doc;
        String err;
        if (st->error.isEmpty() && ! doc.open (f, err))
            st->error = "This file can't be read as a skin: " + err;
        if (st->error.isEmpty())
        {
            if (auto design = doc.design())
            {
                emx::sd::PreviewView view (doc);
                Image img (Image::ARGB, emx::sd::PreviewView::logicalWidth, emx::sd::PreviewView::logicalHeight, true);
                {
                    Graphics g (img);
                    view.drawPreview (g, *design);
                }
                st->preview = img;
                if (st->name->getText().isEmpty())
                    st->name->setText (design->name.isNotEmpty() ? design->name : f.getFileNameWithoutExtension(), false);
            }
        }
        dialog->body.relayout();
        if (modal != nullptr) modal->resized();
        dialog->resized();
        picPtr->repaint();
    };
    browse->onClick = [this, load]
    {
        chooser = std::make_unique<FileChooser> ("Pick your skin", File::getSpecialLocation (File::userDocumentsDirectory).getChildFile ("Emulon X").getChildFile ("Skins"), "*.emxd");
        chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [load] (const FileChooser& fc)
        {
            if (fc.getResult() != File()) load (fc.getResult());
        });
    };
    d->action ("CANCEL", Style::grey, [this] { closeDialog(); });
    auto& send = d->action ("SEND FOR REVIEW", Style::hot, {});
    send.onClick = [this, st, &status, &send, dialog]
    {
        if (st->file == File() || st->preview.isNull()) { flash ("Please pick your .emxd skin file first."); return; }
        if (st->error.isNotEmpty()) { flash ("This skin can't be sent - see the note next to the preview."); return; }
        const auto name = st->name->getText().trim();
        if (name.isEmpty() || slugOf (name).isEmpty()) { flash ("Please give your skin a name."); return; }
        auto login = model.settings.get ("githubLogin");
        if (Net::get().getToken().isEmpty() || login.isEmpty())
        {
            flash ("Please sign in with GitHub first (gear button).");
            return;
        }
        {
            MemoryBlock skinData, png;
            st->file.loadFileAsData (skinData);
            {
                MemoryOutputStream out (png, false);
                PNGImageFormat().writeImageToStream (st->preview, out);
            }
            if (png.getSize() > 2 * 1024 * 1024)
            {
                png.reset();
                MemoryOutputStream out (png, false);
                PNGImageFormat().writeImageToStream (st->preview.rescaled (660, 405, Graphics::highResamplingQuality), out);
            }
            auto* info = new DynamicObject();
            info->setProperty ("name", name);
            info->setProperty ("author", login);
            info->setProperty ("description", st->desc->getText().trim());
            info->setProperty ("version", "1.0");
            const auto infoText = JSON::toString (var (info), false) + "\n";
            const auto slug = slugOf (name).substring (0, 40) + "-" + slugOf (login).substring (0, 7);
            std::vector<RepoFile> files {
                { "skins/" + slug + "/skin.emxd", skinData },
                { "skins/" + slug + "/preview.png", png },
                { "skins/" + slug + "/info.json", MemoryBlock (infoText.toRawUTF8(), infoText.getNumBytesAsUTF8()) }
            };
            const auto body = "**" + name + "** by @" + login + "\n\n" + st->desc->getText().trim()
                              + "\n\nSent from the Emulon X Hub " + hubVersion + ". Made in Emulon X SD.";
            dialog->busy = true;
            send.setEnabled (false);
            send.progress = 0.1f;
            SafePointer<Component> safeDialog (dialog);
            submitSkin (slug, files, name, body,
                [safeDialog, &status, &send] (const String& step)
                {
                    if (safeDialog == nullptr) return;
                    status.setText (step);
                    send.progress = jmin (0.9f, send.progress + 0.2f);
                    send.repaint();
                },
                [this, safeDialog, name] (bool ok, String urlOrError)
                {
                    if (safeDialog == nullptr) return;
                    closeDialog();
                    MessageManager::callAsync ([this, ok, urlOrError, name]
                    {
                        if (! ok)
                        {
                            message ("THE SKIN WASN'T SENT", urlOrError);
                            return;
                        }
                        auto d2 = std::make_unique<Dialog> ("THANK YOU!");
                        d2->width = 540;
                        d2->para ("**" + name + "** is sent in for review. Once it's added, it shows up on the EMULON SKINS tab for everyone, and you'll find a notification here.");
                        d2->action ("SEE IT ON GITHUB", Style::grey, [urlOrError] { URL (urlOrError).launchInDefaultBrowser(); });
                        d2->action ("DONE", Style::hot, [this] { closeDialog(); });
                        showDialog (std::move (d2));
                    });
                });
        }
    };
    dialog->keepAlive = { st, browse };
    showDialog (std::move (d));
    if (start != File())
        load (start);
}
} // namespace emx::hub
