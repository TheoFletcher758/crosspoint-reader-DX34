#!/bin/bash
set -e
cd "$(dirname "$0")"
PYTHON_BIN="${PYTHON_BIN:-python3}"
READER_FONT_STYLES=("Regular" "Italic" "Bold")
READER_FONT_SIZES=(16 17 18 19)
for size in ${READER_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    lower_style=$(echo $style | tr '[:upper:]' '[:lower:]')
    "$PYTHON_BIN" fontconvert.py "bookerly_${size}_${lower_style}" $size "../builtinFonts/source/Bookerly/Bookerly-${style}.ttf" --2bit > "../builtinFonts/bookerly_${size}_${lower_style}.h"
    "$PYTHON_BIN" fontconvert.py "chareink_${size}_${lower_style}" $size "../builtinFonts/source/ChareInk/ChareInk7SP-${style}.ttf" --2bit > "../builtinFonts/chareink_${size}_${lower_style}.h"
  done
done
for size in 14 18; do
  "$PYTHON_BIN" fontconvert.py "unifont_${size}_regular" $size "../builtinFonts/source/UI/unifont-english.ttf" > "../builtinFonts/unifont_${size}_regular.h"
done
for size in 8 10 12; do
  "$PYTHON_BIN" fontconvert.py "ui_${size}_regular" $size "../builtinFonts/source/UI/CozetteVector.ttf" > "../builtinFonts/ui_${size}_regular.h"
done
