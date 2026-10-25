// Emulon X Hub — small building blocks for the pages: buttons, text (editable in dev mode), chips, pictures,
// and a column that stacks them.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "HubLook.h"

namespace emx::hub
{
class HubButton : public juce::Button
{
public:
    explicit HubButton (const juce::String& label = {}, look::Style s = look::Style::grey, float px = 11.0f);
    look::Style style;
    float fontPx;
    float progress = -1.0f;   // >= 0 draws the progress fill
    bool link = false;        // underlined text, no face
    bool dashed = false;      // a "+ add" placeholder
    bool plain = false;       // quiet text only (the footer)
    bool glow = true;
    int radius = 5;
    juce::Colour textColour { 0xffffffff };
    void paintButton (juce::Graphics&, bool hover, bool down) override;
    int idealWidth() const;
};

/** Anything that can go into a Column: says how tall it wants to be for a width. */
struct FlowItem : public juce::Component
{
    virtual int heightFor (int width) = 0;
    std::function<void()> onHeightChanged;   // set by the column
};

/** Text: a heading, a line, paragraphs or a bullet. In dev mode it becomes a text box showing the raw text. */
class TextItem : public FlowItem, private juce::TextEditor::Listener
{
public:
    struct Options
    {
        juce::Font font = look::font (13.0f);
        juce::Colour colour = look::body;
        juce::Colour boldColour = look::text;
        bool upper = false;
        bool paragraphs = false;    // empty lines separate paragraphs
        bool multiline = false;     // keep line breaks when editing
        float lineSpacing = 1.45f;  // as a multiple of the font size
        juce::String placeholder;
        bool glow = false;          // a soft red glow (titles)
        bool led = false;           // red LED text on a dark screen
        juce::Justification just = juce::Justification::topLeft;
    };
    TextItem (const juce::String& text, Options);
    ~TextItem() override;
    /** Dev mode: typing changes the text; commit runs when you leave the box or press Enter. */
    void makeEditable (std::function<void (const juce::String&)> commit);
    void setText (const juce::String&);
    const juce::String& getText() const { return text; }
    int heightFor (int width) override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    bool isEditing() const { return editor != nullptr; }

private:
    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    void textEditorFocusLost (juce::TextEditor&) override;
    void doCommit();
    juce::String shown() const;
    std::vector<juce::TextLayout> layoutFor (float width) const;

    juce::String text;
    Options opt;
    std::unique_ptr<juce::TextEditor> editor;
    std::function<void (const juce::String&)> commit;
    int lastEditorHeight = 0;
};

/** Little rounded tags: FREE, OPEN SOURCE, v1.2.0ea ... */
class ChipsItem : public FlowItem
{
public:
    struct Chip { juce::String text; juce::Colour colour; };
    std::vector<Chip> chips;
    std::vector<juce::Component*> extras;   // components that sit in the row (dev buttons)
    int heightFor (int width) override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    std::vector<juce::Rectangle<int>> place (int width) const;
};

/** A picture (16:10 unless told otherwise), drawn by a function. */
class PictureItem : public FlowItem
{
public:
    std::function<void (juce::Graphics&, juce::Rectangle<float>)> draw;
    float aspect = 10.0f / 16.0f;
    juce::Component* overlay = nullptr;   // e.g. a CHANGE PICTURE button in the corner
    int heightFor (int width) override { return juce::roundToInt ((float) width * aspect); }
    void paint (juce::Graphics& g) override { if (draw) draw (g, getLocalBounds().toFloat()); }
    void resized() override;
};

/** A row of components that wraps (buttons, labelled fields). Each child keeps its width. */
class RowItem : public FlowItem
{
public:
    void add (juce::Component* c, int width, int height = 24);
    int heightFor (int width) override;
    void resized() override;
    int gap = 6;

private:
    struct Entry { juce::Component* c; int w, h; };
    std::vector<Entry> entries;
    std::vector<juce::Rectangle<int>> place (int width) const;
};

/** A bullet point with text (and in dev mode a remove button). */
class BulletItem : public FlowItem
{
public:
    BulletItem (std::unique_ptr<TextItem>, std::unique_ptr<juce::Component> remove = {});
    int heightFor (int width) override;
    void paint (juce::Graphics&) override;
    void resized() override;
    TextItem& textItem() { return *text; }

private:
    std::unique_ptr<TextItem> text;
    std::unique_ptr<juce::Component> remove;
};

/** A box (LED screen or metal) holding a column of items. */
class BoxItem;

/** Stacks FlowItems from top to bottom. Owns them. */
class Column : public FlowItem
{
public:
    template <typename T> T& add (std::unique_ptr<T> item, int gapBefore = -1)
    {
        auto& ref = *item;
        item->onHeightChanged = [this] { relayout(); };
        addAndMakeVisible (*item);
        items.push_back ({ std::move (item), gapBefore });
        return ref;
    }
    void clear() { items.clear(); }
    int heightFor (int width) override;
    void resized() override;
    void relayout();
    int gap = 12;
    juce::BorderSize<int> padding;
    std::function<void (juce::Graphics&, juce::Rectangle<float>)> background;
    void paint (juce::Graphics& g) override { if (background) background (g, getLocalBounds().toFloat()); }
    std::function<void()> onRelayout;   // the column's own height may have changed

private:
    struct Entry { std::unique_ptr<FlowItem> item; int gapBefore; };
    std::vector<Entry> items;
};

/** Dev mode buttons and text boxes look like this. */
juce::TextEditor* styleEditor (juce::TextEditor&, const juce::Font&, bool led);
void drawDashed (juce::Graphics&, juce::Rectangle<float>, juce::Colour);
} // namespace emx::hub
