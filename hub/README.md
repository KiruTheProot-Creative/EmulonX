# Hub texts and pictures

Everything the Emulon X Hub (the launcher) shows comes from the files in this folder.
The Hub downloads them from GitHub each time it starts, so **you only edit a file here and every Hub shows the new text the next time it's opened**. No new build is needed, and editing this folder doesn't start one.

## How to edit
1. Open a file on GitHub, for example `hub/emulon-x.md`.
2. Click the pencil (Edit) button.
3. Change the text and click **Commit changes**.

To add a picture, open `hub/images`, click **Add file → Upload files**, then write its file name where you want it to appear.

## The files
| File | What it is |
|---|---|
| `emulon-x.md` | The EMULON X tab |
| `emulon-x-sd.md` | The EMULON X SD (Skin Designer) tab |
| `skins.md` | Every skin on the EMULON SKINS tab |
| `banks.md` | Every bank on the EMULON BANKS tab |
| `news.md` | Notifications (the bell, plus the pop-up when the Hub starts) |
| `images/` | Screenshots, skin previews and bank covers |

## The rules (that's all there is)
- **Settings at the top**, between the two `---` lines, written as `name: value`.
- **Plain text** below that is the description. Leave an empty line between paragraphs.
- A line starting with `## ` begins a section (like `## Features`) or, in `skins.md`, `banks.md` and `news.md`, a new item.
- A line starting with `- ` is a list item.
- In a `## Screenshots` list, write `file name | caption`.
- `**bold**` makes text bold.

The Emulon X version number and its update notification come from your GitHub Releases automatically, so there's nothing to edit for that.
