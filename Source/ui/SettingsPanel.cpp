// SPDX-License-Identifier: AGPL-3.0-or-later
#include "SettingsPanel.h"
#include "Theme.h"
#include "../PluginProcessor.h"

namespace emx
{
using namespace juce;

//== One folder category: list of paths + add / remove ============================
class SettingsPanel::FolderList final : public Component, private ListBoxModel
{
public:
    FolderList (Settings& s, Settings::Folder w, String t, String h) : settings (s), which (w), title (std::move (t)), help (std::move (h))
    {
        list.setRowHeight (17);
        list.setColour (ListBox::backgroundColourId, Colours::transparentBlack);
        list.setColour (ListBox::outlineColourId, Colours::transparentBlack);
        addAndMakeVisible (list);
        addButton.onClick = [this]
        {
            chooser = std::make_unique<FileChooser> ("Add a folder", File::getSpecialLocation (File::userHomeDirectory));
            chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories,
                                  [this] (const FileChooser& fc)
                                  {
                                      if (fc.getResult().isDirectory())
                                          settings.addFolder (which, fc.getResult());
                                  });
        };
        removeButton.onClick = [this]
        {
            const auto row = list.getSelectedRow();
            if (row >= 0 && row < paths.size())
                settings.removeFolder (which, paths[row]);
        };
        addAndMakeVisible (addButton);
        addAndMakeVisible (removeButton);
        refresh();
    }

    void refresh()
    {
        paths = settings.getFolders (which);
        list.updateContent();
        removeButton.setEnabled (! paths.isEmpty());
        repaint();
    }

    int getNumRows() override { return paths.size(); }
    void paintListBoxItem (int row, Graphics& g, int w, int h, bool selected) override
    {
        if (selected)
        {
            g.setColour (theme::red.withAlpha (0.2f));
            g.fillRoundedRectangle (Rectangle<float> (0, 0, (float) w, (float) h).reduced (2.0f, 1.0f), 3.0f);
        }
        const File f (paths[row]);
        g.setColour (f.isDirectory() ? Colour (0xffdd3333) : Colour (0xffffb030));
        g.setFont (theme::font (8.5f, 500, 0.02f));
        g.drawText (paths[row] + (f.isDirectory() ? "" : "  (NOT FOUND)"), 7, 0, w - 10, h, Justification::centredLeft, true);
    }
    void deleteKeyPressed (int) override { removeButton.triggerClick(); }

    void resized() override
    {
        auto r = getLocalBounds();
        r.removeFromTop (30);
        auto buttons = r.removeFromBottom (22);
        r.removeFromBottom (5);
        listArea = r.toFloat();
        list.setBounds (r.reduced (3));
        addButton.setBounds (buttons.removeFromLeft (70));
        buttons.removeFromLeft (5);
        removeButton.setBounds (buttons.removeFromLeft (76));
    }

    void paint (Graphics& g) override
    {
        auto head = getLocalBounds().removeFromTop (30).toFloat();
        g.setColour (Colours::white);
        g.setFont (theme::font (9.5f, 700, 0.12f));
        g.drawText (title, head.removeFromTop (15.0f), Justification::centredLeft, true);
        g.setColour (theme::textMuted);
        g.setFont (theme::font (7.5f, 500, 0.06f));
        g.drawText (help, head, Justification::topLeft, true);
        theme::drawLed (g, listArea, {});
        if (paths.isEmpty())
        {
            g.setColour (theme::redDark);
            g.setFont (theme::font (8.0f, 500, 0.05f));
            g.drawText ("NO FOLDERS", listArea.reduced (8.0f, 5.0f), Justification::topLeft, true);
        }
    }

private:
    Settings& settings;
    Settings::Folder which;
    String title, help;
    StringArray paths;
    ListBox list { "folders", this };
    TextButton addButton { "ADD..." }, removeButton { "REMOVE" };
    std::unique_ptr<FileChooser> chooser;
    Rectangle<float> listArea;
};

//== Settings panel ==============================================================
template <typename T, size_t N>
static int indexOf (const T (&arr)[N], T value, int fallback)
{
    for (size_t i = 0; i < N; ++i)
        if (std::abs ((double) arr[i] - (double) value) < 1.0e-3)
            return (int) i;
    return fallback;
}

static const int bendChoices[] { 1, 2, 3, 4, 5, 7, 12, 24 };
static const float tuningChoices[] { 415.0f, 430.0f, 432.0f, 435.0f, 438.0f, 440.0f, 442.0f, 443.0f, 444.0f, 446.0f };

