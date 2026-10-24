// Emulon X SD — the skin being designed: its description (skin.json, with palette links),
// the pictures and fonts inside it, undo / redo, opening and saving .emxd files.
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ui/SkinFile.h"

namespace emx::sd
{
class SkinDocument final : public juce::ChangeBroadcaster
{
public:
    SkinDocument();

    //== The description =========================================================
    /** A value by its path, like "panel.fill.a" (void if it isn't there). */
    juce::var get (const juce::String& path) const;
    /** Changes a value. Changes with the same `undoTag` close together become one undo step
        (dragging a slider). */
    void set (const juce::String& path, const juce::var& value, const juce::String& undoTag = {});
    /** Removes a value (back to the default). */
    void remove (const juce::String& path, const juce::String& undoTag = {});
    /** A colour value, with "@accent"-style links followed to the palette. */
    juce::Colour colour (const juce::String& path, juce::Colour fallback) const;
    juce::Colour token (const juce::String& name) const;
    static const juce::StringArray& tokenNames();    // accent, text, muted, surface, background, display
    static const juce::StringArray& tokenTitles();   // ACCENT, TEXT, SOFT TEXT, ...

    juce::String getName() const;
    void setName (const juce::String&);
    juce::String getAuthor() const;
    void setAuthor (const juce::String&);

    //== Files inside the skin (pictures, textures, a font) ======================
    /** Adds a file and returns the name it has inside the skin ("images/knob.png"). */
    juce::String addFile (const juce::String& folder, const juce::String& originalName, const juce::MemoryBlock&);
    /** A file of the skin, or one of the built-in textures ("textures/metal-brushed.jpg"); empty if missing. */
    juce::MemoryBlock fileData (const juce::String& name) const;

    /** The built-in overlay textures: ids ("metal-brushed") and titles ("BRUSHED"). */
    static const juce::StringArray& builtInTextures();
    static const juce::StringArray& builtInTextureTitles();
    static juce::MemoryBlock builtInTexture (const juce::String& id);

    /** Your own textures in this skin (added with "+", or used by any part). */
    juce::StringArray ownTextures() const;
    void addOwnTexture (const juce::String& file);
    void removeOwnTexture (const juce::String& file);
    /** True if some part other than `exceptPrefix` uses this texture. */
    bool textureInUse (const juce::String& file, const juce::String& exceptPrefix = {}) const;

    //== Starting points =========================================================
    static const juce::StringArray& presetTitles();   // EMULON CLASSIC, EMULON XP - LIGHT, ...
    /** Starts over from a built-in skin (keeps the name and author). Can be undone. */
    void startFrom (int presetIndex);
    /** A fresh document (Classic, "My Skin"), nothing to undo. */
    void reset();

    //== Files ===================================================================
    bool open (const juce::File&, juce::String& error);
    /** Writes the .emxd file: the description plus every file it uses (built-in textures included). */
    bool save (const juce::File&, juce::String& error);
    juce::File getFile() const { return file; }
    bool hasUnsavedChanges() const { return revision != savedRevision; }

    //== Undo ====================================================================
    bool canUndo() const { return ! undoStack.empty(); }
    bool canRedo() const { return ! redoStack.empty(); }
    void undo();
    void redo();
    /** Changes every time the skin changes (for anyone redrawing). */
    int getRevision() const noexcept { return revision; }
    /** Changes when the shape of the description changes (undo, new skin, open): rebuild the controls. */
    int getStructureRevision() const noexcept { return structureRevision; }

    //== What it looks like ======================================================
    /** The skin as Emulon X draws it (rebuilt after changes). */
    std::shared_ptr<const skin::Design> design() const;
    const juce::StringArray& getWarnings() const { design(); return warnings; }

    //== The work in progress is kept between sessions ===========================
    void saveDraft();
    bool loadDraft();

private:
    juce::var& at (const juce::String& path, bool create);
    void changed (bool structure);
    void pushUndo (const juce::String& tag);
    juce::String snapshot() const;
    void restore (const juce::String&);
    void importDataUrls (juce::var&);
    void migrate();
    std::map<juce::String, juce::MemoryBlock> usedFiles() const;

    juce::var json;
    std::map<juce::String, juce::MemoryBlock> files;
    juce::File file;
    std::vector<juce::String> undoStack, redoStack;
    juce::String lastTag;
    double lastChange = 0.0;
    int revision = 0, savedRevision = 0, structureRevision = 0;
    mutable int designRevision = -1;
    mutable std::shared_ptr<const skin::Design> cachedDesign;
    mutable juce::StringArray warnings;
};
} // namespace emx::sd
