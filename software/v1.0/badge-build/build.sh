#!/bin/bash
#
# USAGE: $ ./build.sh 5.5.0

# particle usb dfu
mkdir -p my-$1/p2/release/
particle compile p2 ../badge --target $1 --saveTo my-$1/p2/release/p2-tinker@$1.bin
# particle flash --usb p2-badge@$1.bin
# device-os-flash --all-devices my-$1/ --user
device-os-flash --all-devices my-$1/

# All done, ding!
tput bel