SettingsPanel::SettingsPanel (EmulonXProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    auto& s = *settings;

    for (int i = 0; i < 5; ++i)
    {
        navButtons[i].onClick = [this, i] { showPage (i); };
        addAndMakeVisible (navButtons[i]);
    }
    doneButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (doneButton);

    //== Folders =================================================================
    folderLists.add (new FolderList (s, Settings::Folder::base, "BASE BANKS", "WHERE THE BANKS THAT COME WITH EMULON X LIVE"));
    folderLists.add (new FolderList (s, Settings::Folder::other, "OTHER BANKS", ".EMXB BANKS FROM OTHER PEOPLE - SHOWN AS FOLDERS OF PRESETS"));
    folderLists.add (new FolderList (s, Settings::Folder::user, "USER PRESETS", "YOUR .EMXP PRESETS - THE FIRST FOLDER IS WHERE SAVE GOES"));
    folderLists.add (new FolderList (s, Settings::Folder::samples, "SAMPLE FOLDERS", "AUDIO FILES SHOWN IN THE EXPLORER'S SAMPLES TAB"));
    for (auto* f : folderLists)
        addChildComponent (f);

    //== Performance =============================================================
    StringArray voices, cpu;
    for (auto v : Settings::voiceChoices) voices.add (String (v) + " VOICES");
    for (auto c : Settings::cpuChoices)   cpu.add (c == 0 ? String ("OFF") : String (c) + " %");

    addRow (1, "MAXIMUM VOICES", "HOW MANY NOTES CAN SOUND AT ONCE. FEWER = LESS CPU.", voices,
            [&s] { return indexOf (Settings::voiceChoices, s.getMaxVoices(), 3); },
            [&s] (int i) { s.setMaxVoices (Settings::voiceChoices[i]); });
    addRow (1, "CPU LIMIT", "ABOVE THIS LOAD, EMULON X FADES OUT VOICES TO AVOID CRACKLES.", cpu,
            [&s] { return indexOf (Settings::cpuChoices, s.getCpuLimit(), 5); },
            [&s] (int i) { s.setCpuLimit (Settings::cpuChoices[i]); });
    addRow (1, "WHEN VOICES RUN OUT", "WHICH NOTE MAKES ROOM FOR A NEW ONE (RELEASED NOTES GO FIRST).",
            { "STEAL OLDEST", "STEAL QUIETEST" },
            [&s] { return s.getStealQuietest() ? 1 : 0; },
            [&s] (int i) { s.setStealQuietest (i == 1); });
    addRow (1, "INTERPOLATION FOR NEW PRESETS", "SINC HQ SOUNDS CLEANEST, LINEAR IS LIGHTEST ON THE CPU.",
            { "LINEAR", "CUBIC", "SINC HQ" },
            [&s] { return jlimit (0, 2, s.getDefaultInterp()); },
            [&s] (int i) { s.setDefaultInterp (i); });
    addRow (1, "KEEP SPEED FOR NEW SAMPLES", "NEW ZONES PITCH WITHOUT CHANGING LENGTH, LIKE THE EMULATOR X.",
            { "ON", "OFF" },
            [&s] { return s.getDefaultKeepSpeed() ? 0 : 1; },
            [&s] (int i) { s.setDefaultKeepSpeed (i == 0); });
    addRow (1, "SAMPLES IN DAW PROJECTS", "STORE SAMPLES INSIDE YOUR ABLETON PROJECT, SO IT OPENS ANYWHERE.",
            { "STORE INSIDE THE PROJECT", "LINK TO THE FILES ONLY" },
            [&s] { return s.getEmbedSamples() ? 0 : 1; },
            [&s] (int i) { s.setEmbedSamples (i == 0); });

    //== Audio & MIDI ============================================================
    StringArray channels { "OMNI (ALL)" }, bends, tunings;
    for (int c = 1; c <= 16; ++c) channels.add ("CHANNEL " + String (c));
    for (auto b : bendChoices) bends.add ("+/- " + String (b) + (b == 1 ? " SEMITONE" : " SEMITONES"));
    for (auto t : tuningChoices) tunings.add ("A4 = " + String (roundToInt (t)) + " HZ");

    addRow (2, "MIDI CHANNEL", "WHICH CHANNEL EMULON X LISTENS TO.", channels,
            [&s] { return jlimit (0, 16, s.getMidiChannel()); },
            [&s] (int i) { s.setMidiChannel (i); });
    addRow (2, "VELOCITY CURVE", "SOFT MAKES GENTLE PLAYING LOUDER, HARD NEEDS MORE FORCE.",
            { "SOFT", "LINEAR", "HARD" },
            [&s] { return jlimit (0, 2, s.getVelocityCurve()); },
            [&s] (int i) { s.setVelocityCurve (i); });
    addRow (2, "PITCH BEND RANGE", "HOW FAR THE PITCH WHEEL BENDS.", bends,
            [&s] { return indexOf (bendChoices, s.getBendRange(), 1); },
            [&s] (int i) { s.setBendRange (bendChoices[i]); });
    addRow (2, "MASTER TUNING", "REFERENCE PITCH FOR THE WHOLE INSTRUMENT.", tunings,
            [&s]
            {
                for (int i = 0; i < (int) std::size (tuningChoices); ++i)
                    if (std::abs (tuningChoices[i] - s.getA4()) < 0.01f)
                        return i;
                return 5;
            },
            [&s] (int i) { s.setA4 (tuningChoices[i]); });
    panicButton.setTooltip ("Stops every sounding note immediately");
    panicButton.onClick = [this] { proc.panic(); };
    addChildComponent (panicButton);

    //== Interface ===============================================================
    StringArray sizes;
    for (auto sc : Settings::uiScales) sizes.add (String (roundToInt (sc * 100.0f)) + " %");
    addRow (3, "INTERFACE SIZE", "HOW BIG THE EMULON X WINDOW IS.", sizes,
            [&s] { return indexOf (Settings::uiScales, s.getUiScale(), 1); },
            [&s] (int i) { s.setUiScale (Settings::uiScales[i]); });
    addRow (3, "KNOB SENSITIVITY", "HOW FAR YOU DRAG THE MOUSE FOR A FULL TURN OF A KNOB.",
            { "FAST", "NORMAL", "FINE" },
            [&s] { return jlimit (0, 2, s.getKnobSensitivity()); },
            [&s] (int i) { s.setKnobSensitivity (i); });
    addRow (3, "HELP TIPS", "SHOW A SHORT EXPLANATION WHEN THE MOUSE RESTS ON SOMETHING.",
            { "ON", "OFF" },
            [&s] { return s.getShowHints() ? 0 : 1; },
            [&s] (int i) { s.setShowHints (i == 0); });
    resetButton.setTooltip ("Puts every setting except your folders back to its default");
    resetButton.onClick = [this] { settings->resetToDefaults(); };
    addChildComponent (resetButton);

    //== About ===================================================================
    siteLink.setFont (theme::font (10.0f, 700, 0.04f), false, Justification::centredLeft);
    siteLink.setColour (HyperlinkButton::textColourId, theme::red);
    addChildComponent (siteLink);

    settings->addChangeListener (this);
    refresh();
    showPage (0);
}

