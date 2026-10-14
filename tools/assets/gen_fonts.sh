#!/bin/sh
# Regenerate the Fredoka fonts in components/ui/assets/. The output is
# committed, so a normal build does not need this. Needs node (npx), curl
# and python3, plus managed_components/ for LVGL's FontAwesome symbols.
set -eu

root=$(cd "$(dirname "$0")/../.." && pwd)
out="$root/components/ui/assets"
work="$root/build-fonts"

FREDOKA_COMMIT=a60a77e14f28abd4ef243a1b5dfc48df0cec5205
FREDOKA_URL="https://raw.githubusercontent.com/google/fonts/$FREDOKA_COMMIT/ofl/fredoka"
LV_FONT_CONV="npx --yes lv_font_conv@1.5.3"
FONTTOOLS=fonttools==4.60.1
SYMBOL_FONT="$root/managed_components/lvgl__lvgl/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff"

# Latin-1 plus typographic punctuation. Keep in sync with SUPPORTED_EXTRA in
# tools/pukk.py and docs/PACKS.md.
TEXT=0x20-0x7E,0xA0-0xFF,0x2013,0x2014,0x2018,0x2019,0x201C,0x201D,0x2022,0x2026,0x20AC
# LVGL symbols we use: ok, close, right, charge, bluetooth, battery full..empty,
# plus a star for the score.
SYMBOLS=0xF00C,0xF00D,0xF054,0xF0E7,0xF293,0xF240-0xF244,0xF005

mkdir -p "$work" "$out"
curl -sSfL "$FREDOKA_URL/Fredoka%5Bwdth,wght%5D.ttf" -o "$work/Fredoka-VF.ttf"
curl -sSfL "$FREDOKA_URL/OFL.txt" -o "$out/Fredoka-OFL.txt"

if [ ! -x "$work/venv/bin/fonttools" ]; then
    python3 -m venv "$work/venv"
    "$work/venv/bin/pip" install -q "$FONTTOOLS"
fi
"$work/venv/bin/fonttools" varLib.instancer -q "$work/Fredoka-VF.ttf" wght=500 wdth=100 -o "$work/Fredoka-Medium.ttf"
"$work/venv/bin/fonttools" varLib.instancer -q "$work/Fredoka-VF.ttf" wght=600 wdth=100 -o "$work/Fredoka-SemiBold.ttf"

# Run lv_font_conv inside $work with relative paths: it records its command
# line in the generated file.
cp "$SYMBOL_FONT" "$work/FontAwesome5.woff"
cd "$work"

# gen <name> <size> <weight> <range> [symbols]
gen() {
    if [ -n "${5:-}" ]; then
        $LV_FONT_CONV --no-compress --no-prefilter --bpp 4 --size "$2" \
            --font "Fredoka-$3.ttf" -r "$4" --font FontAwesome5.woff -r "$5" \
            --format lvgl --lv-include lvgl.h --lv-font-name "$1" --force-fast-kern-format \
            -o "$1.c"
    else
        $LV_FONT_CONV --no-compress --no-prefilter --bpp 4 --size "$2" \
            --font "Fredoka-$3.ttf" -r "$4" \
            --format lvgl --lv-include lvgl.h --lv-font-name "$1" --force-fast-kern-format \
            -o "$1.c"
    fi
    mv "$1.c" "$out/$1.c"
    echo "$1: $(wc -c < "$out/$1.c") bytes of C"
}

gen pukk_font_20 20 Medium "$TEXT" "$SYMBOLS"
gen pukk_font_24 24 Medium "$TEXT"
gen pukk_font_28 28 SemiBold "$TEXT"
gen pukk_font_32 32 SemiBold "$TEXT" "$SYMBOLS"
gen pukk_font_48 48 SemiBold "$TEXT"
# Wordmark for the splash screen: only the letters of "pukk".
gen pukk_font_logo 120 SemiBold 0x6B,0x70,0x75
