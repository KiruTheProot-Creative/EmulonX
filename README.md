# Emulon X

A free, open-source sampler plugin inspired by the E-mu Emulator X.
Load whole sound banks, edit and loop samples, stack effects, and modulate
everything with drawable envelopes and macros.

> Status: **milestone 4** — the Editor tab: waveform, loop points, zone settings and keymap.

## Using it (milestone 4)

- **Editor tab:** double-click a zone on the Main tab (or use **<** / **>**) to edit it.
  - **Waveform:** mouse wheel zooms, dragging scrolls, double-click shows everything.
    Drag the white **S** / **E** flags to move the loop points, or type exact
    values (in sample frames). **ZERO-X** moves a point to the nearest zero
    crossing; **SNAP** does that automatically while dragging.
  - **Loop modes:** *Off*, *Loop* (keeps looping during the release) and
    *Until release* (plays on to the end after you let go). **X-FADE** blends
    the loop seam.
  - **Mapping, Pitch, Amp, Envelope:** root note, key and velocity ranges, fine
    tune, keep speed and grain size, gain, pan, and an ADSR envelope per zone.
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

1. Download `EmulonX-Windows` from the latest run on the **Actions** tab
   (or from **Releases** once there is one) and unzip it.
2. Copy the folder **`Emulon X.vst3`** to
   `C:\Program Files\Common Files\VST3\`
3. In Ableton: *Options → Preferences → Plug-Ins*, make sure
   **Use VST3 Plug-In System Folders** is on, then click **Rescan**.
4. Find **Emulon X** under *Plug-Ins → VST3 → Emulon*, drag it onto a MIDI
   track and play.

`Emulon X.exe` is a standalone version that runs without a DAW.

## Roadmap

| Milestone | What you get |
|---|---|
| 1 | Plugin window, your artwork, interface sizes 75–200 %, master volume, 20 macros |
| 2 | Sampler engine: zones, sinc interpolation, crossfaded loops, keep-speed pitching, ADSR |
| 3 | Explorer, `.emxp` / `.emxb` files, settings window with bank folders |
| 4 | Editor tab: waveform, loop points, keymap |
| 5 | FX chains (preset + per zone), envelopes, macro and mod-wheel routing |
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