SettingsPanel::~SettingsPanel() { settings->removeChangeListener (this); }

SettingsPanel::Row& SettingsPanel::addRow (int pageIndex, const String& title, const String& help, const StringArray& items,
                                           std::function<int()> read, std::function<void (int)> write)
{
    auto* row = rows.add (new Row());
    row->title = title;
    row->help = help;
    row->page = pageIndex;
    row->read = std::move (read);
    row->write = std::move (write);
    row->box.addItemList (items, 1);
    row->box.onChange = [row]
    {
        const int idx = row->box.getSelectedItemIndex();
        if (idx >= 0 && idx != row->read())
            row->write (idx);
    };
    addChildComponent (row->box);
    return *row;
}

void SettingsPanel::changeListenerCallback (ChangeBroadcaster*) { refresh(); }

void SettingsPanel::refresh()
{
    for (auto* r : rows)
        r->box.setSelectedItemIndex (r->read(), dontSendNotification);
    for (auto* f : folderLists)
        f->refresh();
}

void SettingsPanel::showPage (int index)
{
    page = index;
    for (int i = 0; i < 5; ++i)
        navButtons[i].setToggleState (i == page, dontSendNotification);
    for (auto* f : folderLists)
        f->setVisible (page == 0);
    for (auto* r : rows)
        r->box.setVisible (r->page == page);
    panicButton.setVisible (page == 2);
    resetButton.setVisible (page == 3);
    siteLink.setVisible (page == 4);
    resized();
    repaint();
}

