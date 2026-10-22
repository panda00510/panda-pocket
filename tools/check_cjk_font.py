#!/usr/bin/env python3
"""Fails if a non-ASCII character used in the UI strings is missing from the generated CJK font subsets
(it would render as a box). Same sources as tools/gen_cjk_font.sh."""
import re, sys, pathlib

root = pathlib.Path(__file__).resolve().parent.parent
used = {c for f in ("components/pp_i18n/i18n.c", "components/pp_nav/nav_parse.c")
        for c in (root / f).read_text(encoding="utf-8") if ord(c) > 0x2E7F}
bad = 0
for size in (14, 16, 20):
    font = (root / f"components/pp_ui/fonts/pp_font_cjk_{size}.c").read_text(encoding="utf-8")
    have = {chr(int(m, 16)) for m in re.findall(r"/\* U\+([0-9A-Fa-f]{4,6}) ", font)}
    missing = sorted(used - have)
    if missing:
        bad = 1
        print(f"pp_font_cjk_{size}: missing {''.join(missing)}  (run tools/gen_cjk_font.sh <NotoSansCJKsc-Regular.otf>)")
print("font ok" if not bad else "font check FAILED")
sys.exit(bad)
