// Emulon X — global, per-user settings (shared by every plugin instance).
// SPDX-License-Identifier: AGPL-3.0-or-later
#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace emx
{
/** Stored in the user's settings folder, e.g.
    Windows: %APPDATA%\Emulon X\Emulon X.settings
    macOS:   ~/Library/Application Support/Emulon X/Emulon X.settings

    Use through juce::SharedResourcePointer<Settings> so all instances share one file. */
class Settings
{
public:
    Settings();

    /** The interface-size presets offered in the gear menu. */
    static constexpr float uiScales[] { 0.75f, 1.0f, 1.25f, 1.5f, 2.0f };

    float getUiScale() const;
    void  setUiScale (float newScale);

    juce::PropertiesFile& properties() noexcept { return *props; }

private:
    std::unique_ptr<juce::PropertiesFile> props;
};
} // namespace emx
