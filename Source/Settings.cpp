// SPDX-License-Identifier: AGPL-3.0-or-later
#include "Settings.h"

namespace emx
{
/** A folder the Windows installer chose (it stores them in the registry); empty elsewhere. */
static juce::String installerFolder (const char* name)
{
   #if JUCE_WINDOWS
    return juce::WindowsRegistry::getValue (juce::String ("HKEY_LOCAL_MACHINE\\Software\\Emulon X\\") + name).trim();
   #else
    juce::ignoreUnused (name);
    return {};
   #endif
}

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

    // First run: the folders picked in the installer, or the standard ones in Documents.
    const auto docs = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Emulon X");
    const auto baseBanks = docs.getChildFile ("Base Banks");
    const auto instBase = installerFolder ("BaseBanksFolder"), instUser = installerFolder ("PresetsFolder");
    if (! props->containsKey ("folders.user"))
    {
        props->setValue ("folders.user", instUser.isNotEmpty() ? instUser : docs.getChildFile ("Presets").getFullPathName());
        props->setValue ("folders.base", instBase.isNotEmpty() ? instBase : baseBanks.getFullPathName());
        props->setValue ("installer.base", instBase);
        props->setValue ("installer.presets", instUser);
    }
    else if (baseBanks.isDirectory())
    {
        // Settings from before the installer: swap the old suggested folder for "Base Banks".
        auto base = getFolders (Folder::base);
        const auto before = base;
        const auto oldDefault = docs.getChildFile ("Banks").getFullPathName();
        if (base.contains (oldDefault) && ! juce::File (oldDefault).isDirectory())
            base.removeString (oldDefault);
        base.addIfNotAlreadyThere (baseBanks.getFullPathName());
        if (base != before)
            props->setValue ("folders.base", base.joinIntoString ("\n"));
    }

    // Installed again with different folders: add them (presets first, so Save goes there).
    // Folders you added or removed yourself in Settings stay as they are otherwise.
    if (instBase.isNotEmpty() && instBase != props->getValue ("installer.base"))
    {
        auto base = getFolders (Folder::base);
        base.addIfNotAlreadyThere (instBase);
        props->setValue ("folders.base", base.joinIntoString ("\n"));
        props->setValue ("installer.base", instBase);
    }
    if (instUser.isNotEmpty() && instUser != props->getValue ("installer.presets"))
    {
        auto user = getFolders (Folder::user);
        user.removeString (instUser);
        user.insert (0, instUser);
        props->setValue ("folders.user", user.joinIntoString ("\n"));
        props->setValue ("installer.presets", instUser);
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
                    "defaultKeepSpeed", "embedSamples", "midiChannel", "velocityCurve", "bendRange", "a4", "hueShift", "bankColumns" })
        props->removeValue (k);
    changed();
}

void Settings::changed()
{
    props->saveIfNeeded();
    sendChangeMessage();
}
} // namespace emx
