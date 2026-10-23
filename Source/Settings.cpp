// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Settings.h"

namespace emx
{
Settings::Settings()
{
    juce::PropertiesFile::Options o;
    o.applicationName     = "Emulon X";
    o.filenameSuffix      = "settings";
    o.folderName          = "Emulon X";
    o.osxLibrarySubFolder = "Application Support";
    o.storageFormat       = juce::PropertiesFile::storeAsXML;
    props = std::make_unique<juce::PropertiesFile> (o);
}

float Settings::getUiScale() const
{
    const auto s = (float) props->getDoubleValue ("uiScale", 1.0);
    for (auto allowed : uiScales)
        if (std::abs (allowed - s) < 0.01f)
            return allowed;
    return 1.0f;
}

void Settings::setUiScale (float newScale)
{
    props->setValue ("uiScale", (double) newScale);
    props->saveIfNeeded();
}
} // namespace emx
