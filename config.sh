#!/bin/sh

rm -fr autom4te.cache/ configure config.h* config.status
autoreconf --install
autoconf
echo "run configure"
