// Emulon X — the Editor tab (V2 "SuperSampler"): a strip on top (mode, zone, which panels to show,
// quality) and foldable panels that scroll: SAMPLE (loop / warp / slices), VOICE and KEYMAP.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <bitset>
#include <map>
#include "Widgets.h"
#include "../Settings.h"
#include "../engine/Zone.h"
#include "../engine/SampleEdit.h"
#include "../engine/EmuVoice.h"

namespace emx
{
class EmulonXProcessor;
struct ModelZone;

/** Zone colours: steps around the colour wheel from the skin's accent (golden angle), same lightness. */
juce::Colour zoneColour (int index);

//== Waveform: loop markers, warp preview or slice cuts ===========================
class WaveformView final : public juce::Component
{
public:
    explicit WaveformView (EmulonXProcessor&);

    enum class Mode { loop, warp, slices };
    void setMode (Mode m) { mode = m; repaint(); }
    Mode getMode() const noexcept { return mode; }

    /** The warp drawn in yellow over the sample (WARP tab). */
    void setWarp (const edit::Warp& w) { warp = w; repaint(); }

    void zoomAll();
    void zoomAround (int frame, int halfWidth = 1000);
    bool snapToZero = false;
    std::function<void (int which, int frame)> onMarkerMoved;   // which: 0 loop start, 1 loop end, 2 sample start, 3 sample end, 4 random area end

    void tick();   // playheads
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static int nearestZeroCrossing (const juce::AudioBuffer<float>&, int frame);

private:
    const ModelZone* zone() const;
    int viewKey (const ModelZone&) const;
    juce::Range<double> view (const ModelZone&);
    void setView (const ModelZone&, double start, double end);
    int hitMarker (const ModelZone&, juce::Point<float>);
    int hitCut (const ModelZone&, juce::Point<float>);
    double frameAt (const ModelZone&, float x);
    void paintWave (juce::Graphics&, const ModelZone&, juce::Range<double> v, bool warped);

    EmulonXProcessor& proc;
    Mode mode = Mode::loop;
    edit::Warp warp;
    std::map<int, juce::Range<double>> views;   // zoom per zone (one for all slices)
    int dragMode = -1;                          // -1 none, 0..4 marker, 5 pan, 6 slice cut
    int dragCut = -1;
    std::vector<int> dragCuts;
    juce::Range<double> dragView;
    std::vector<double> playheads, lastPlayheads;
};

//== Three cycles of the wave, big: how the warp changes its shape ================
class CloseUpView final : public juce::Component
{
public:
    explicit CloseUpView (EmulonXProcessor& p) : proc (p) {}
    void setWarp (const edit::Warp& w, int centreFrame) { warp = w; centre = centreFrame; repaint(); }
    void paint (juce::Graphics&) override;

private:
    EmulonXProcessor& proc;
    edit::Warp warp;
    int centre = 0;
};

//== A value in a red LED box: drag up / down to change it, double-click to type ===
class ValueBox final : public juce::Component,
                       public juce::SettableTooltipClient
{
public:
    explicit ValueBox (juce::String label = {});

    void setRange (double minimum, double maximum, double step = 1.0) { lo = minimum; hi = maximum; inc = step; }
    void setValue (double v) { if (! juce::exactlyEqual (v, value)) { value = v; repaint(); } }
    double getValue() const noexcept { return value; }
    void setReadOnly (bool r) { readOnly = r; }
    void setDragPixels (float px) { dragPixels = px; }   // pixels for the whole range

    std::function<juce::String (double)> format;
    std::function<bool (const juce::String&, double&)> parse;   // typed text -> value (default: the number in it)
    std::function<void (double)> onChange;

    static constexpr int labelledHeight = 36;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> ledArea() const;
    void set (double v);

    juce::String label;
    double value = 0.0, lo = 0.0, hi = 1.0, inc = 1.0, dragStart = 0.0;
    float dragPixels = 200.0f;
    bool readOnly = false;
    std::unique_ptr<TypeInField> typeIn;
};

//== Key ranges (or slices) over a big keyboard ====================================
class KeymapView final : public juce::Component
{
public:
    explicit KeymapView (EmulonXProcessor&);

