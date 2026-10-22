#!/usr/bin/env bash
# Regenerates components/pp_ui/fonts/pp_font_cjk_{14,16,20}.c: a subset of Noto Sans CJK SC (SIL OFL 1.1)
# holding exactly the non-ASCII characters used in pp_i18n/i18n.c and pp_nav/nav_parse.c.
# Needs node (npx). Usage: tools/gen_cjk_font.sh /path/to/NotoSansCJKsc-Regular.otf
set -euo pipefail
FONT=${1:?path to NotoSansCJKsc-Regular.otf (github.com/notofonts/noto-cjk, Sans/OTF/SimplifiedChinese)}
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/components/pp_ui/fonts"
SYMBOLS=$(python3 -I - "$ROOT/components/pp_i18n/i18n.c" "$ROOT/components/pp_nav/nav_parse.c" <<'PY'
import sys
chars = sorted({c for f in sys.argv[1:] for c in open(f, encoding="utf-8").read() if ord(c) > 0x2E7F})
print("".join(chars), end="")
PY
)
echo "glyphs: ${#SYMBOLS}"
for SZ in 14 16 20; do
    npx -y lv_font_conv@1.5.3 --font "$FONT" --size $SZ --bpp 4 --format lvgl --no-compress \
        --lv-font-name pp_font_cjk_$SZ --symbols "$SYMBOLS" -o "$OUT/pp_font_cjk_$SZ.c"
    # The project includes LVGL as "lvgl.h"; no absolute paths in the header.
    sed -i.bak -e 's|#include "lvgl/lvgl.h"|#include "lvgl.h"|' -e "s|$FONT|NotoSansCJKsc-Regular.otf|g" "$OUT/pp_font_cjk_$SZ.c" && rm "$OUT/pp_font_cjk_$SZ.c.bak"
done
