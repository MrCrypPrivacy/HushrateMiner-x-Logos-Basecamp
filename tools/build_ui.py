#!/usr/bin/env python3
"""Builds the final Main.qml from ui/Main.src.qml.

Usage: python3 -I tools/build_ui.py OUT.qml [LINKS.json]

LINKS.json is optional: {"urlGithub": "https://...", "urlX": "https://..."}.
A link left empty hides its button in the UI.
"""
import json, os, sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import i18n_data

root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
if len(sys.argv) < 2:
    sys.exit(__doc__)
out = sys.argv[1]
links = json.load(open(sys.argv[2])) if len(sys.argv) > 2 else {}
icons = json.load(open(f"{root}/assets/icons.json"))
version = open(f"{root}/VERSION").read().strip()
src = open(f"{root}/ui/Main.src.qml", encoding="utf-8").read()


def compact(v):
    return json.dumps(v, ensure_ascii=False, separators=(",", ":"))


def qml_string(v):
    v = str(v).strip()
    if v and not v.startswith("https://"):
        sys.exit("links must start with https://")
    if any(c in v for c in '"\\\n\r$`'):
        sys.exit("link contains forbidden characters")
    return v


rep = {
    "@@ICO_LOGO@@": compact(icons["logo"]),
    "@@ICO_GITHUB@@": compact(icons["github"]),
    "@@ICO_X@@": compact(icons["x"]),
    "@@I18N@@": compact(i18n_data.LANGS),
    "@@NATIVE@@": compact(i18n_data.NATIVE),
    "@@LANGNAMES@@": compact(i18n_data.LANGNAMES),
    "@@URL_GITHUB@@": qml_string(links.get("urlGithub", "")),
    "@@URL_X@@": qml_string(links.get("urlX", "")),
    "@@VERSION@@": version,
}
for k, v in rep.items():
    assert k in src, k
    src = src.replace(k, v)
assert "@@" not in src
open(out, "w", encoding="utf-8").write(src)
print(f"{out}: {len(src)} bytes (version {version})")