    int getPreferredHeight();
    void tick();   // held keys
    void paint (juce::Graphics&) override;
    void resized() override { layoutLanes(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void layoutLanes();

    static constexpr float laneStep = 54.0f, keysHeight = 84.0f, labelsHeight = 14.0f;

    /** STACK: every zone gets its own lane (the V2 view). OVERLAP: key x velocity, zones on top of each other (see-through). */
    static inline bool stacked = false;   // shared by every window

private:
    struct KeyRect { float x, w; bool black; };
    KeyRect keyRect (int note) const;
    int noteAt (juce::Point<float>) const;
    juce::Rectangle<float> barRect (size_t index) const;
    int velocityAt (float y, int note) const;
    int zoneIndexOf (int id) const;

    EmulonXProcessor& proc;
    std::vector<int> lanes;   // lane of each zone (same order as getZones())
    int numLanes = 1, firstNote = 36, lastNote = 96;
    float keyW = 12.0f, laneH = laneStep, keysTop = 0.0f;
    std::bitset<128> held;

    enum class Drag { none, move, resizeLo, resizeHi, resizeVelLo, resizeVelHi, play };
    Drag drag = Drag::none;
    int dragZone = 0, dragLo = 0, dragHi = 0, dragLoVel = 1, dragHiVel = 127, playingNote = -1;
    std::vector<int> clickCandidates;   // zones under the mouse (Kontakt-style: click again to reach the one below)
    bool cycleOnClick = false;
    float velY (int velocity) const;    // y of a velocity in the map (127 at the top)
    Drag edgeAt (juce::Rectangle<float> bar, juce::Point<float>) const;
};

} // namespace emx
#include "EmuEditor.h"
namespace emx
{
//== The page =====================================================================
class EditorPage final : public juce::Component,
                         private juce::ChangeListener
{
public:
    explicit EditorPage (EmulonXProcessor&);
    ~EditorPage() override;

    void tick();
    void paint (juce::Graphics&) override;
    void resized() override;

    enum class Tab { loop, warp, slices };
    /** For the preview tool: pick a SAMPLE tab, fold panels, scroll. */
    void showTab (Tab);
    void setPanelShown (int panel, bool shown);   // 0 sample, 1 voice, 2 keymap
    void scrollTo (int y);

private:
    class Stack;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void layoutStack();
    void paintStack (juce::Graphics&);
    void stackClicked (juce::Point<int>);
    void edit (std::function<void (ZoneParams&)>, bool adsr = false);
    void editDesign (std::function<void (emu::Design&)>);
    void setLoopPoint (int which, int frame, bool exact);
    edit::Warp currentWarp() const;
    void updateWarpPreview();
    Tab shownTab() const;

    EmulonXProcessor& proc;
    juce::SharedResourcePointer<Settings> settings;

    // Strip
    juce::TextButton modeButtons[2] { juce::TextButton ("SAMPLER"), juce::TextButton ("SPLICER") };
    juce::TextButton prevButton { "<" }, nextButton { ">" };
    LedDisplay zoneName;
    juce::TextButton showButtons[3] { juce::TextButton ("SAMPLE"), juce::TextButton ("VOICE"), juce::TextButton ("KEYMAP") };
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" };
    juce::TextButton interpButtons[3] { juce::TextButton ("LINEAR"), juce::TextButton ("CUBIC"), juce::TextButton ("SINC HQ") };
    juce::TextButton allButton { "ALL" };

    std::unique_ptr<Stack> stack;
    juce::Viewport viewport;
    bool shown[3] { true, true, true };
    Tab tab = Tab::loop;

    // SAMPLE
    juce::TextButton tabButtons[2];
    WaveformView wave;
    //   loop
    juce::TextButton loopButtons[3] { juce::TextButton ("OFF"), juce::TextButton ("ON"), juce::TextButton ("UNTIL RELEASE") };
    juce::TextButton dirButtons[3] { juce::TextButton ("FORWARD"), juce::TextButton ("PING-PONG"), juce::TextButton ("BACKWARD") };
    ValueBox startBox { "START" }, endBox { "END" }, loopStartBox { "LOOP START" }, loopEndBox { "LOOP END" }, fadeBox { "CROSS-FADE" }, randomBox { "RANDOM TO" };
    juce::TextButton randomButton { "RANDOM START" }, zeroButton { "ZERO-X" }, snapButton { "SNAP" }, zoomAllButton { "SHOW ALL" };
    //   warp
    juce::TextButton shapeButtons[edit::numWarpShapes];
    ValueBox amountBox, mixBox;
    juce::TextButton whereButtons[2] { juce::TextButton ("WHOLE"), juce::TextButton ("LOOP") };
    CloseUpView closeUp;
    juce::TextButton undoWarpButton { "UNDO" }, applyButton { "APPLY TO SAMPLE" };
    edit::Warp warp;
    bool warpPart = false;
    //   slices
    juce::TextButton findHitsButton { "FIND HITS" };
    ValueBox sensBox, firstKeyBox;
    juce::TextButton evenButtons[4] { juce::TextButton ("4"), juce::TextButton ("8"), juce::TextButton ("16"), juce::TextButton ("32") };
    juce::TextButton toZonesButton { "TURN SLICES INTO ZONES" };

    // VOICE
    juce::TextButton copyEnvButton { "COPY ENVELOPE TO ALL ZONES" };
    ValueControl semi { "Semi", ValueControl::Style::knob }, tune { "Tune", ValueControl::Style::knob }, grain { "Grain", ValueControl::Style::knob };
    juce::TextButton keepSpeed { "KEEP SPEED" };
    ValueControl gain { "Gain", ValueControl::Style::knob }, pan { "Pan", ValueControl::Style::knob };
    ValueControl attack { "Attack", ValueControl::Style::fader, 84 }, decay { "Decay", ValueControl::Style::fader, 84 },
                 sustain { "Sustain", ValueControl::Style::fader, 84 }, release { "Release", ValueControl::Style::fader, 84 };
    juce::TextButton polyButton { "POLY" }, monoButton { "MONO" }, legatoButton { "LEGATO" };
    ValueControl glide { "Glide", ValueControl::Style::knob };

    // EMU ENGINE (V3): filter & chorus, 6-stage envelopes, LFOs, cords
    EmuContext emuCtx;
    EmuFilterPanel emuFilter;
    EmuEnvPanel emuEnv;
    EmuLfoPanel emuLfo;
    EmuCordsPanel emuCords;

    // KEYMAP
    KeymapView keymap;
    ValueBox rootBox { "ROOT" }, loKeyBox { "LOW KEY" }, hiKeyBox { "HIGH KEY" }, loVelBox { "LOW VEL" }, hiVelBox { "HIGH VEL" };
    juce::TextButton stackButton { "STACK" }, overlapButton { "OVERLAP" };
    juce::TextButton addZoneButton { "+ ADD ZONE" }, autoMapButton { "AUTO MAP" }, dupButton { "DUPLICATE" }, deleteButton { "DELETE" };
    ValueBox sliceKeyBox { "KEY" }, sliceLenBox { "LENGTH" };
    juce::TextButton sliceOnlyButton { "PLAY SLICE ONLY" }, toEndButton { "PLAY TO END" };
    std::unique_ptr<juce::FileChooser> chooser;

    // what the stack paints (filled by layoutStack)
    struct Text { juce::Rectangle<float> r; juce::String text; bool hint; juce::Justification just; int heading = -1; bool chips = false; };
    std::vector<Text> texts;
    juce::Rectangle<float> panelArea[3], headArea[3], groupArea[4], chipArea;
    std::vector<Component*> stackItems;
    juce::Rectangle<float> stripArea;
    std::vector<Text> stripTexts;
};
} // namespace emx
