// Emulon X SD — the skin designer for Emulon X. Makes .emxd skin files.
//     EmulonXSD [skin.emxd]            opens the app (and the skin)
//     EmulonXSD --self-test <folder>   renders screenshots, checks saving / loading, then quits
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "MainComponent.h"
#include "ui/EmxLookAndFeel.h"
#include "ui/Theme.h"
#include "ui/SkinFile.h"

using namespace juce;

namespace
{
/** Screenshots of every part of the app plus checks that a saved skin loads in Emulon X. Returns the number of failures. */
int selfTest (const File& dir)
{
    dir.createDirectory();
    int failures = 0;
    auto check = [&failures] (bool ok, const String& what)
    {
        std::printf ("%s  %s\n", ok ? "PASS" : "FAIL", what.toRawUTF8());
        if (! ok) ++failures;
    };
    auto pump = [] { MessageManager::getInstance()->runDispatchLoopUntil (60); };

    emx::sd::MainComponent mc;
    mc.setSize (1440, 900);
    mc.doc.reset();
    pump();
    auto shot = [&] (const String& name)
    {
        pump();
        const auto img = mc.createComponentSnapshot (mc.getLocalBounds(), true, 1.0f);
        const auto f = dir.getChildFile (name + ".png");
        f.deleteFile();
        FileOutputStream out (f);
        PNGImageFormat().writeImageToStream (img, out);
        std::printf ("wrote %s\n", f.getFullPathName().toRawUTF8());
    };

    shot ("sd-start");
    for (auto key : { "button", "knob", "display", "tab", "type", "window" })
    {
        mc.select (key);
        shot (String ("sd-part-") + key);
    }
    const char* slugs[] { "classic", "xp-light", "xp-dark", "emurum", "emutal", "emublox", "emul8r" };
    for (int i = 1; i < emx::sd::SkinDocument::presetTitles().size(); ++i)
    {
        mc.doc.startFrom (i);
        mc.select ("panel");
        shot (String ("sd-preset-") + slugs[i]);
    }

    // edits, undo and redo
    auto& doc = mc.doc;
    doc.startFrom (0);
    doc.setName ("SD Self Test");
    doc.setAuthor ("Emulon X SD");
    doc.set ("panel.overlay.tex", "metal-brushed");
    doc.set ("panel.overlay.strength", 0.8);
    doc.set ("button.shape", 1.0);
    doc.set ("button.radius", 9.0);
    doc.set ("knob.ring", 3.0);
    doc.set ("knob.ringColor", "@accent");
    doc.set ("tokens.accent", "#22aaff");
    check (doc.design()->ringValue == Colour (0xff22aaff), "palette links follow the palette");
    doc.undo();
    check (doc.design()->ringValue == Colour (0xffff2a2a), "undo takes the last change back");
    doc.redo();
    check (doc.design()->ringValue == Colour (0xff22aaff), "redo puts it back");
    mc.select ("panel");
    shot ("sd-edited");

    // save -> Emulon X's own loader
    const auto file = dir.getChildFile ("SD Self Test.emxd");
    String error;
    check (doc.save (file, error), "save an .emxd file " + error);
    emx::skin::Design loaded;
    StringArray warnings;
    const bool ok = emx::skin::load (file, loaded, error, &warnings);
    check (ok && loaded.name == "SD Self Test" && loaded.author == "Emulon X SD", "Emulon X reads the saved skin");
    check (ok && loaded.panel.overlay.isActive(), "the built-in texture travels inside the skin file");
    check (ok && std::abs (loaded.button.shape - 1.0f) < 0.001f && std::abs (loaded.button.radius - 9.0f) < 0.001f, "corner shape is saved");
    check (ok && loaded.ringValue == Colour (0xff22aaff), "linked colours are saved as links and resolved");
    check (warnings.isEmpty(), "no missing pictures or textures " + warnings.joinIntoString ("; "));

    // open it again, as a fresh document
    doc.reset();
    check (doc.open (file, error) && doc.getName() == "SD Self Test" && doc.get ("knob.ringColor").toString() == "@accent",
           "open a saved skin again (links kept)");

    // effects, display options, knob layers, several own textures
    {
        doc.reset();
        doc.setName ("SD Effects");
        auto png = [] (Colour c, bool ring)
        {
            Image im (Image::ARGB, 64, 64, true);
            {
                Graphics ig (im);
                ig.setColour (c);
                if (ring) ig.drawEllipse (4, 4, 56, 56, 6); else { ig.fillEllipse (8, 8, 48, 48); ig.setColour (Colours::white); ig.fillRect (30, 8, 4, 22); }
            }
            MemoryOutputStream out;
            PNGImageFormat().writeImageToStream (im, out);
            return out.getMemoryBlock();
        };
        auto grey = [] (int seed)
        {
            Image im (Image::RGB, 64, 64, false);
            Random rnd (seed);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x)
                {
                    const auto v = (uint8) jlimit (0, 255, 128 + rnd.nextInt (90) - 45);
                    im.setPixelAt (x, y, Colour (v, v, v));
                }
            MemoryOutputStream out;
            PNGImageFormat().writeImageToStream (im, out);
            return out.getMemoryBlock();
        };
        const auto t1 = doc.addFile ("images", "grain-one.png", grey (1)), t2 = doc.addFile ("images", "grain-two.png", grey (2));
        doc.addOwnTexture (t1);
        doc.addOwnTexture (t2);
        check (doc.ownTextures().size() == 2, "several own textures can be added");
        doc.set ("panel.overlay.tex", "custom");
        doc.set ("panel.overlay.custom", t2);
        doc.set ("panel.overlay.strength", 0.7);
        doc.set ("button.fx.bloom", 0.8);
        doc.set ("button.fx.bloomSize", 10.0);
        doc.set ("buttonOn.fx.bloom", 1.0);
        doc.set ("heading.fx.bloom", 0.9);
        doc.set ("screen.same", false);
        doc.set ("screen.fill", JSON::parse ("{\"type\":\"solid\",\"a\":\"#0c2a26\"}"));
        doc.set ("screen.pattern", "grid");
        doc.set ("screen.patternColor", "#3fbcae");
        doc.set ("screen.glass", true);
        doc.set ("display.fx.bloom", 0.7);
        doc.set ("inset.fx.blur", 1.5);
        doc.set ("knob.style", "picture");
        doc.set ("knob.lower.picture", doc.addFile ("images", "knob-base.png", png (Colours::orange, true)));
        doc.set ("knob.lower.turns", false);
        doc.set ("knob.upper.picture", doc.addFile ("images", "knob-cap.png", png (Colour (0xff303030), false)));
        doc.set ("knob.upper.turns", true);
        doc.set ("knob.fx.bloom", 0.5);
        mc.select ("display");
        shot ("sd-effects-display");
        mc.select ("screen");
        shot ("sd-effects-screen");
        mc.select ("knob");
        shot ("sd-effects-knob");
        const auto fxFile = dir.getChildFile ("SD Effects.emxd");
        check (doc.save (fxFile, error), "save a skin with effects");
        emx::skin::Design fx;
        const bool okFx = emx::skin::load (fxFile, fx, error, &warnings);
        check (okFx && fx.button.fx.bloom > 0.79f && fx.inset.fx.blur > 1.4f && fx.headingFx.bloom > 0.89f, "effects are saved and read by Emulon X");
        check (okFx && fx.scanlines && fx.display.fill.a != Colour (0xff0c2a26), "displays keep their own look");
        check (okFx && ! fx.screensLikeDisplays && fx.graphPattern == emx::skin::Design::ScreenPattern::grid && fx.graphGlass
                   && fx.screen.fill.a == Colour (0xff0c2a26) && fx.graphPatternColour == Colour (0xff3fbcae),
               "screens (graph backgrounds) with their own look are saved");
        check (okFx && fx.knobLower.isValid() && fx.knobUpper.isValid() && ! fx.lowerTurns && fx.upperTurns, "both knob layers and their movement are saved");
        check (okFx && fx.panel.overlay.isActive(), "an own texture travels inside the skin");
    }

