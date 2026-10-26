// Emulon X — V3 Editor panels for the EMU engine (the E-mu style voice every zone can use):
// FILTER & CHORUS, 6-STAGE ENVELOPES, LFOs and CORDS (the mod matrix). They fold like the
// Editor's other panels and edit the selected zone's design.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include "Widgets.h"
#include "../engine/EmuVoice.h"

namespace emx
{
class EmulonXProcessor;
class ValueBox;

/** What the panels need from the Editor: the selected zone's design (null = the zone doesn't use the
    EMU engine) and a way to change it (one undo step per drag, like the other zone settings). */
struct EmuContext
{
    EmulonXProcessor& proc;
    std::function<const emu::Design*()> design;
    std::function<void (std::function<void (emu::Design&)>)> edit;
    std::function<void()> enable, disable, copyToAll;
};

//== A foldable panel in the Editor's stack ==========================================
class EmuPanel : public juce::Component
{
public:
    EmuPanel (EmuContext&, juce::String title, juce::String hint);

    bool isOpen() const noexcept { return open; }
    void setOpen (bool o) { open = o; if (onFold) onFold(); }
    std::function<void()> onFold;

    int getPreferredHeight();
    /** Re-reads the design (selection or zone changed). */
    virtual void refresh() = 0;

    void paint (juce::Graphics&) final;
    void resized() final;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

    static constexpr int pad = 10, headH = 22;

protected:
    virtual int contentHeight() = 0;
    virtual void layoutContent (juce::Rectangle<int>) = 0;
    virtual void paintContent (juce::Graphics&) {}
    /** Room in the head row (right side) for buttons. */
    juce::Rectangle<int> headRow() const;
    juce::Rectangle<int> contentArea() const;
    void drawGroup (juce::Graphics&, juce::Rectangle<int>, const juce::String& title);

    EmuContext& ctx;
    juce::String title, hint;
    bool open = true;
    int headRight = 0;   // where the head's buttons begin (the rest folds the panel)
};

//== FILTER & CHORUS (and turning the EMU engine on / off) ===========================
class EmuFilterPanel final : public EmuPanel
{
public:
    explicit EmuFilterPanel (EmuContext&);
    void refresh() override;

private:
    int contentHeight() override;
    void layoutContent (juce::Rectangle<int>) override;
    void paintContent (juce::Graphics&) override;

    juce::TextButton enableButton { "USE THE EMU ENGINE ON THIS ZONE" }, offButton { "EMU ENGINE ON" }, copyButton { "COPY TO ALL ZONES" };
    juce::ComboBox filterType;
    ValueControl freq { "Frequency", ValueControl::Style::knob }, res { "Resonance", ValueControl::Style::knob },
                 chorus { "Chorus", ValueControl::Style::knob }, width { "Width", ValueControl::Style::knob },
                 bendUp { "Bend +", ValueControl::Style::knob }, bendDown { "Bend -", ValueControl::Style::knob };
    juce::Rectangle<int> groups[3];
    bool has = false;
};

//== 6-STAGE ENVELOPES ==================================================================
class EnvGraph final : public juce::Component
{
public:
    explicit EnvGraph (EmuContext& c) : ctx (c) {}
    int which = 0;   // 0 amp, 1 filter, 2 aux
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;

private:
    std::array<juce::Point<float>, 7> points (const emu::Envelope&) const;   // start + the end of each stage
    float yFor (float level) const;
    EmuContext& ctx;
    int dragPoint = -1, hover = -1;
    float startRate = 0.0f, startLevel = 0.0f;
};

class EmuEnvPanel final : public EmuPanel
{
public:
    explicit EmuEnvPanel (EmuContext&);
    ~EmuEnvPanel() override;
    void refresh() override;

private:
    int contentHeight() override;
    void layoutContent (juce::Rectangle<int>) override;
    void paintContent (juce::Graphics&) override;

    juce::TextButton envButtons[3] { juce::TextButton ("AMP"), juce::TextButton ("FILTER"), juce::TextButton ("AUX") };
    EnvGraph graph;
    juce::OwnedArray<ValueBox> rates, levels;
    juce::Rectangle<int> stageLabels[6];
};

//== LFOs ==================================================================================
class EmuLfoPanel final : public EmuPanel
{
public:
    explicit EmuLfoPanel (EmuContext&);
    void refresh() override;

private:
    int contentHeight() override;
    void layoutContent (juce::Rectangle<int>) override;
    void paintContent (juce::Graphics&) override;

    struct Lfo
    {
        juce::ComboBox shape, division;
        juce::TextButton sync { "SYNC" }, keySync { "KEY SYNC" };
        ValueControl rate { "Rate", ValueControl::Style::knob }, delay { "Delay", ValueControl::Style::knob }, variation { "Variation", ValueControl::Style::knob };
        juce::Rectangle<int> group, preview;
    };
    Lfo lfos[2];
};

//== CORDS (the mod matrix) ==============================================================
class EmuCordsPanel final : public EmuPanel
{
public:
    explicit EmuCordsPanel (EmuContext&);
    ~EmuCordsPanel() override;
    void refresh() override;

private:
    class Row;
    int contentHeight() override;
    void layoutContent (juce::Rectangle<int>) override;
    void paintContent (juce::Graphics&) override;
    std::vector<int> shownRows() const;   // cord indices shown (used ones, or all 36)

    juce::TextButton addButton { "+ ADD CORD" }, allButton { "SHOW ALL 36" };
    juce::OwnedArray<Row> rows;
    bool showAll = false;
};
} // namespace emx
