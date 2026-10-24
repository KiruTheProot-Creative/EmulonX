# Emulon X

A free, open-source sampler plugin inspired by the E-mu Emulator X.
Load whole sound banks, edit and loop samples, stack effects, and modulate
everything with drawable envelopes and macros.

> Status: **version 1.1** — advanced arpeggiator, sample start / end, seven reverb types.

## Using it

- **ARP tab:** switch **ARP ON** and hold a chord. Choose the order (Up, Down,
  Up/Down, Down/Up, As played, Random, Chord), the step length (**BEATS** /
  division, straight, dotted or triplet), **OCTAVES**, **GATE** and **SWING**.
  **LATCH** keeps it playing after you let go.
  - **Patterns A-H:** each has its own length and 16 steps. Drag the bars for
    velocity, click a step number to make a rest, and set the bottom row to
    transpose a step by semitones (drag, mouse wheel, click +1, right-click -1,
    double-click resets). **COPY** / **PASTE** copy a pattern.
  - **Arrange:** the bar above the pattern sets the order patterns play in
    (for example A A B A), in a loop. Click a slot to choose its pattern, **+** to
    add one, right-click to remove one.
  - While your DAW plays, the steps follow its beat grid.
- **Sample start / end (Editor tab):** drag the amber markers (flags at the
  bottom of the waveform) or use the **SMP START** / **SMP END** sliders. The
  sliders under the waveform also move the loop start / end and the crossfade;
  hold shift for fine moves.
- **Random start (Editor tab):** switch **RANDOM** on and every note starts at a
  different spot between the sample start and the dashed amber **RND TO**
  marker (drag it, or use the **RND TO** slider), so repeated notes sound less
  alike. Starts land on zero crossings, so they don't click.
- **Reverb types:** Room, Hall, Plate, Ambience, Cathedral, Spring and Shimmer,
  each built differently (early reflections, density, size, movement, tone).
- **Voice (Editor tab):** **POLY** plays chords; **MONO** plays one note at a
  time and returns to a key you still hold. In mono, **LEGATO** makes
  overlapping notes slide without restarting, and **GLIDE** sets how long the
  pitch takes to get to the new note.

### Milestone 5


- **FX tab:** pick a **CHANNEL**: *Whole preset* (runs on everything) or a single
  zone (runs only on that zone, before the whole-preset chain). **+ ADD EFFECT**
  adds Filter, Morph Filter, EQ, Distortion (10 types), Compressor (with
  Limiter and OTT), Chorus / Flanger / Ensemble, Phaser, Delay (Stereo,
  Ping-Pong, Tape, Diffuse; synced to your DAW's tempo) and Reverb (Room, Hall,
  Plate, Ambience), plus Utility. Effects run top to bottom; use the arrows to
  reorder and **ON** to bypass.
- **Modulation:** right-click any FX knob (or the master volume) and pick an
  envelope, a macro or the mod wheel. Rings around the knob show how far each
  source moves it; the list under the envelope editor sets the amounts
  (negative = upside down). Right-click a macro on the Main tab to see its
  connections, or choose **Link** and then click the knob it should move.
- **Envelopes:** draw your own shape (drag points, drag the small circles to
  bend curves, double-click to add or remove points) or switch to **AHDSR**.
  Shapes loop or play once, restart with each note or run freely, and follow the
  tempo (**SYNC**) or a rate in Hz. Up to 16 envelopes (**+**).
- Effects, envelopes and connections are saved in presets, banks and your DAW
  project, in the same format as the browser prototype.

### Earlier milestones


- **Editor tab:** double-click a zone on the Main tab (or use **<** / **>**) to edit it.
  - **Waveform:** mouse wheel zooms, dragging scrolls, double-click shows everything.
    Drag the white **S** / **E** flags to move the loop points, or type exact
    values (in sample frames). **ZERO-X** moves a point to the nearest zero
    crossing; **SNAP** does that automatically while dragging.
  - **Loop modes:** *Off*, *Loop* (keeps looping during the release) and
    *Until release* (plays on to the end after you let go). **X-FADE** blends
    the loop seam.
  - **Mapping, Pitch, Amp, Envelope:** root note, key and velocity ranges,
    **SEMI** (semitones, can be moved by envelopes / macros / mod wheel: right-click
    it), fine tune, keep speed and grain size, gain, pan, and an ADSR envelope per zone.
  - **Keymap:** each bar is a zone. Click it to edit, drag it to move, drag its
    ends to change the key range. Click keys to play (lower on the key = louder),
    shift-click a key to set the root note.
  - **UNDO / REDO** (Ctrl+Z / Ctrl+Shift+Z) for every zone change.


