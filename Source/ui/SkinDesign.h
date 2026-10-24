// Emulon X — custom skins (.emxd, "Emulon X Design").
// A .emxd file is a zip with skin.json plus image and font files. An unpacked folder with the
// same files works too (that is what "Save template" writes and "Load folder" reads).
// See docs/SKINS.md for the full format.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

namespace emx
{
struct SkinDesign
{
    // Everything a skin can replace. Missing parts keep the EMULON CLASSIC look.
    enum Part { background, frame, knob, fader, panel, inset, display, button, buttonOn, logo, faderKnob, numParts };
    static const char* partKey (int part);     // file name without extension, e.g. "button-on"
    static const char* partTitle (int part);   // shown in the designer
    static const char* partHelp (int part);    // size / how it is drawn

    // Colours that can be set (unset ones follow the accent colour).
    static constexpr int numColours = 6;
    static const char* colourKey (int i);      // accent, text, muted, display-text, display-bg, list
    static const char* colourTitle (int i);

    struct Blob
    {
        juce::MemoryBlock data;
        juce::String ext;   // "png", "jpg", "ttf", ...
        bool isEmpty() const noexcept { return data.isEmpty(); }
    };

    juce::String name { "My Skin" }, author;
    bool hueShift = true;      // may the colour shift in Settings move this skin?
    bool knobRotates = false;  // false: the knob image stays and a pointer turns; true: the image itself turns
    bool shadows = true;       // a slight drop shadow under the skin's pictures
    bool keepCorners = false;  // panels / buttons / displays: false = the whole picture fills them; true = 9-slice (corners kept)
    std::map<juce::String, juce::Colour> colours;
    Blob images[numParts];
    Blob font;

    bool isEmpty() const;

    /** Reads a .emxd file or a folder. */
    static bool load (const juce::File& emxdOrFolder, SkinDesign& out, juce::String& error);
    /** Writes a .emxd file. */
    bool save (const juce::File& emxd, juce::String& error) const;
    /** Writes the parts as loose files plus skin.json into a folder. */
    bool saveFolder (const juce::File& folder, juce::String& error) const;

    juce::var toJson() const;
    void fromJson (const juce::var&);
};
} // namespace emx