void SettingsPanel::resized()
{
    panel = getLocalBounds().withSizeKeepingCentre (860, 560);
    auto r = panel.reduced (20, 16);
    auto head = r.removeFromTop (28);
    doneButton.setBounds (head.removeFromRight (90).withSizeKeepingCentre (90, 26));
    r.removeFromTop (12);

    auto nav = r.removeFromLeft (150);
    for (auto& b : navButtons)
    {
        b.setBounds (nav.removeFromTop (30));
        nav.removeFromTop (8);
    }
    r.removeFromLeft (18);
    content = r;

    // Folders: a 2 x 2 grid
    auto grid = content.reduced (12, 12);
    const int cw = (grid.getWidth() - 16) / 2, ch = (grid.getHeight() - 16) / 2;
    for (int i = 0; i < folderLists.size(); ++i)
        folderLists[i]->setBounds (grid.getX() + (i % 2) * (cw + 16), grid.getY() + (i / 2) * (ch + 16), cw, ch);

    // Choice rows
    int y = content.getY() + 16;
    int lastPage = -1;
    for (auto* row : rows)
    {
        if (row->page != lastPage)
        {
            y = content.getY() + 16;
            lastPage = row->page;
        }
        row->box.setBounds (content.getRight() - 16 - 250, y + 6, 250, 26);
        y += 54;
    }
    panicButton.setBounds (content.getX() + 16, content.getY() + 16 + 4 * 54 + 12, 150, 28);
    resetButton.setBounds (content.getX() + 16, content.getY() + 16 + 3 * 54 + 12, 150, 28);
    siteLink.setBounds (content.getX() + 16, content.getY() + 128, 400, 22);
}

void SettingsPanel::paint (Graphics& g)
{
    g.fillAll (Colours::black.withAlpha (0.72f));
    theme::drawPanel (g, panel.toFloat(), 12.0f);

    auto r = panel.reduced (20, 16).toFloat();
    auto head = r.removeFromTop (28.0f);
    theme::drawGlowText (g, "SETTINGS", head, theme::font (16.0f, 900, 0.16f), Colours::white, theme::red.withAlpha (0.7f), 8);
    g.setColour (theme::textMuted);
    g.setFont (theme::font (8.0f, 500, 0.1f));
    g.drawText ("SAVED AUTOMATICALLY - SHARED BY EVERY EMULON X IN EVERY PROJECT", head.withTrimmedLeft (130.0f).withTrimmedRight (110.0f),
                Justification::centredLeft, true);

    theme::drawInset (g, content.toFloat());

    if (page >= 1 && page <= 3)
    {
        int y = content.getY() + 16;
        for (auto* row : rows)
        {
            if (row->page != page)
                continue;
            auto line = Rectangle<float> ((float) content.getX() + 16.0f, (float) y, (float) content.getWidth() - 290.0f, 38.0f);
            g.setColour (Colours::white);
            g.setFont (theme::font (9.5f, 700, 0.1f));
            g.drawText (row->title, line.removeFromTop (19.0f), Justification::bottomLeft, true);
            g.setColour (theme::textMuted);
            g.setFont (theme::font (7.5f, 500, 0.05f));
            g.drawText (row->help, line, Justification::topLeft, true);
            g.setColour (Colours::black.withAlpha (0.35f));
            g.fillRect ((float) content.getX() + 16.0f, (float) y + 46.0f, (float) content.getWidth() - 32.0f, 1.0f);
            y += 54;
        }
    }

    if (page == 4)
    {
        auto a = content.reduced (16).toFloat();
        theme::drawGlowText (g, "EMULON X", a.removeFromTop (34.0f), theme::font (24.0f, 900, 0.16f), Colours::white, theme::red.withAlpha (0.8f), 10);
        g.setColour (theme::red);
        g.setFont (theme::font (10.0f, 700, 0.1f));
        g.drawText ("VERSION " EMX_VERSION_STRING, a.removeFromTop (20.0f), Justification::centredLeft, false);
        a.removeFromTop (10.0f);
        g.setColour (theme::textCol);
        g.setFont (theme::font (9.0f, 500, 0.04f));
        g.drawFittedText ("A free and open-source sampler - a spiritual successor to E-mu's Emulator X.\n"
                          "Licensed under the GNU AGPL v3. Built with JUCE. Orbitron font: SIL Open Font License.",
                          a.removeFromTop (40.0f).toNearestInt(), Justification::topLeft, 3, 1.0f);
        a.removeFromTop (40.0f);   // link sits here
        g.setColour (theme::textMuted);
        g.setFont (theme::font (8.0f, 500, 0.06f));
        g.drawFittedText ("SETTINGS FILE: " + settings->properties().getFile().getFullPathName(),
                          a.removeFromTop (30.0f).toNearestInt(), Justification::topLeft, 2, 1.0f);
    }
}

void SettingsPanel::mouseDown (const MouseEvent& e)
{
    if (! panel.contains (e.getPosition()) && onClose)
        onClose();
}

bool SettingsPanel::keyPressed (const KeyPress& k)
{
    if (k == KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }
    return false;
}
} // namespace emx
