#!/bin/sh
set -e

# Ensure sysroot and headers are prepared and kernel is built/installed
. ./headers.sh
for PROJECT in $PROJECTS; do
  (cd $PROJECT && DESTDIR="$SYSROOT" $MAKE install)
done

# 1. Build user programs and prepare the grub directory structure and copy kernel
echo "Building user/ (userprog.elf, ush.elf)"
(cd user && ${MAKE:-make} clean && ${MAKE:-make} all)
rm -rf isodir
mkdir -p isodir/boot/grub
cp kernel/jimir.kernel isodir/boot/
cp user/userprog.elf isodir/boot/
cp user/ush.elf isodir/boot/
if [ -f rootfs.ext2 ]; then
	cp rootfs.ext2 isodir/boot/
	HAVE_ROOTFS=1
elif [ -f rootfs.img ]; then
	cp rootfs.img isodir/boot/
	HAVE_ROOTFS=1
else
	HAVE_ROOTFS=0
fi

# 2. Dynamically create the grub.cfg file in the correct location
cat > isodir/boot/grub/grub.cfg << EOF
set timeout=0
set default=0

# Mirror GRUB messages to serial and screen
serial --unit=0 --speed=115200 --word=8 --parity=no --stop=1
terminal_input console serial
terminal_output console serial
set pager=1

menuentry "jimirOS" {
	echo "Loading jimir.kernel via Multiboot..."
	multiboot /boot/jimir.kernel
	module /boot/userprog.elf userprog.elf
	module /boot/ush.elf ush.elf
EOF

# If we have a rootfs image, add the module line
if [ "$HAVE_ROOTFS" = "1" ]; then
	echo "	module /boot/rootfs.ext2 rootfs.ext2" >> isodir/boot/grub/grub.cfg
fi

# Close the menuentry block
cat >> isodir/boot/grub/grub.cfg << EOF
	boot
}
EOF

# 3. Create the final ISO
grub-mkrescue -o jimir.iso isodir/
if [ "$HAVE_ROOTFS" = "1" ]; then
	echo "ISO created: jimir.iso (modules: userprog.elf, ush.elf, rootfs.ext2)"
else
	echo "ISO created: jimir.iso (modules: userprog.elf, ush.elf)"
fi