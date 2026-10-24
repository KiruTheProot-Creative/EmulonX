// Emulon X — skin files (.emxd): what a custom skin contains, and reading / writing them.
// The format is described in docs/SKINS.md. Emulon X SD (the skin designer) writes these files.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_graphics/juce_graphics.h>
#include <functional>
#include <map>

namespace emx::skin
{
/** A picture from the skin file, plus the name it has inside the file (kept for saving). */
struct Picture
{
    juce::Image image;
    juce::String file;
    bool isValid() const noexcept { return image.isValid(); }
};

/** How a surface is filled. */
struct Fill
{
    enum class Type { solid, gradient, image };
    enum class Fit { stretch, cover, tile };
    Type type = Type::solid;
    juce::Colour a { 0xff202020 }, b { 0xff101010 };   // solid uses a; gradient goes a -> b
    float angle = 180.0f;                              // like CSS: 180 = top to bottom
    Picture picture;                                   // for Type::image (a is shown behind it)
    Fit fit = Fit::stretch;

    /** One colour that stands for the fill (for readable text and the colour translation). */
    juce::Colour average() const { return type == Type::gradient ? a.interpolatedWith (b, 0.5f) : a; }
};

/** A grayscale texture blended over a surface ("overlay" blend), stretched to the surface. */
struct Overlay
{
    juce::String name;      // "metal-brushed", "custom", ... (as written in the file)
    Picture texture;        // the texture as the file holds it
    juce::Image lighter, darker;   // prepared for drawing: the parts lighter / darker than mid-grey
    float strength = 0.6f;
    bool softLight = false;
    bool isActive() const noexcept { return lighter.isValid() && strength > 0.001f; }
};

/** A rounded surface: panels, knob areas, displays, buttons, tabs. */
struct Box
{
    Fill fill;
    float radius = 6.0f;          // corner size in pixels
    float shape = 0.5f;           // corner roundness: 0 cut straight, 0.5 round, 1 squircle
    float outline = 1.0f;
    juce::Colour outlineColour { 0xff000000 };
    float shadowY = 0.0f, shadowBlur = 0.0f, shadowOpacity = 0.0f;
    Overlay overlay;
};

/** Everything a custom skin defines, with links to the palette already resolved. */
struct Design
{
    juce::String name { "Custom skin" }, author;
    bool hueShift = true;        // may players turn this skin to another colour?
    juce::Colour accent { 0xffff2a2a }, text { 0xffe8e4e4 }, muted { 0xffb8a6a6 },
                 surface { 0xff1a181d }, background { 0xff120203 }, displayColour { 0xff070203 };

    juce::String fontName { "Saira" };
    juce::MemoryBlock fontData;  // a font file inside the skin (ttf / otf), if any
    juce::String fontFile;
    bool upperCase = true;

    // window
    Fill window;
    enum class Pattern { none, grid, noise };
    Pattern pattern = Pattern::none;
    juce::Colour patternColour { 0xffff2a2a };
    float patternOpacity = 0.12f;
    Overlay windowOverlay;
    float frameWidth = 3.0f;
    juce::Colour frameColour { 0xff000000 };

    // top bar and logo
    Fill topBar;
    float topLine = 1.0f;
    juce::Colour topLineColour { 0xff000000 }, logoColour { 0xffffffff };
    float logoGlow = 0.8f;
    Picture logo;
    Overlay topBarOverlay;

    // surfaces and readouts
    Box panel;
    juce::Colour heading { 0xffe8e4e4 };
    Box inset;
    float insetDepth = 0.6f;
    Box display;
    juce::Colour displayText { 0xffff2a2a };
    float displayGlow = 0.55f;
    bool scanlines = true;
    juce::Colour list { 0xffff4a4a };

    // controls
    Box button;
    juce::Colour buttonText { 0xffdddddd };
    float bevel = 0.5f;
    Box buttonOn;
    juce::Colour buttonOnText { 0xffffffff };
    float buttonOnGlow = 0.45f;
    bool tabsLikeButtons = true;
    Box tab, tabOn;
    juce::Colour tabText { 0xffdddddd };

    enum class KnobStyle { cap, ring, picture };
    KnobStyle knobStyle = KnobStyle::cap;
    juce::Colour capLight { 0xff5a5560 }, capDark { 0xff141217 };
    float ring = 0.0f;
    juce::Colour ringTrack { 0xff2a262c }, ringValue { 0xffff2a2a }, pointer { 0xffff2a2a };
    float pointerGlow = 0.9f;
    Picture knobPicture;
    Overlay knobOverlay;

    juce::Colour faderTrack { 0xff2a262c }, faderFill { 0xffff2a2a }, thumbTop { 0xff8a8590 }, thumbBottom { 0xff202025 };
    Picture thumbPicture;
    Overlay faderOverlay;

    juce::Colour label { 0xffe8e4e4 };

    /** True if the surfaces are light (dark text on them). */
    bool isLight() const { return surface.getPerceivedBrightness() > 0.6f; }
};

/** Reads a skin file (.emxd zip, or a plain skin.json as Skin Studio exports it).
    Returns false and fills `error` with a short reason if the file can't be used.
    Problems that don't stop the skin (a missing picture, say) are added to `warnings`. */
bool load (const juce::File&, Design&, juce::String& error, juce::StringArray* warnings = nullptr);

/** Builds a design from the skin description. `fileData` returns the bytes of a file inside the skin
    ("images/knob.png"), or an empty block. */
bool fromJson (const juce::var& json, const std::function<juce::MemoryBlock (const juce::String&)>& fileData,
               Design&, juce::String& error, juce::StringArray* warnings = nullptr);

/** Writes an .emxd file: `json` (the skin description) plus the files it refers to. */
bool save (const juce::File&, const juce::var& json, const std::map<juce::String, juce::MemoryBlock>& files, juce::String& error);

/** Quick look at a skin file: its name and author, without loading the pictures. */
bool readInfo (const juce::File&, juce::String& name, juce::String& author);

/** All skin files (.emxd) in these folders (not in sub-folders), sorted by name. */
juce::Array<juce::File> findSkins (const juce::StringArray& folders);

/** Parses "#rrggbb" / "#rgb" (also without #); false if it isn't a colour. */
bool parseColour (const juce::String&, juce::Colour&);

/** Calls `fn` on every colour of the design (used to turn a skin to another colour). */
void forEachColour (Design&, const std::function<void (juce::Colour&)>& fn);

/** Prepares a grayscale texture for the overlay blend (see Overlay::lighter / darker). */
void makeBlendImages (const juce::Image& texture, juce::Image& lighter, juce::Image& darker, int maxSize = 512);
} // namespace emx::skin
