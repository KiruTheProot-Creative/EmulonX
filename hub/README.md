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
| `hub.json` | Settings for the Hub itself: `githubClientId` is the Hub's GitHub sign-in app (see below) |
| `images/` | Screenshots, skin previews and bank covers |

## The rules (that's all there is)
- **Settings at the top**, between the two `---` lines, written as `name: value`.
- **Plain text** below that is the description. Leave an empty line between paragraphs.
- A line starting with `## ` begins a section (like `## Features`) or, in `skins.md`, `banks.md` and `news.md`, a new item.
- A line starting with `- ` is a list item.
- In a `## Screenshots` list, write `file name | caption`.
- `**bold**` makes text bold.

## What comes from somewhere else (nothing to edit here)
- **Version numbers, downloads and update notifications** come from your GitHub Releases, by the start of the tag:
  `v1.2.0ea` is Emulon X, `sd-v0.1af` is Emulon X SD, `hub-v0.1af` is the Hub. Pushing such a tag builds the
  installer and publishes the release; every Hub sees it on its next start (or within 30 minutes).
- **Community skins** come from the [EmulonX-Skins](https://github.com/KiruTheProot-Creative/EmulonX-Skins) repository
  (people send them with SUBMIT A SKIN in the Hub; you accept the pull request and the skin shows up).
- **Banks** listed in `banks.md` can also be released in the [EmulonX-Banks](https://github.com/KiruTheProot-Creative/EmulonX-Banks)
  repository. Once a bank with the same name has a download there, the Hub shows it as out (and tells everyone who
  asked to be notified). Banks that are only in EmulonX-Banks appear under MORE BANKS.

## Dev mode and SUBMIT A SKIN: the GitHub sign-in app
Signing in uses GitHub's own sign-in page, so no password or key is ever stored in the Hub. It needs a (free)
GitHub "OAuth App" once:
1. GitHub > Settings > Developer settings > OAuth Apps > **New OAuth App**.
2. Name: `Emulon X Hub`, Homepage URL: this repository, Authorization callback URL: this repository too (it isn't used).
3. Click **Register application**, then tick **Enable Device Flow** and save.
4. Copy the **Client ID** (not a secret) into `hub.json` as `"githubClientId": "..."`.

In the Hub, **DEV LOGIN** (bottom left) signs you in; accounts that can change this repository get dev mode.
