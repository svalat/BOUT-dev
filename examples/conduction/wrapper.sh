#!/bin/bash

LD_PRELOAD=~/testinstall4/lib/libummap-io-rw-wrapper.so "$@"
exit $?

