#!/bin/bash
set -e
cd "$(dirname "$0")"
PYTHON_BIN="${PYTHON_BIN:-python3}"
READER_FONT_STYLES=("Regular" "Italic" "Bold")
FREESERIF_FONT_SIZES=(19 21 23)
CHAREINK_FONT_SIZES=(15 16 17 18 19)
for size in ${FREESERIF_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    lower_style=$(echo $style | tr '[:upper:]' '[:lower:]')
    "$PYTHON_BIN" fontconvert.py "freeserif_${size}_${lower_style}" $size "../builtinFonts/source/FreeSerif/FreeSerif-${style}.ttf" --2bit > "../builtinFonts/freeserif_${size}_${lower_style}.h"
  done
done
for size in ${CHAREINK_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    lower_style=$(echo $style | tr '[:upper:]' '[:lower:]')
    "$PYTHON_BIN" fontconvert.py "chareink_${size}_${lower_style}" $size "../builtinFonts/source/ChareInk/ChareInk7SP-${style}.ttf" --2bit > "../builtinFonts/chareink_${size}_${lower_style}.h"
  done
done
for size in 13 14; do
  for style in ${READER_FONT_STYLES[@]}; do
    lower_style=$(echo $style | tr '[:upper:]' '[:lower:]')
    "$PYTHON_BIN" fontconvert.py "chareink_${size}_${lower_style}" $size "../builtinFonts/source/ChareInk/ChareInk7SP-${style}.ttf" --2bit > "../builtinFonts/chareink_${size}_${lower_style}.h"
  done
done
for size in 14 18; do
  "$PYTHON_BIN" fontconvert.py "unifont_${size}_regular" $size "../builtinFonts/source/UI/unifont-english.ttf" > "../builtinFonts/unifont_${size}_regular.h"
done
for size in 8 10 12; do
  "$PYTHON_BIN" fontconvert.py "ui_${size}_regular" $size "../builtinFonts/source/UI/CozetteVector.ttf" > "../builtinFonts/ui_${size}_regular.h"
done
