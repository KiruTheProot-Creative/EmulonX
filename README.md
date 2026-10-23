# Emulon X

A free, open-source sampler plugin inspired by the E-mu Emulator X.
Load whole sound banks, edit and loop samples, stack effects, and modulate
everything with drawable envelopes and macros.

> Status: **milestone 2** — the sampler engine. Drop audio files or a preset
> made in the browser prototype onto the window and play.

## Using it (milestone 2)

- **Add sounds:** drag WAV, AIFF, FLAC, OGG or MP3 files onto the plugin window,
  or press **LOAD...**. A `.emxp` preset from the browser prototype loads with all
  its zones, loops and envelopes.
- **Root notes** are read from WAV loop markers or from file names such as
  `Piano_C4.wav` or `Pad A#3.wav`. Dropping several files at once spreads them
  across the keyboard automatically (**AUTO MAP** does it again any time).
- **Interpolation:** *Sinc HQ* sounds cleanest; *Linear* uses the least CPU.
- **Keep speed** changes pitch without changing length (turn it off for drums).
- Sustain pedal, pitch bend (±2 semitones) and up to 64 voices work.
- Everything is saved with your DAW project. Samples are referenced from where
  they are on disk, so don't move or delete them.

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
