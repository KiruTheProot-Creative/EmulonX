# Making Emulon X skins (`.emxd`)

A skin changes how Emulon X looks: background, window frame, knobs, faders, panels,
value displays, buttons, logo, font and colours. Skins are single **`.emxd`** files
("Emulon X Design") that anyone can import.

## The easy way: the skin designer

Open **Settings → Skin designer**. Everything you change shows up in the plugin right away.

| Control | What it does |
|---|---|
| **Name / By** | The skin's name and yours. |
| **Colour shift: allowed / off** | Whether the colour bar in *Settings → Customization* may turn your skin's colours. |
| **Pictures: fill / keep corners** | *Fill* (default): the whole picture covers each button, display, panel and knob area. *Keep corners*: 9-slice, for frame-style pictures whose corners must not stretch. |
| **Shadows: on / off** | A slight drop shadow under your pictures (on by default). |
| **Knob: pointer turns / picture turns** | *Pointer*: your knob picture stays still and a pointer turns on top of it. *Picture*: the picture itself turns. Its marker should point straight up. |
| **Choose font…** | A `.ttf` or `.otf` file, packed into the skin. It is sized automatically so all text still fits. Only use fonts whose licence lets you share them. |
| **Picture tiles** | **CHOOSE…** a PNG/JPG for that part, **X** to go back to the Classic look for it. |
| **Colours** | Click to pick a colour, right-click to set it back to *auto*. *Auto* colours follow the accent colour. |
| **Start over** | Clears everything (back to EMULON CLASSIC). |
| **Load folder…** | Reads a folder of skin files (see below) or a `.emxd`, to keep working on it. |
| **Save template…** | Writes every picture into a folder: yours, plus the Classic ones you haven't replaced. Includes a `HOW TO.txt`. Edit them in any image program, then use **Load folder…**. |
| **Export .emxd…** | Saves the skin as one file to share. It is also added to your skins and switched on. |

Your work in progress is kept, even if you close the plugin.

**Using skins:** go to *Settings → Customization → Custom skin*. Pick a skin from the list,
or use **Import .emxd…**, or drop a `.emxd` file onto the plugin window.

## The parts

Anything you leave out keeps the EMULON CLASSIC look, turned to your accent colour.

| File name | Part | How it's drawn | Suggested size |
|---|---|---|---|
| `background.png/.jpg` | Window background | Fills the window | 1920 × 1200 or larger |
| `frame.png` | Window frame | 9-slice over the window edges, middle transparent | 1920 × 1080, like the template |
| `knob.png` | Knob | Square; a pointer turns on it, or it turns itself | 256 × 256 |
| `fader.png` | Fader track | Stretched to the fader's height | 35 × 150 |
| `panel.png` | Panel background | Fills it (or 9-slice) | 96 × 96 |
| `inset.png` | Knob area inside panels | Fills it (or 9-slice) | 80 × 80 |
| `display.png` | Value displays / text fields | Fills them (or 9-slice) | 48 × 32 |
| `button.png` | Buttons | Fills them (or 9-slice) | 64 × 36 |
| `button-on.png` | Buttons that are on | Fills them (or 9-slice) | 64 × 36 |
| `logo.png` | Top-left logo | Fitted, keeps its shape | 871 × 200, transparent |
| `fader-knob.png` | The knob that slides on faders | About 32 px wide on screen, keeps its shape | 64 × 32 |
| any `.ttf` / `.otf` | Font | All text | — |

With **Pictures: fill**, each picture is stretched over the whole button, display or panel.
With **Pictures: keep corners** (9-slice),  the corners keep their shape while the edges and middle stretch. The corner
is a quarter of the picture's shorter side, and it shrinks by the same amount as the rest of
the picture. So a big, detailed picture (for example a 640 × 96 button) looks like a smaller
copy of itself, stretched only along its long side. Pictures are drawn at most at half their
pixel size, so they stay sharp at 200 % interface size.

## The file format (for the curious)

A `.emxd` file is a normal **zip** file containing the part files above plus `skin.json`:

```json
{
  "format": "emxd",
  "version": 1,
  "name": "Neon Night",
  "author": "Kiru",
  "hueShift": true,
  "knobRotates": false,
  "shadows": true,
  "keepCorners": false,
  "colours": {
    "accent": "#35d06b",
    "text": "#e8f0ea",
    "muted": "#9fb8aa",
    "display-text": "#35d06b",
    "display-bg": "#031008",
    "list": "#4ef0a0"
  },
  "images": { "panel": "panel.png", "knob": "knob.png" },
  "font": "font.ttf"
}
```

Every key is optional. An unpacked folder with the same files works too: that's what
*Save template* writes and *Load folder* reads. Installed skins live in the `Skins` folder
next to the Emulon X settings file:
- Windows: `%APPDATA%\Emulon X\Skins`
- macOS: `~/Library/Application Support/Emulon X/Skins`
