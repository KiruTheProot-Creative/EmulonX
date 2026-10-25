// SPDX-License-Identifier: AGPL-3.0-or-later
#include "HubWidgets.h"

namespace emx::hub
{
using namespace juce;

//== Button =========================================================================
HubButton::HubButton (const String& label, look::Style s, float px) : Button (label), style (s), fontPx (px)
{
    setMouseCursor (MouseCursor::PointingHandCursor);
}

int HubButton::idealWidth() const
{
    GlyphArrangement ga;
    ga.addLineOfText (look::font (fontPx, 700, 0.1f), getButtonText(), 0, 0);
    return roundToInt (ga.getBoundingBox (0, -1, true).getWidth()) + (link ? 2 : roundToInt (fontPx * 2.2f));
}

void HubButton::paintButton (Graphics& g, bool hover, bool down)
{
    auto r = getLocalBounds().toFloat();
    const bool enabled = isEnabled();
    if (link)
    {
        g.setColour (hover ? look::text : look::list);
        g.setFont (look::font (fontPx, 500, 0.06f));
        g.drawText (getButtonText(), r, Justification::centredLeft, false);
        GlyphArrangement ga;
        ga.addLineOfText (look::font (fontPx, 500, 0.06f), getButtonText(), 0, 0);
        const auto w = jmin (r.getWidth(), ga.getBoundingBox (0, -1, true).getWidth());
        g.fillRect (r.getX(), r.getBottom() - 3.0f, w, 1.0f);
        return;
    }
    if (plain)
    {
        g.setColour (Colours::white.withAlpha (hover ? 0.9f : 0.45f));
        g.setFont (look::font (fontPx, 500, 0.14f));
        g.drawText (getButtonText(), r, Justification::centredLeft, false);
        return;
    }
    if (dashed)
    {
        g.setColour (Colour (0xff0b0809));
        g.fillRoundedRectangle (r.reduced (0.5f), (float) radius);
        drawDashed (g, r.reduced (0.5f), Colour (0xff8a5050));
        g.setColour (hover ? look::text : look::muted);
        g.setFont (look::font (fontPx, 700, 0.12f));
        g.drawText (getButtonText(), r, Justification::centred, false);
        return;
    }
    const auto st = getToggleState() && style == look::Style::grey ? look::Style::hot : style;
    look::buttonFace (g, r, st, hover && enabled, down && enabled, (float) radius, glow);
    if (progress >= 0.0f)
    {
        const auto fill = r.reduced (1.0f).withWidth ((r.getWidth() - 2.0f) * jlimit (0.0f, 1.0f, progress));
        g.setGradientFill (ColourGradient::horizontal (Colours::white.withAlpha (0.08f), fill.getX(), Colours::white.withAlpha (0.28f), fill.getRight()));
        g.fillRoundedRectangle (fill, (float) radius - 1.0f);
    }
    auto tc = style == look::Style::quiet || style == look::Style::grey ? (getToggleState() ? Colours::white : Colour (0xffdddddd)) : textColour;
    if (hover && enabled) tc = Colours::white;
    if (! enabled && progress < 0.0f && style != look::Style::quiet) tc = tc.withAlpha (0.45f);
    g.setColour (tc);
    g.setFont (look::font (fontPx, 700, 0.12f));
    g.drawFittedText (getButtonText(), getLocalBounds().reduced (4, 0), Justification::centred, 1, 0.8f);
}

//== Text ===========================================================================
void drawDashed (Graphics& g, Rectangle<float> r, Colour c)
{
    Path p, d;
    p.addRoundedRectangle (r, 3.0f);
    const float dash[] { 4.0f, 3.0f };
    PathStrokeType (1.0f).createDashedStroke (d, p, dash, 2);
    g.setColour (c);
    g.fillPath (d);
}

TextEditor* styleEditor (TextEditor& e, const Font& f, bool led)
{
    e.setFont (f);
    e.setColour (TextEditor::backgroundColourId, led ? look::ledBg : Colour (0x1aff2a2a));
    e.setColour (TextEditor::textColourId, led ? look::red : look::text);
    e.setColour (TextEditor::highlightColourId, look::red.withAlpha (0.35f));
    e.setColour (TextEditor::highlightedTextColourId, Colours::white);
    e.setColour (CaretComponent::caretColourId, look::red);
    e.setColour (TextEditor::outlineColourId, Colours::black);
    e.setColour (TextEditor::focusedOutlineColourId, look::red);
    e.setIndents (5, 3);
    return &e;
}

TextItem::TextItem (const String& t, Options o) : text (t), opt (std::move (o))
{
    setInterceptsMouseClicks (false, true);
}

TextItem::~TextItem()
{
    if (editor != nullptr)
        editor->removeListener (this);
}

void TextItem::setText (const String& t)
{
    text = t;
    if (editor != nullptr)
        editor->setText (t, false);
    repaint();
}

String TextItem::shown() const { return opt.upper ? text.toUpperCase() : text; }

std::vector<TextLayout> TextItem::layoutFor (float width) const
{
    std::vector<TextLayout> out;
    StringArray paras;
    if (opt.paragraphs)
    {
        auto lines = StringArray::fromLines (shown());
        String cur;
        for (auto& l : lines)
        {
            if (l.trim().isEmpty())
            {
                if (cur.isNotEmpty()) paras.add (cur);
                cur = {};
            }
            else
                cur << (cur.isEmpty() ? "" : " ") << l.trim();
        }
        if (cur.isNotEmpty()) paras.add (cur);
    }
    else
        paras.add (shown().replace ("\n", " "));
    const auto bold = opt.font.boldened();
    for (auto& p : paras)
    {
        // the same size in the bold cut (look::font's height already includes Saira's own line height)
        auto s = look::rich (p, opt.font, look::font (opt.font.getHeight() / (1.25f * 1.3137f), 700), opt.colour, opt.boldColour);
        s.setJustification (opt.just);
        s.setWordWrap (AttributedString::byWord);
        TextLayout tl;
        tl.createLayout (s, jmax (10.0f, width));
        out.push_back (std::move (tl));
    }
    ignoreUnused (bold);
    return out;
}

int TextItem::heightFor (int width)
{
    if (editor != nullptr)
    {
        // the raw text, as typed
        AttributedString s;
        s.append (editor->getText().isEmpty() ? String ("X") : editor->getText() + (editor->getText().endsWith ("\n") ? "X" : ""), editor->getFont());
        s.setWordWrap (AttributedString::byWord);
        TextLayout tl;
        tl.createLayout (s, (float) width - 14.0f);
        return roundToInt (tl.getHeight()) + 10;
    }
    if (text.isEmpty())
        return 0;
    float h = 0;
    const auto gapPara = opt.font.getHeight() * 0.5f;
    for (auto& tl : layoutFor ((float) width))
        h += tl.getHeight() + gapPara;
    return roundToInt (h - gapPara + 1.0f);
}

void TextItem::paint (Graphics& g)
{
    if (editor != nullptr)
        return;
    if (opt.led)
        look::led (g, getLocalBounds().toFloat());
    float y = 0;
    const auto gapPara = opt.font.getHeight() * 0.5f;
    for (auto& tl : layoutFor ((float) getWidth()))
    {
        if (opt.glow)
        {
            Image glow (Image::ARGB, jmax (1, getWidth()), jmax (1, getHeight()), true);
            {
                Graphics gg (glow);
                tl.draw (gg, { 0.0f, y, (float) getWidth(), tl.getHeight() });
            }
            GlowEffect ge;
            ignoreUnused (ge);
            g.setColour (look::red.withAlpha (0.25f));
            g.drawImageAt (glow.rescaled (jmax (1, getWidth() / 3), jmax (1, getHeight() / 3)).rescaled (getWidth(), getHeight()), 0, 0, true);
        }
        tl.draw (g, { 0.0f, y, (float) getWidth(), tl.getHeight() });
        y += tl.getHeight() + gapPara;
    }
}

void TextItem::paintOverChildren (Graphics& g)
{
    if (editor != nullptr && ! editor->hasKeyboardFocus (true))
        drawDashed (g, getLocalBounds().toFloat().reduced (0.5f), Colour (0x8cff6e6e));
}

void TextItem::resized()
{
    if (editor != nullptr)
        editor->setBounds (getLocalBounds());
}

void TextItem::makeEditable (std::function<void (const String&)> c)
{
    commit = std::move (c);
    editor = std::make_unique<TextEditor>();
    const bool multi = opt.paragraphs || opt.multiline;
    editor->setMultiLine (true, true);
    editor->setReturnKeyStartsNewLine (multi);
    editor->setScrollbarsShown (false);
    styleEditor (*editor, opt.font, false);
    editor->setColour (TextEditor::backgroundColourId, Colours::transparentBlack);
    editor->setColour (TextEditor::outlineColourId, Colours::transparentBlack);
    editor->setTextToShowWhenEmpty (opt.placeholder, Colours::white.withAlpha (0.3f));
    editor->setText (text, false);
    editor->addListener (this);
    setInterceptsMouseClicks (true, true);
    addAndMakeVisible (*editor);
}

void TextItem::textEditorTextChanged (TextEditor&)
{
    const auto h = heightFor (getWidth());
    if (h != getHeight() && onHeightChanged)
        onHeightChanged();
    repaint();
}

void TextItem::textEditorReturnKeyPressed (TextEditor& e)
{
    if (! (opt.paragraphs || opt.multiline))
        e.unfocusAllComponents();
}

void TextItem::textEditorEscapeKeyPressed (TextEditor& e) { e.unfocusAllComponents(); }
void TextItem::textEditorFocusLost (TextEditor&) { doCommit(); repaint(); }

void TextItem::doCommit()
{
    if (editor == nullptr)
        return;
    auto v = editor->getText().replace ("\xc2\xa0", " ");
    if (opt.paragraphs || opt.multiline)
    {
        while (v.contains ("\n\n\n"))
            v = v.replace ("\n\n\n", "\n\n");
        v = v.trim();
    }
    else
        v = v.replace ("\r", "").replace ("\n", " ").trim();
    if (v == text)
        return;
    text = v;
    if (commit)
    {
        auto c = commit;
        auto value = v;
        MessageManager::callAsync ([c, value] { c (value); });
    }
}

//== Chips ==========================================================================
std::vector<Rectangle<int>> ChipsItem::place (int width) const
{
    std::vector<Rectangle<int>> out;
    int x = 0, y = 0;
    const int h = 22, gap = 5;
    auto put = [&] (int w)
    {
        if (x > 0 && x + w > width)
        {
            x = 0;
            y += h + gap;
        }
        out.push_back ({ x, y, w, h });
        x += w + gap;
    };
    for (auto& c : chips)
    {
        GlyphArrangement ga;
        ga.addLineOfText (look::font (10.4f, 700, 0.08f), c.text.toUpperCase(), 0, 0);
        put (roundToInt (ga.getBoundingBox (0, -1, true).getWidth()) + 20);
    }
    for (auto* e : extras)
        put (jmax (e->getWidth(), 40));
    return out;
}

int ChipsItem::heightFor (int width)
{
    const auto p = place (width);
    return p.empty() ? 0 : p.back().getBottom();
}

void ChipsItem::paint (Graphics& g)
{
    const auto p = place (getWidth());
    for (size_t i = 0; i < chips.size(); ++i)
    {
        const auto r = p[i].toFloat();
        g.setColour (Colour (0xff0d0a0c));
        g.fillRoundedRectangle (r, r.getHeight() / 2);
        g.setColour (chips[i].colour == look::muted ? Colours::black : chips[i].colour.withAlpha (0.4f));
        g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() / 2, 1.0f);
        g.setColour (chips[i].colour);
        g.setFont (look::font (10.4f, 700, 0.08f));
        g.drawText (chips[i].text.toUpperCase(), r, Justification::centred, false);
    }
}

