#!/bin/bash
set -e

echo "Updating rootfs.ext2 with ELF files..."

# Build the user programs first with the correct compiler
cd user
make clean
CC=i686-elf-gcc make all
cd ..

# Mount the ext2 filesystem
sudo mkdir -p /mnt/jimirfs
sudo mount -o loop rootfs.ext2 /mnt/jimirfs

# Copy ELF files
echo "Copying user programs to rootfs..."
sudo cp user/userprog.elf /mnt/jimirfs/
sudo cp user/ush.elf /mnt/jimirfs/
sudo cp user/minitest.elf /mnt/jimirfs/ 2>/dev/null || echo "minitest.elf not found"
sudo cp user/forktest.elf /mnt/jimirfs/ 2>/dev/null || echo "forktest.elf not found"
sudo cp user/proctest.elf /mnt/jimirfs/ 2>/dev/null || echo "proctest.elf not found"
sudo cp user/simplefork.elf /mnt/jimirfs/ 2>/dev/null || echo "simplefork.elf not found"

# List contents
echo "Filesystem contents:"
ls -lh /mnt/jimirfs/

# Unmount
sudo umount /mnt/jimirfs
sudo rmdir /mnt/jimirfs

echo "Done! rootfs.ext2 now contains the ELF files."
echo "Run ./iso.sh to rebuild the ISO with the updated filesystem."
