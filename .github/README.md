# e2fsprogs: e4defrag mergesort fork

This is [Donwulff's fork](https://github.com/Donwulff/e2fsprogs) of
[Theodore Ts'o's e2fsprogs](https://github.com/tytso/e2fsprogs). The default
branch is `defrag-mergesort`; it carries changes to `e4defrag` alongside
updates from upstream `master`.

The fork replaces e4defrag's insertion sort with mergesort adapted from the
Linux kernel. Large, sparse database files and backups synchronized with
rsync can have tens of thousands of extents, making extent sorting costly.

It also defragments along contiguous physical segments of the donor file,
rather than moving each original extent separately, to help reduce the
extent tree. Allocation groups incorporate small holes of fewer than eight
blocks to improve contiguity for read-ahead, backup scans, and database range
scans. The kernel defragmentation ioctl still preserves sparse-file holes.

These e4defrag changes are experimental and should be tested before use.
Fork-specific e4defrag bugs are the fork author's responsibility, not
Ted Ts'o's. Use at your own risk and keep backups of your data.

See the upstream [README](../README) and [INSTALL](../INSTALL) for project
information and build instructions. The upstream README stays unchanged in
this fork so upstream release updates can merge without conflicting with
the fork description here.

For a step-by-step explanation of fetching, merging, testing, and pushing
upstream updates, see [Keeping this fork in sync](../doc/FORK-SYNC.md).
