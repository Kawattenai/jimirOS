#!/bin/sh
set -e
. ./headers.sh

# 1. Run 'make install' to build the kernel
for PROJECT in $PROJECTS; do
  (cd $PROJECT && DESTDIR="$SYSROOT" $MAKE install)
done

# 2. Create the 'isodir/boot' directory
#    The '-p' flag ensures it doesn't fail if it already exists.
mkdir -p isodir/boot/

# 3. Copy the built kernel into the isodir
cp kernel/jimir.kernel isodir/boot/