void ChipsItem::resized()
{
    const auto p = place (getWidth());
    for (size_t i = 0; i < extras.size(); ++i)
        extras[i]->setBounds (p[chips.size() + i]);
}

void PictureItem::resized()
{
    if (overlay != nullptr)
    {
        const int w = overlay->getWidth() > 0 ? overlay->getWidth() : 130;
        overlay->setBounds (getWidth() - w - 7, getHeight() - 31, w, 24);
    }
}

//== Row ============================================================================
void RowItem::add (Component* c, int width, int height)
{
    entries.push_back ({ c, width, height });
    addAndMakeVisible (c);
}

std::vector<Rectangle<int>> RowItem::place (int width) const
{
    std::vector<Rectangle<int>> out;
    int x = 0, y = 0, rowH = 0;
    for (auto& e : entries)
    {
        const int w = jmin (e.w, width);
        if (x > 0 && x + w > width)
        {
            x = 0;
            y += rowH + gap;
            rowH = 0;
        }
        out.push_back ({ x, y, w, e.h });
        x += w + gap;
        rowH = jmax (rowH, e.h);
    }
    // centre each item vertically in its row
    return out;
}

int RowItem::heightFor (int width)
{
    int h = 0;
    for (auto& r : place (width))
        h = jmax (h, r.getBottom());
    return h;
}

