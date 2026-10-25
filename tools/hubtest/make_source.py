#!/usr/bin/env python3
"""Makes a pretend GitHub for the Emulon X Hub self-test (no internet needed):
    python3 tools/hubtest/make_source.py <out folder> <repository folder> <a .emxd skin>
Then run:  EMX_HUB_SOURCE=<out folder> "Emulon X Hub" --self-test <screenshots folder>"""
import hashlib, json, os, shutil, sys
from pathlib import Path

out, repo, skin = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(), Path(sys.argv[3]).resolve()
shutil.rmtree(out, ignore_errors=True)
shutil.copytree(repo / "hub", out / "EmulonX" / "hub")

s = out / "EmulonX-Skins" / "skins" / "test-skin"
s.mkdir(parents=True)
shutil.copy(skin, s / "skin.emxd")
shutil.copy(repo / "hub" / "images" / "skin-classic.jpg", s / "preview.png")
data = (s / "skin.emxd").read_bytes()
json.dump({"format": "emulonx-skins", "version": 1, "skins": [{
    "id": "test-skin", "name": "BRUSHED RED", "author": "Kiru", "description": "A community skin for the self-test.",
    "version": "1.0", "skin": (s / "skin.emxd").as_uri(), "preview": (s / "preview.png").as_uri(),
    "size": len(data), "sha256": hashlib.sha256(data).hexdigest()}]},
    open(out / "EmulonX-Skins" / "index.json", "w"), indent=1)

b = out / "EmulonX-Banks" / "dl"
b.mkdir(parents=True)
(b / "Tape Choirs.emxb").write_bytes(b"x" * 5000)
json.dump({"format": "emulonx-banks", "version": 1, "banks": [
    {"id": "emulon-x-base-bank", "name": "EMULON X BASE BANK", "author": "Kiru", "official": True, "soon": True,
     "description": "", "presets": 0, "version": "", "download": "", "size": 0, "colour": 0},
    {"id": "tape-choirs", "name": "TAPE CHOIRS", "author": "Someone", "official": False, "soon": False,
     "description": "Choirs and vocal pads with a warm, wobbly character.", "presets": 16, "version": "1.0",
     "download": (b / "Tape Choirs.emxb").as_uri(), "size": 5000, "colour": 285}]},
    open(out / "EmulonX-Banks" / "index.json", "w"), indent=1)

asset = lambda n, size: [{"name": n, "browser_download_url": "https://example.invalid/" + n, "size": size}]
json.dump([
    {"tag_name": "v1.2.0ea", "name": "Emulon X 1.2.0ea", "body": "", "html_url": "", "draft": False, "assets": asset("EmulonX-Setup-1.2.0ea.exe", 14000000)},
    {"tag_name": "v1.1.1ea", "draft": False, "assets": []},
    {"tag_name": "sd-v0.1af", "draft": False, "assets": asset("EmulonX-SD-Setup-0.1af.exe", 9000000)},
    {"tag_name": "hub-v0.2af", "draft": False, "assets": asset("EmulonX-Hub-Setup-0.2af.exe", 8000000)},
    {"tag_name": "v9.0.0", "draft": True, "assets": []}], open(out / "releases.json", "w"))
print("made", out)
