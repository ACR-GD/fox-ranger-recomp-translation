#!/bin/sh
# Rebuild the fully translated binary from the unpacked FOX.EXE.
#   prerequisite: work/FOX_UNP2.EXE  (tools/unpack_exe.py run twice: PKLITE, then EXEPACK)
set -e
cd "$(dirname "$0")/.."
python3 -I tools/patch_protection.py work/FOX_UNP2.EXE work/FOX_NOPROT.EXE
python3 -I tools/inject_translation.py work/FOX_NOPROT.EXE work/FOX_EN_FINAL.EXE \
    --intro translations/intro_en.json \
    --ending translations/ending_en.json \
    --credits translations/credits_en.json \
    --fox-dat fox-ranger/FOX.DAT --preview work/preview
