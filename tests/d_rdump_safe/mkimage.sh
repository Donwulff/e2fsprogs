#!/bin/sh
#
# This is the script that was used to create the image.gz in this directory.
#
# The image is a tiny ext2 filesystem whose root directory holds a normal file
# "loot" and a second, hostile directory entry whose name is the literal string
# "../escaped" (a hard link to loot's inode).  A valid filesystem never contains
# a name with a '/' in it, and neither mke2fs nor debugfs will create one, so the
# hostile entry is planted with a small libext2fs helper: ext2fs_link() writes
# the raw name without validating it.
#
# "debugfs -R 'rdump / <dest>'" used to copy that entry to <dest>/../escaped,
# i.e. outside <dest>.  See the d_rdump_safe test.
#
# Requires mke2fs, debugfs and the libext2fs headers (the e2fsprogs build tree,
# or the e2fslibs-dev / libext2fs-dev package).  The UUID and timestamps are not
# pinned, so a re-run yields a functionally identical but not byte-identical
# image, the same way the f_* fsck test images do.

set -e -u

IMG=image

# 1. A tiny ext2 filesystem: 1024-byte blocks, 512 blocks, 64 inodes.
mke2fs -F -q -b 1024 -N 64 "$IMG" 512

# 2. A normal file "loot" in the root directory.
printf 'loot-content\n' > loot-content
debugfs -w -R "write loot-content loot" "$IMG"
rm -f loot-content

# 3. Plant a hostile hard link named "../escaped" onto loot's inode.
cat > plant.c <<'PLANT'
#include <ext2fs/ext2fs.h>

int main(int argc, char **argv)
{
	ext2_filsys fs;
	ext2_ino_t ino;

	if (ext2fs_open(argv[1], EXT2_FLAG_RW, 0, 0, unix_io_manager, &fs))
		return 1;
	if (ext2fs_namei(fs, EXT2_ROOT_INO, EXT2_ROOT_INO, "loot", &ino))
		return 1;
	if (ext2fs_link(fs, EXT2_ROOT_INO, "../escaped", ino, EXT2_FT_REG_FILE))
		return 1;
	return ext2fs_close(fs) ? 1 : 0;
}
PLANT
cc -o plant plant.c -lext2fs -lcom_err
./plant "$IMG"
rm -f plant plant.c

# 4. Compress; the result is committed as image.gz.
gzip -9 -f "$IMG"
