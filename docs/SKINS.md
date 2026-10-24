# Emulon X skin files (.emxd)

A skin file changes the whole look of Emulon X: colours, pictures, metal textures, corner shapes, knobs and faders.
You make them in **Emulon X SD** (the skin designer, a separate app you get from the Emulon X Hub), but the format is
open, so anyone can write a tool for it.

## Using a skin

- Drop the `.emxd` file onto the Emulon X window, or
- open **Settings > Customization**, click **ADD SKIN FILE...** and pick it.

Either way the file is copied into your skins folder (`Documents\Emulon X\Skins` unless you changed it under
**Settings > Folders > SKINS**) and used straight away. All skins in your skins folders appear in the **SKIN** list.
If a skin can't be read, Emulon X switches to Emulon Classic and shows the reason under the list.

## What's inside

An `.emxd` file is a zip file:

```
skin.json               the description (required)
images/...              pictures used by the skin (PNG or JPG)
textures/<name>.jpg     overlay textures named in skin.json (PNG or JPG)
fonts/...               an optional font file (TTF or OTF)
```

Emulon X also reads a plain `skin.json` (what the Skin Studio prototype exports). Pictures can then be inside it as
`data:` URLs, or files next to it.

## skin.json

Colours are written as `#rrggbb`. Any colour can instead link to the palette with `@accent`, `@text`, `@muted`,
`@surface`, `@background` or `@display`: change the palette colour and every linked colour follows.
Numbers are in pixels unless noted. Anything left out keeps the Emulon Classic value.

```jsonc
{
  "format": "emxd",              // required
  "version": 2,                  // required: 2 (the 1.1.2 format isn't supported any more)
  "name": "My Skin",
  "author": "Your name",
  "hueShift": true,              // may players turn the skin to another colour in Settings?
  "tokens": { "accent": "#ff2a2a", "text": "#e8e4e4", "muted": "#b8a6a6",
              "surface": "#1a181d", "background": "#120203", "display": "#070203" },
  "font": "Saira",               // Saira, Fredoka, Tahoma, or any font installed on the computer
  "fontFile": "fonts/my.ttf",    // optional: a font inside the skin (wins over "font")
  "upper": true,                 // panel titles in capitals

  "window":  { "fill": FILL, "texture": "none|grid|noise", "textureColor": COLOUR, "textureOpacity": 0-1, "overlay": OVERLAY },
  "frame":   { "width": 3, "color": COLOUR },
  "topbar":  { "fill": FILL, "line": 1, "lineColor": COLOUR, "logoColor": COLOUR, "logoGlow": 0-1,
               "logo": "images/logo.png", "overlay": OVERLAY },
  "panel":   BOX,
  "heading": { "color": COLOUR },                       // panel titles
  "inset":   BOX + { "innerShadow": 0-1 },              // the areas that hold knobs
  "display": BOX + { "textColor": COLOUR, "glow": 0-1, "scanlines": true },
  "list":    { "color": COLOUR },                       // names in lists
  "button":  BOX + { "textColor": COLOUR, "bevel": 0-1 },  // bevel = shine on the upper half
  "buttonOn": BOX + { "textColor": COLOUR, "glow": 0-1 },
  "tab":     { "same": true }                           // tabs look like buttons, or:
          // { "same": false, "fill": FILL, "onFill": FILL, "textColor": COLOUR, + the BOX shape fields }
  "knob":    { "style": "cap|ring|picture", "capA": COLOUR, "capB": COLOUR, "ring": 0-16,
               "ringTrack": COLOUR, "ringColor": COLOUR, "pointer": COLOUR, "pointerGlow": 0-1,
               "picture": "images/knob.png", "overlay": OVERLAY },
  "fader":   { "track": COLOUR, "fill": COLOUR, "thumbA": COLOUR, "thumbB": COLOUR,
               "thumbPic": "images/handle.png", "overlay": OVERLAY },
  "label":   { "color": COLOUR }                        // captions under knobs
}
```

**FILL**: `{ "type": "solid", "a": COLOUR }`, `{ "type": "gradient", "a": COLOUR, "b": COLOUR, "angle": 180 }`
(angle like CSS: 180 = top to bottom), or `{ "type": "image", "img": "images/x.png", "fit": "stretch|cover|tile", "a": COLOUR }`
(`a` shows behind see-through parts and is used to pick readable text colours).

**BOX**: `{ "fill": FILL, "radius": 6, "shape": 0.5, "outline": 1, "outlineColor": COLOUR,
"shadowY": 0, "shadowBlur": 0, "shadowOpacity": 0, "overlay": OVERLAY }`.
`shape` is the corner roundness: 0 cuts the corners straight, 0.5 is a normal round corner, 1 is an extra-smooth squircle.
A `shadowBlur` of 0 gives a hard, cartoon-style shadow.

**OVERLAY**: `{ "tex": "none" | "<name>" | "custom", "custom": "images/my-texture.png", "strength": 0-1, "blend": "overlay|soft-light" }`.
A named texture is read from `textures/<name>.jpg` (or `.png`) inside the skin, so a skin always carries its own textures.
Textures should be **grayscale**: mid-grey leaves the surface as it is, lighter areas add shine and darker areas add
scratches and shade. They're stretched over the whole part, so a square picture of about 512 to 1024 px works best.

## Limits

Pictures bigger than 2048 px are scaled down. A single file inside the skin can be up to 32 MB and the whole skin up to 128 MB.
Pictures must be PNG or JPG. A missing picture or texture doesn't stop the skin; it's reported under the skin list in Settings.
