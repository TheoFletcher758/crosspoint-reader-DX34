#!/bin/bash
set -e
cd "$(dirname "$0")"
PYTHON_BIN="${PYTHON_BIN:-python3}"
READER_FONT_STYLES=("Regular" "Italic" "Bold")
CHAREINK_FONT_SIZES=(13 14 15 16 17 18 19)
for size in ${CHAREINK_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    lower_style=$(echo $style | tr '[:upper:]' '[:lower:]')
    font_name="chareink_${size}_${lower_style}"
    echo "Generating ${font_name}..."
    "$PYTHON_BIN" fontconvert.py "${font_name}" $size "../builtinFonts/source/ChareInk/ChareInk7SP-${style}.ttf" --2bit > "../builtinFonts/${font_name}.h"
  done
done

for size in 8 10 12; do
  echo "Generating ui_${size}_regular..."
  "$PYTHON_BIN" fontconvert.py "ui_${size}_regular" $size "../builtinFonts/source/UI/CozetteVector.ttf" > "../builtinFonts/ui_${size}_regular.h"
done

echo "All fonts generated."