void RowItem::resized()
{
    const auto p = place (getWidth());
    for (size_t i = 0; i < entries.size(); ++i)
    {
        // the text boxes in a row can grow; the row follows
        if (auto* t = dynamic_cast<FlowItem*> (entries[i].c))
        {
            const auto h = jmax (entries[i].h, t->heightFor (p[i].getWidth()));
            t->setBounds (p[i].withHeight (h));
        }
        else
            entries[i].c->setBounds (p[i]);
    }
}

//== Bullet =========================================================================
BulletItem::BulletItem (std::unique_ptr<TextItem> t, std::unique_ptr<Component> r) : text (std::move (t)), remove (std::move (r))
{
    addAndMakeVisible (*text);
    text->onHeightChanged = [this] { if (onHeightChanged) onHeightChanged(); };
    if (remove != nullptr)
        addAndMakeVisible (*remove);
}

int BulletItem::heightFor (int width) { return jmax (18, text->heightFor (width - 16 - (remove != nullptr ? 26 : 0))); }

void BulletItem::paint (Graphics& g) { look::dot (g, { 3.0f, 9.5f }, look::red, 6.0f); }

void BulletItem::resized()
{
    auto r = getLocalBounds().withTrimmedLeft (16);
    if (remove != nullptr)
        remove->setBounds (r.removeFromRight (22).withHeight (18));
    text->setBounds (r);
}

//== Column =========================================================================
int Column::heightFor (int width)
{
    const int w = width - padding.getLeftAndRight();
    int h = padding.getTop();
    bool first = true;
    for (auto& e : items)
    {
        const int ih = e.item->heightFor (w);
        if (ih <= 0 && e.item->getNumChildComponents() == 0)
            continue;
        if (! first) h += e.gapBefore >= 0 ? e.gapBefore : gap;
        h += ih;
        first = false;
    }
    return h + padding.getBottom();
}

void Column::resized()
{
    const int w = getWidth() - padding.getLeftAndRight();
    int y = padding.getTop();
    bool first = true;
    for (auto& e : items)
    {
        const int ih = e.item->heightFor (w);
        if (ih <= 0 && e.item->getNumChildComponents() == 0)
        {
            e.item->setBounds ({});
            continue;
        }
        if (! first) y += e.gapBefore >= 0 ? e.gapBefore : gap;
        e.item->setBounds (padding.getLeft(), y, w, ih);
        y += ih;
        first = false;
    }
}

void Column::relayout()
{
    if (onHeightChanged)
        onHeightChanged();
    if (onRelayout)
        onRelayout();
    resized();
}
} // namespace emx::hub