    // a skin.json from the Skin Studio prototype, with a picture inside it
    {
        Image im (Image::RGB, 8, 8, false);
        im.clear (im.getBounds(), Colours::orange);
        MemoryOutputStream png;
        PNGImageFormat().writeImageToStream (im, png);
        const auto json = "{\"format\":\"emxd\",\"version\":2,\"name\":\"From Studio\",\"button\":{\"fill\":{\"type\":\"image\",\"a\":\"#000000\","
                          "\"img\":\"data:image/png;base64," + Base64::toBase64 (png.getData(), png.getDataSize()) + "\"}}}";
        const auto jf = dir.getChildFile ("studio-export.json");
        jf.replaceWithText (json);
        check (doc.open (jf, error) && doc.get ("button.fill.img").toString().startsWith ("images/") && doc.design()->button.fill.picture.isValid(),
               "import a Skin Studio export (pictures move into the skin)");
        jf.deleteFile();
    }
    std::printf ("\n%d failed\n", failures);
    return failures;
}

class MainWindow final : public DocumentWindow
{
public:
    MainWindow() : DocumentWindow ("Emulon X SD", Colours::black, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        auto* mc = new emx::sd::MainComponent();
        mc->onTitleChanged = [this] (const String& t) { setName (t); };
        setContentOwned (mc, false);
        setResizable (true, false);
        setResizeLimits (1100, 700, 5000, 4000);
        centreWithSize (1440, 900);
        setVisible (true);
        main = mc;
    }
    void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    emx::sd::MainComponent* main = nullptr;
};
} // namespace

class EmulonXSDApplication final : public JUCEApplication
{
public:
    const String getApplicationName() override    { return "Emulon X SD"; }
    const String getApplicationVersion() override { return EMX_SD_VERSION; }
    bool moreThanOneInstanceAllowed() override    { return false; }

    void initialise (const String& commandLine) override
    {
        LookAndFeel::setDefaultLookAndFeel (&lnf);
        const auto args = StringArray::fromTokens (commandLine, true);
        if (args.contains ("--self-test"))
        {
            const auto dir = File::getCurrentWorkingDirectory().getChildFile (args[args.indexOf ("--self-test") + 1].unquoted());
            setApplicationReturnValue (selfTest (dir) == 0 ? 0 : 1);
            quit();
            return;
        }
        window = std::make_unique<MainWindow>();
        openFromCommandLine (commandLine);
    }

    void shutdown() override
    {
        window.reset();
        emx::sd::releaseFonts();
        LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const String& commandLine) override { openFromCommandLine (commandLine); }

private:
    void openFromCommandLine (const String& commandLine)
    {
        for (auto& a : StringArray::fromTokens (commandLine, true))
        {
            const File f (a.unquoted());
            if (window != nullptr && window->main != nullptr && f.existsAsFile() && (f.hasFileExtension ("emxd") || f.hasFileExtension ("json")))
                window->main->openFile (f);
        }
    }

    emx::EmxLookAndFeel lnf;
    std::unique_ptr<MainWindow> window;
};

START_JUCE_APPLICATION (EmulonXSDApplication)
