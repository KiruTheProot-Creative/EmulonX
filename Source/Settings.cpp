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
    o.millisecondsBeforeSaving = 500;
    props = std::make_unique<juce::PropertiesFile> (o);

    // First run: suggest the standard Emulon X folders in Documents.
    if (! props->containsKey ("folders.user"))
    {
        const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Emulon X");
        props->setValue ("folders.user", docs.getChildFile ("Presets").getFullPathName());
        props->setValue ("folders.base", docs.getChildFile ("Banks").getFullPathName());
    }
}

juce::String Settings::key (Folder f)
{
    switch (f)
    {
        case Folder::base:    return "folders.base";
        case Folder::other:   return "folders.other";
        case Folder::user:    return "folders.user";
        case Folder::samples: return "folders.samples";
    }
    return {};
}

juce::StringArray Settings::getFolders (Folder f) const
{
    auto list = juce::StringArray::fromLines (props->getValue (key (f)));
    list.removeEmptyStrings();
    return list;
}

void Settings::setFolders (Folder f, const juce::StringArray& list)
{
    props->setValue (key (f), list.joinIntoString ("\n"));
    changed();
}

void Settings::addFolder (Folder f, const juce::File& dir)
{
    auto list = getFolders (f);
    list.addIfNotAlreadyThere (dir.getFullPathName());
    setFolders (f, list);
}

void Settings::removeFolder (Folder f, const juce::String& path)
{
    auto list = getFolders (f);
    list.removeString (path);
    setFolders (f, list);
}

juce::File Settings::getUserPresetFolder() const
{
    const auto list = getFolders (Folder::user);
    juce::File dir = list.isEmpty() ? juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                          .getChildFile ("Emulon X").getChildFile ("Presets")
                                    : juce::File (list[0]);
    dir.createDirectory();
    return dir;
}

float Settings::getUiScale() const
{
    const auto s = (float) props->getDoubleValue ("uiScale", 1.0);
    for (auto allowed : uiScales)
        if (std::abs (allowed - s) < 0.01f)
            return allowed;
    return 1.0f;
}

void Settings::setUiScale (float s)
{
    props->setValue ("uiScale", (double) s);
    changed();
}

void Settings::resetToDefaults()
{
    for (auto k : { "uiScale", "knobSensitivity", "showHints", "maxVoices", "cpuLimit", "stealQuietest", "defaultInterp",
                    "defaultKeepSpeed", "embedSamples", "midiChannel", "velocityCurve", "bendRange", "a4" })
        props->removeValue (k);
    changed();
}

void Settings::changed()
{
    props->saveIfNeeded();
    sendChangeMessage();
}
} // namespace emx
