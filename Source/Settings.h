// Emulon X — global, per-user settings (shared by every plugin instance).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

namespace emx
{
/** Stored in the user's settings folder, e.g.
    Windows: %APPDATA%\Emulon X\Emulon X.settings
    macOS:   ~/Library/Application Support/Emulon X/Emulon X.settings

    Use through juce::SharedResourcePointer<Settings>. Listeners hear about every change. */
class Settings final : public juce::ChangeBroadcaster
{
public:
    Settings();

    enum class Folder { base, other, user, samples };
    static constexpr float uiScales[] { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };
    static constexpr int voiceChoices[] { 16, 32, 48, 64, 96, 128, 192, 256 };
    static constexpr int cpuChoices[] { 0, 50, 60, 70, 80, 85, 90, 95 };   // 0 = off

    //== Folders ===============================================================
    juce::StringArray getFolders (Folder) const;
    void setFolders (Folder, const juce::StringArray&);
    void addFolder (Folder, const juce::File&);
    void removeFolder (Folder, const juce::String& path);
    /** First user-preset folder (created if needed); default place to save presets. */
    juce::File getUserPresetFolder() const;

    //== Interface =============================================================
    float getUiScale() const;
    void  setUiScale (float);
    int   getKnobSensitivity() const { return get ("knobSensitivity", 1); }   // 0 fast, 1 normal, 2 fine
    void  setKnobSensitivity (int v) { set ("knobSensitivity", v); }
    bool  getShowHints() const       { return get ("showHints", 1) != 0; }
    void  setShowHints (bool v)      { set ("showHints", v ? 1 : 0); }
    static constexpr int maxBankColumns = 6;
    int   getBankColumns() const     { return juce::jlimit (1, maxBankColumns, get ("bankColumns", 3)); }   // presets per row in the bank grid
    void  setBankColumns (int v)     { set ("bankColumns", juce::jlimit (1, maxBankColumns, v)); }

    //== Customization =========================================================
    /** 0 EMULON CLASSIC, 1 EMULON XP (light), 2 EMULON XP (dark), 3 EMURUM, 4 EMUTAL. */
    int   getSkin() const             { return juce::jlimit (0, 4, get ("skin", 0)); }
    void  setSkin (int v)             { set ("skin", juce::jlimit (0, 4, v)); }
    /** Colour scheme rotation in degrees, -180..180 (0 = the original red). */
    float getHueShift() const         { return (float) juce::jlimit (-180.0, 180.0, props->getDoubleValue ("hueShift", 0.0)); }
    void  setHueShift (float deg)     { props->setValue ("hueShift", (double) juce::jlimit (-180.0f, 180.0f, deg)); sendChangeMessage(); }   // saved by the file's own timer (cheap while dragging)

    //== Performance ===========================================================
    int  getMaxVoices() const         { return get ("maxVoices", 64); }
    void setMaxVoices (int v)         { set ("maxVoices", v); }
    int  getCpuLimit() const          { return get ("cpuLimit", 85); }       // percent, 0 = off
    void setCpuLimit (int v)          { set ("cpuLimit", v); }
    bool getStealQuietest() const     { return get ("stealQuietest", 0) != 0; }
    void setStealQuietest (bool v)    { set ("stealQuietest", v ? 1 : 0); }
    int  getDefaultInterp() const     { return get ("defaultInterp", 2); }
    void setDefaultInterp (int v)     { set ("defaultInterp", v); }
    bool getDefaultKeepSpeed() const  { return get ("defaultKeepSpeed", 1) != 0; }
    void setDefaultKeepSpeed (bool v) { set ("defaultKeepSpeed", v ? 1 : 0); }
    bool getEmbedSamples() const      { return get ("embedSamples", 1) != 0; }
    void setEmbedSamples (bool v)     { set ("embedSamples", v ? 1 : 0); }

    //== MIDI & tuning =========================================================
    int   getMidiChannel() const      { return get ("midiChannel", 0); }      // 0 = omni
    void  setMidiChannel (int v)      { set ("midiChannel", v); }
    int   getVelocityCurve() const    { return get ("velocityCurve", 1); }    // 0 soft, 1 linear, 2 hard
    void  setVelocityCurve (int v)    { set ("velocityCurve", v); }
    int   getBendRange() const        { return get ("bendRange", 2); }
    void  setBendRange (int v)        { set ("bendRange", v); }
    float getA4() const               { return (float) props->getDoubleValue ("a4", 440.0); }
    void  setA4 (float hz)            { props->setValue ("a4", (double) hz); changed(); }

    void resetToDefaults();
    juce::PropertiesFile& properties() noexcept { return *props; }

private:
    int  get (const juce::String& key, int fallback) const { return props->getIntValue (key, fallback); }
    void set (const juce::String& key, int v) { props->setValue (key, v); changed(); }
    void changed();
    static juce::String key (Folder);

    std::unique_ptr<juce::PropertiesFile> props;
};
} // namespace emx
