#!/bin/bash

set -e

make clean
make parisc
mv out/hppa-firmware.img ../../pc-bios/hppa-firmware.img
make clean
