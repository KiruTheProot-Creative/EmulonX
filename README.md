# EMULON X

a Vibecoded project to make a Sampler, inspired by Emulator X (by E-MU) and with the ease to use like Vital.

# Current Features in V.1.1.1

- ### Sound engine
- **Sampler with zones:** each sample is a zone with its own key range, velocity range and root note.
- **Three interpolation modes:** *Sinc HQ* (cleanest), *Cubic* and *Linear* (lightest on the CPU).
- **Keep speed:** change the pitch without changing the length, like the Emulator X (turn it off for drums). Adjustable grain size.
- **Loops:** *Off*, *Loop* (keeps looping during the release) or *Until release* (plays on to the end after you let go), with a crossfade to smooth the loop seam.
- **Sample start / end** and **random start:** every note can start at a different spot inside an area you choose, always on a zero crossing so it doesn't click.
- **Per zone:** semitones, fine tune, gain, pan and an ADSR envelope.
- **Voice modes:** *Poly* for chords, or *Mono* with *Legato* and *Glide*.
- **Fits your setup:** MIDI channel, velocity curve (soft / linear / hard), pitch-bend range, master tuning (A4), up to 256 voices, a CPU limit and a choice of which notes get stolen.

### Main tab
- **Presets (`.emxp`):** the samples are packed inside the file, so one file is all you need to share a sound.
- **Banks (`.emxb`):** collect presets in the bank and export them as one file. The bank is a grid of presets (1–6 per row, set in Settings). Click to select, double-click or Enter to load, Delete to remove.
- **16 macros** at the bottom: automatable in your DAW and renamable (double-click the name). Right-click a macro to link it to any knob.
- **Explorer:** Base banks, Other banks (each bank shown as a folder of presets), your User presets and your sample folders. Click a sample to hear it, double-click to load it.
- **Drag and drop** WAV, AIFF, FLAC, OGG or MP3 files, presets or banks anywhere onto the window. Root notes are read from loop markers or file names like `Piano_C4.wav`.

### Editor tab
- **Waveform:** zoom with the mouse wheel, drag the markers for loop start / end, sample start / end and the random start area. Snap to zero crossings, or type exact positions.
- **Keymap:** drag zones to move them, drag their ends to change the key range, click keys to play them, shift-click a key to set the root note.
- **Envelope copy:** "Copy from…" takes the attack, decay, sustain and release of another zone, or gives every zone the current envelope.
- **Undo / Redo** (Ctrl+Z / Ctrl+Shift+Z) for every zone change.

### FX tab
- **FX chains** on the whole preset and on each single zone. Effects run top to bottom; reorder them with the arrows and bypass them with ON.
- **10 effects:**
  - Filter (low pass, high pass, band, notch)
  - Morph Filter (vowel, sweep)
  - EQ
  - Distortion (10 types, from soft clip to bitcrush)
  - Compressor / Limiter / OTT
  - Chorus / Flanger / Ensemble
  - Phaser
  - Delay (stereo, ping-pong, tape, diffuse; follows your DAW's tempo)
  - Reverb (Room, Hall, Plate, Ambience, Cathedral, Spring, Shimmer)
  - Utility
- **Modulation:** right-click any FX knob, the SEMI knob or the master volume to connect an envelope, a macro or the mod wheel. Rings around the knob show how far each source moves it.
- **Up to 16 envelopes:** draw your own shapes with curves or use AHDSR. They loop or play once, restart with each note or run freely, and follow your DAW's tempo or a rate in Hz.

### Arp tab
- **Advanced arpeggiator:** Up, Down, Up/Down, Down/Up, As played, Random and Chord, with rate (straight, dotted, triplet), octaves, gate, swing and latch.
- **8 patterns (A–H)** with up to 16 steps each: velocity per step, rests, and a semitone shift per step.
- **Arrangement bar:** chain patterns like A A B A. While your DAW plays, the steps follow its beat grid.

### Everywhere
- **Master volume** in the top bar, with the output meter built in.
- **Type values in:** double-click any value display and type, units included (`300ms`, `1.5 s`, `50%`, `-6 dB`, `C4`, `L20`, `2.5k`).
- **Skins:** *EMULON CLASSIC* (red metal, with a color shift and 8 quick picks) and *EMULON XP* in light and dark.
- **Interface size** from 75 % to 200 %, adjustable knob sensitivity, help tips.
- **Settings are shared** by every Emulon X in every project. Your DAW project can store the samples too, so it opens anywhere.

### Installer (Windows)
- You choose every folder: the plugin (VST3), base banks, your presets and the standalone app. The summary before installing lists every folder that will be used, and Emulon X finds the chosen folders by itself.
- Optional standalone app, to play Emulon X without a DAW.
