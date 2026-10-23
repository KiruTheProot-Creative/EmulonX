// Emulon X — the Editor tab: waveform + loop points, zone settings, keymap.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <bitset>
#include <map>
#include "Widgets.h"
#include "../Settings.h"
#include "../engine/Zone.h"

namespace emx
{
class EmulonXProcessor;
struct ModelZone;

//== Waveform with loop markers ===================================================
class WaveformView final : public juce::Component
{
public:
    explicit WaveformView (EmulonXProcessor&);

    void zoomAll();
    void zoomAround (int frame, int halfWidth = 1000);
    bool snapToZero = false;
    std::function<void (int which, int frame)> onMarkerMoved;   // which: 0 start, 1 end

    void tick();   // playheads
    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static int nearestZeroCrossing (const juce::AudioBuffer<float>&, int frame);

private:
    const ModelZone* zone() const;
    juce::Range<double> view (const ModelZone&);
    void setView (const ModelZone&, double start, double end);
    int hitMarker (const ModelZone&, float x);
    double frameAt (const ModelZone&, float x);

    EmulonXProcessor& proc;
    std::map<int, juce::Range<double>> views;   // zoom per zone
    int dragMode = -1;                          // -1 none, 0 start, 1 end, 2 pan
    juce::Range<double> dragView;
    std::vector<double> playheads, lastPlayheads;
};

//== Zone bars + keyboard =========================================================
class KeymapView final : public juce::Component
{
public:
    explicit KeymapView (EmulonXProcessor&);

    void tick();   // held keys
    void paint (juce::Graphics&) override;
    void resized() override { layoutLanes(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void layoutLanes();

private:
    struct KeyRect { float x, w; bool black; };
    KeyRect keyRect (int note) const;
    int noteAt (juce::Point<float>) const;
    juce::Rectangle<float> barRect (size_t index) const;
    int velocityAt (float y, int note) const;

    EmulonXProcessor& proc;
    std::vector<int> lanes;   // lane of each zone (same order as getZones())
    int numLanes = 1;
    float keyW = 12.0f, laneH = 18.0f, keysTop = 0.0f;
    std::bitset<128> held;

    enum class Drag { none, move, resizeLo, resizeHi, play };
    Drag drag = Drag::none;
    int dragZone = 0, dragLo = 0, dragHi = 0, playingNote = -1;
};

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

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void refresh();
    void edit (std::function<void (ZoneParams&)>);
    void setLoopPoint (int which, int frame, bool exact);

    EmulonXProcessor& proc;
    juce::SharedResourcePointer<Settings> settings;

    // Top bar
    juce::TextButton prevButton { "<" }, nextButton { ">" }, dupButton { "DUPLICATE" }, deleteButton { "DELETE" }, mapButton { "AUTO MAP" };
    juce::TextButton undoButton { "UNDO" }, redoButton { "REDO" };
    LedDisplay zoneName;
    juce::TextButton interpButtons[3] { juce::TextButton ("LINEAR"), juce::TextButton ("CUBIC"), juce::TextButton ("SINC HQ") };

    // Sample & loop
    WaveformView wave;
    juce::TextButton loopButtons[3] { juce::TextButton ("OFF"), juce::TextButton ("LOOP"), juce::TextButton ("UNTIL RELEASE") };
    LedLabel startBox { 9.0f }, endBox { 9.0f }, fadeBox { 9.0f };
    juce::TextButton zeroStart { "ZERO-X" }, zeroEnd { "ZERO-X" }, snapButton { "SNAP" };
    juce::TextButton zoomAllButton { "ALL" }, zoomStartButton { "ZOOM S" }, zoomEndButton { "ZOOM E" };
    LedDisplay loopInfo;

    // Zone settings
    ValueControl root { "Root", ValueControl::Style::knob }, loKey { "Low key", ValueControl::Style::knob },
                 hiKey { "High key", ValueControl::Style::knob }, loVel { "Low vel", ValueControl::Style::knob },
                 hiVel { "High vel", ValueControl::Style::knob };
    ValueControl tune { "Tune", ValueControl::Style::knob }, grain { "Grain", ValueControl::Style::knob };
    juce::TextButton keepSpeed { "KEEP SPEED" };
    ValueControl gain { "Gain", ValueControl::Style::knob }, pan { "Pan", ValueControl::Style::knob };
    ValueControl attack { "Attack", ValueControl::Style::fader, 84 }, decay { "Decay", ValueControl::Style::fader, 84 },
                 sustain { "Sustain", ValueControl::Style::fader, 84 }, release { "Release", ValueControl::Style::fader, 84 };

    KeymapView keymap;

    juce::Rectangle<float> topArea, sampleArea, waveArea, mapArea, pitchArea, ampArea, envArea, keyArea, keyInset;
    juce::Rectangle<float> labelStart, labelEnd, labelFade;
};
} // namespace emx
