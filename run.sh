#! /usr/bin/sh
make update && make  && DISPLAY=:0 ./haptic_rods "$@"
