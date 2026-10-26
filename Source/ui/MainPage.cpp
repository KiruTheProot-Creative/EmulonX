// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MainPage.h"
#include "Theme.h"
#include "../PluginProcessor.h"
#include "FxPage.h"

namespace emx
{
using namespace juce;

MainPage::MainPage (EmulonXProcessor& p) : proc (p)
{
    for (int i = 0; i < numMacros; ++i)
    {
        auto* k = macros.add (new EmxKnob (p.apvts, macroParamId (i), {}, 40));
        k->setEditableCaption ([this, i] { return proc.getMacroName (i); },
                               [this, i] (const String& n) { proc.setMacroName (i, n); });
        k->slider.onRightClick = [this, i, k] { modui::showMacroMenu (proc, i, k); };
        k->slider.setTooltip ("Automatable in your DAW - right-click to link it to FX knobs");
        addAndMakeVisible (k);
    }

    volume.setRange ({ -60.0, 6.0, 0.1, 2.2 }, 0.0);
    volume.format = [] (double v) { return v <= -59.9 ? String ("-INF DB") : (v > 0 ? "+" : "") + String (v, 1) + " DB"; };
    volume.onChange = [this] (double v) { proc.setPartVolume ((float) v); };
    volume.slider.setTooltip ("This preset's own volume (before the master volume in the top bar)");
    pan.setRange ({ -1.0, 1.0, 0.01 }, 0.0);
    pan.format = [] (double v) { const int x = roundToInt (v * 100.0); return x == 0 ? String ("C") : (x < 0 ? "L" + String (-x) : "R" + String (x)); };
    pan.onChange = [this] (double v) { proc.setPartPan ((float) v); };
    pan.slider.setTooltip ("This preset's place between left and right");
    addAndMakeVisible (volume);
    addAndMakeVisible (pan);

    proc.addChangeListener (this);
    settings->addChangeListener (this);
    changeListenerCallback (nullptr);
}

MainPage::~MainPage()
{
    proc.removeChangeListener (this);
    settings->removeChangeListener (this);
}

void MainPage::changeListenerCallback (ChangeBroadcaster*)
{
    // A preset may have brought new macro names; the settings may have a new knob sensitivity.
    const int sens = settings->getKnobSensitivity();
    for (auto* k : macros)
    {
        k->refreshCaption();
        k->setSensitivity (sens);
    }
    volume.setSensitivity (sens);
    pan.setSensitivity (sens);
    volume.setValue (proc.getPartVolume());
    pan.setValue (proc.getPartPan());
    repaint (infoArea.toNearestInt());
}

void MainPage::tick()
{
    proc.readScope (scopeLo, scopeHi);
    repaint (scopeArea.toNearestInt());
}

String MainPage::bankCode (const String& bankName)
{
    const auto clean = bankName.toUpperCase().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 ");
    if (clean.trim().isEmpty())
        return "USR";
    const auto letters = clean.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    // a number at the end tells banks of a series apart: "Orbit-3" -> OR3, "Proteus 2" -> PR2
    String digits;
    for (int i = clean.length() - 1; i >= 0 && CharacterFunctions::isDigit (clean[i]); --i)
        digits = String::charToString (clean[i]) + digits;
    if (digits.isNotEmpty() && letters.length() >= 2)
        return letters.substring (0, 3 - jmin (2, digits.length())) + digits.getLastCharacters (jmin (2, digits.length()));
    return (letters + "XXX").substring (0, 3);
}

String MainPage::categoryOf (const String& presetName)
{
    if (! presetName.containsChar (':') || presetName.indexOfChar (':') > 4)
        return {};
    static const char* table[][2] {
        { "amb", "AMBIENT" }, { "arp", "ARPEGGIO" }, { "bas", "BASS" }, { "bel", "BELLS" }, { "bow", "BOWED" }, { "brs", "BRASS" },
        { "bts", "BEATS" }, { "cho", "CHOIR" }, { "cmb", "COMBINATION" }, { "drm", "DRUMS" }, { "dms", "DRUMS" }, { "fx", "EFFECTS" },
        { "gtr", "GUITAR" }, { "hit", "HITS" }, { "key", "KEYBOARD" }, { "kit", "DRUM KIT" }, { "led", "LEAD" }, { "lop", "LOOP" },
        { "org", "ORGAN" }, { "pad", "PAD" }, { "pno", "PIANO" }, { "prc", "PERCUSSION" }, { "pr1", "PERCUSSION" }, { "pr2", "PERCUSSION" },
        { "sfx", "SOUND FX" }, { "str", "STRINGS" }, { "syn", "SYNTH" }, { "tun", "TUNED PERC" }, { "vox", "VOICE" }, { "wnd", "WIND" },
        { "vln", "VIOLIN" }, { "hrn", "HORN" }, { "flt", "FLUTE" }, { "spl", "SPLIT" }, { "lyr", "LAYER" }, { "rhy", "RHYTHM" } };
    const auto pre = presetName.upToFirstOccurrenceOf (":", false, false).trim().toLowerCase();
    for (auto& t : table)
        if (pre == t[0])
            return t[1];
    return pre.toUpperCase();
}

void MainPage::mouseDoubleClick (const MouseEvent& e)
{
    if (! nameArea.contains (e.position))
        return;
    rename = std::make_unique<TypeInField> (*this, nameArea.toNearestInt(), proc.getPresetName(),
                                            [this] (const String& t) { if (t.trim().isNotEmpty()) proc.setPresetName (t.trim()); repaint(); });
}

void MainPage::resized()
{
    auto r = getLocalBounds();
    presetArea = r.removeFromTop (186).toFloat();
    r.removeFromTop (8);
    auto inner = presetArea.reduced (11.0f, 9.0f).withTrimmedTop (22.0f);
    scopeArea = inner.removeFromLeft (inner.getWidth() * 0.58f);
    inner.removeFromLeft (8.0f);
    infoArea = inner;

    outArea = r.removeFromLeft (170).toFloat();
    r.removeFromLeft (8);
    outInset = outArea.reduced (10.0f, 8.0f).withTrimmedTop (22.0f);
    {
        auto o = outInset.toNearestInt();
        const int w = 64, h = volume.getPreferredHeight();
        const int y = o.getCentreY() - h / 2;
        volume.setBounds (o.getCentreX() - w - 4, y, w, h);
        pan.setBounds (o.getCentreX() + 4, y, w, h);
    }

    macroArea = r.toFloat();
    macroInset = macroArea.reduced (10.0f, 8.0f).withTrimmedTop (22.0f);
    auto grid = macroInset.reduced (6.0f, 6.0f).toNearestInt();
    constexpr int perRow = numMacros / 2;   // two rows of 8
    const int cellW = grid.getWidth() / perRow, cellH = macros[0]->getPreferredHeight();
    const int gap = jmax (2, (grid.getHeight() - 2 * cellH) / 2);
    for (int i = 0; i < macros.size(); ++i)
        macros[i]->setBounds (grid.getX() + (i % perRow) * cellW, grid.getY() + gap / 2 + (i / perRow) * (cellH + gap), cellW, cellH);
}

void MainPage::paint (Graphics& g)
{
    // The preset LCD
    theme::drawPanel (g, presetArea.reduced (1.0f));
    theme::drawHeading (g, presetArea.reduced (11.0f, 9.0f), "Preset", "the display follows the sound");
    theme::drawScreen (g, scopeArea);
    {
        auto s = scopeArea.reduced (8.0f, 10.0f);
        const float mid = s.getCentreY(), half = s.getHeight() * 0.5f;
        const float colW = s.getWidth() / (float) EmulonXProcessor::scopeSize;
        float peak = 0.0f;
        for (int i = 0; i < EmulonXProcessor::scopeSize; ++i)
            peak = jmax (peak, std::abs (scopeLo[i]), std::abs (scopeHi[i]));
        const float scale = 1.0f / jmax (0.25f, peak * 1.1f);   // gently auto-scaled, so quiet presets still show
        g.setColour (theme::red.withAlpha (0.25f));
        g.fillRect (s.getX(), mid - 0.5f, s.getWidth(), 1.0f);
        g.setColour (theme::red);
        for (int i = 0; i < EmulonXProcessor::scopeSize; ++i)
        {
            const float top = mid - jlimit (0.0f, 1.0f, scopeHi[i] * scale) * half;
            const float bot = mid + jlimit (0.0f, 1.0f, -scopeLo[i] * scale) * half;
            g.fillRect (s.getX() + (float) i * colW, top, jmax (1.0f, colW - 0.6f), jmax (1.0f, bot - top));
        }
    }

    theme::drawScreen (g, infoArea, false);
    {
        const auto& src = proc.getPresetSource();
        auto t = infoArea.reduced (14.0f, 10.0f);
        const auto ledCol = theme::isMetal() ? theme::textStrong : theme::red;
        g.setColour (ledCol);
        auto top = t.removeFromTop (38.0f);
        g.setFont (theme::font (30.0f, 700, 0.12f));
        const auto code = src.bank.isNotEmpty() ? bankCode (src.bank) : String ("USR");
        g.drawText (code, top.removeFromLeft (top.getWidth() * 0.45f), Justification::centredLeft, false);
        g.drawText (src.number >= 0 ? String (src.number).paddedLeft ('0', 3) : String ("---"), top, Justification::centredLeft, false);
        t.removeFromTop (6.0f);
        auto name = proc.getPresetName();
        const auto& info = proc.getPresetInfo();
        const auto prefix = categoryOf (name);
        if (prefix.isNotEmpty())
            name = name.fromFirstOccurrenceOf (":", false, false).trim();
        const auto cat = info.type.isNotEmpty() ? info.type.toUpperCase() : prefix;   // the type from the Save window wins
        g.setFont (theme::font (22.0f, 700, 0.06f));
        nameArea = t.removeFromTop (30.0f);
        g.drawFittedText (name.toUpperCase(), nameArea.toNearestInt(), Justification::centredLeft, 1, 0.7f);
        t.removeFromTop (6.0f);
        g.setColour (ledCol.withAlpha (0.75f));
        g.setFont (theme::font (10.0f, 700, 0.14f));
        g.drawText ((cat.isNotEmpty() ? cat : String ("NO CATEGORY")) + (src.bank.isNotEmpty() ? "  -  " + src.bank.toUpperCase() : String())
                        + (info.author.isNotEmpty() ? "  -  BY " + info.author.toUpperCase() : String()),
                    t.removeFromTop (16.0f), Justification::centredLeft, true);
    }

    // Output: the preset's volume and pan
    theme::drawPanel (g, outArea.reduced (1.0f));
    theme::drawHeading (g, outArea.reduced (11.0f, 9.0f), "Output");
    theme::drawInset (g, outInset);

    // Macros
    theme::drawPanel (g, macroArea.reduced (1.0f));
    theme::drawHeading (g, macroArea.reduced (11.0f, 9.0f), "Macro controller", "automatable in your DAW - double-click a name to rename - right-click to link to FX");
    theme::drawInset (g, macroInset);
}
} // namespace emx