- **Add sounds:** drag WAV, AIFF, FLAC, OGG or MP3 files onto the window, press
  **LOAD...**, or double-click them in the Explorer's **SAMPLES** tab (a single
  click plays a preview). Root notes come from WAV loop markers or file names
  such as `Piano_C4.wav`; several files at once are spread across the keyboard
  (**AUTO MAP** does it again).
- **Presets (`.emxp`):** name the preset at the top of the Preset panel and press
  **SAVE**. The samples are packed inside the file, so one file is all you need
  to share. Opening a preset replaces the current one.
- **Banks (`.emxb`):** **ADD CURRENT** puts a copy of the current preset into the
  Bank list; **EXPORT...** saves the whole list as one `.emxb` file. **OPEN...**
  (or dropping a bank on the window) fills the list from a bank; double-click a
  preset to load it.
- **Explorer:** the **BANKS** tab shows *Base banks*, *Other banks* (each bank as
  a folder of presets) and *User* folders. Add folders with the folder **+** button
  or in the settings.
- **Settings** (gear button): folders, voices, CPU limit, voice stealing, default
  interpolation and keep speed, MIDI channel, velocity curve, pitch-bend range,
  tuning (A4), interface size, knob sensitivity and help tips. They are shared by
  every Emulon X in every project.
- **Your DAW project stores the samples too** (setting *Samples in DAW projects*),
  so a project still sounds right if the original files move or are deleted.
- **Interpolation:** *Sinc HQ* sounds cleanest; *Linear* uses the least CPU.
  **Keep speed** changes pitch without changing length (turn it off for drums).

## Install (Windows, e.g. for Ableton Live)

1. Download **EmulonX-Setup-x.y.z.exe** from the latest release (or the
   `EmulonX-Windows-Installer` artifact of the latest run on the **Actions** tab).
2. Run it. It puts the plugin into `C:\Program Files\Common Files\VST3` (you can
   pick another folder), creates `Documents\Emulon X\Base Banks` and
   `Documents\Emulon X\Presets`, installs the base banks and, if you like, the
   standalone app.
3. In Ableton: *Options → Preferences → Plug-Ins*, make sure
   **Use VST3 Plug-In System Folders** is on, then click **Rescan**. Emulon X is
   under *Plug-Ins → VST3*.

Windows may show a "Windows protected your PC" notice because the installer is
not code-signed: click **More info → Run anyway**. Uninstall it like any other
app (Settings → Apps); your presets and banks in Documents are kept.

Without the installer: copy the folder **`Emulon X.vst3`** from the
`EmulonX-Windows` artifact to `C:\Program Files\Common Files\VST3\`.
`Emulon X.exe` is a standalone version that runs without a DAW.

## Roadmap

| Milestone | What you get |
|---|---|
| 1 | Plugin window, your artwork, interface sizes 75–200 %, master volume, 16 macros |
| 2 | Sampler engine: zones, sinc interpolation, crossfaded loops, keep-speed pitching, ADSR |
| 3 | Explorer, `.emxp` / `.emxb` files, settings window with bank folders |
| 4 | Editor tab: waveform, loop points, keymap |
| 5 | FX chains (preset + per zone), envelopes, macro and mod-wheel modulation |
| 6 | Polish, factory banks, installers |

## Building it yourself

You don't have to — GitHub builds every change automatically (see
`.github/workflows/build.yml`). If you want to:

```
cmake -S . -B build
cmake --build build --config Release
```

Requires CMake 3.22+ and a C++20 compiler (Visual Studio 2022 on Windows,
Xcode on macOS). JUCE 9 is downloaded automatically.

## License

Emulon X is free software under the **GNU Affero General Public License v3**
(see `LICENSE`). It is built with [JUCE](https://juce.com), used under its
AGPLv3 option. The Orbitron font is under the SIL Open Font License
(`Assets/fonts/OFL-Orbitron.txt`).

"Emulator X" and "E-mu" are trademarks of their respective owners. Emulon X is
an independent project and is not affiliated with them.
