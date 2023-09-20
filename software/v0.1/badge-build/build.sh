#!/bin/bash
#
# USAGE: $ ./build.sh 5.5.0

particle usb dfu
particle compile p2 ../badge --target $1 --saveTo p2-badge@$1.bin
particle flash --usb p2-badge@$1.bin

# All done, ding!
tput bel
