#!/bin/bash
set -e
cd "$(dirname "$0")"
READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
READER_FONT_SIZES=(12 14 17 19)
for size in ${READER_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    lower_style=$(echo $style | tr '[:upper:]' '[:lower:]')
    python fontconvert.py "bookerly_${size}_${lower_style}" $size "../builtinFonts/source/Bookerly/Bookerly-${style}.ttf" --2bit > "../builtinFonts/bookerly_${size}_${lower_style}.h"
    python fontconvert.py "chareink_${size}_${lower_style}" $size "../builtinFonts/source/ChareInk/ChareInk7SP-${style}.ttf" --2bit > "../builtinFonts/chareink_${size}_${lower_style}.h"
  done
done
VOLLKORN_FONT_STYLES=("Regular" "Italic" "Bold")
VOLLKORN_FONT_SIZES=(16 18 19)
for size in ${VOLLKORN_FONT_SIZES[@]}; do
  for style in ${VOLLKORN_FONT_STYLES[@]}; do
    lower_style=$(echo $style | tr '[:upper:]' '[:lower:]')
    python fontconvert.py "vollkorn_${size}_${lower_style}" $size "../builtinFonts/source/Vollkorn/Vollkorn-${style}.ttf" --2bit > "../builtinFonts/vollkorn_${size}_${lower_style}.h"
  done
done
for size in 14 18; do
  python fontconvert.py "unifont_${size}_regular" $size "../builtinFonts/source/UI/unifont-english.ttf" > "../builtinFonts/unifont_${size}_regular.h"
done
for size in 8 10 12; do
  python fontconvert.py "ui_${size}_regular" $size "../builtinFonts/source/UI/CozetteVector.ttf" > "../builtinFonts/ui_${size}_regular.h"
done
