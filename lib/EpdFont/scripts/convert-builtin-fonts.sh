#!/bin/bash
set -e
cd "$(dirname "$0")"
READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
BOOKERLY_FONT_SIZES=(18 20 22)
NOTOSANS_FONT_SIZES=(12 14 16 18)
BITTER_UI_FONT_SIZES=(8 9 10)
for size in ${BOOKERLY_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    python fontconvert.py "bookerly_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')" $size "../builtinFonts/source/Bookerly/Bookerly-${style}.ttf" --2bit > "../builtinFonts/bookerly_${size}_$(echo $style | tr '[:upper:]' '[:lower:]').h"
  done
done
for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    python fontconvert.py "notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')" $size "../builtinFonts/source/NotoSans/NotoSans-${style}.ttf" --2bit > "../builtinFonts/notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]').h"
  done
done
for size in ${BITTER_UI_FONT_SIZES[@]}; do
  python fontconvert.py "bitter_ui_${size}_regular" $size "../builtinFonts/source/Bitter/BitterPro-Regular.ttf" > "../builtinFonts/bitter_ui_${size}_regular.h"
done
