#!/bin/sh

gettextize -f
autoreconf -f -i

if [ -z "$NOCONFIGURE" ]; then
    ./configure "$@"
fi
