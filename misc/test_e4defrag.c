/* SPDX-License-Identifier: GPL-2.0 */
/* Exercise extent bookkeeping without issuing filesystem-changing ioctls. */
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include "config.h"
#include <ext2fs/ext2fs.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/ioctl.h>

static int test_ioctl(int fd, unsigned long request, ...);
static void *test_malloc(size_t size);

#define main e4defrag_main
#define ioctl test_ioctl
#define malloc test_malloc
#include "e4defrag.c"
#undef malloc
#undef ioctl
#undef main

static unsigned int allocations, fail_allocation;

static void *test_malloc(size_t size)
{
	if (++allocations == fail_allocation) {
		errno = ENOMEM;
		return NULL;
	}
	return malloc(size);
}

static int test_ioctl(int fd EXT2FS_ATTR((unused)), unsigned long request, ...)
{
	struct fiemap *fm;
	va_list ap;
	unsigned int i;

	assert(request == FS_IOC_FIEMAP);
	va_start(ap, request);
	fm = va_arg(ap, struct fiemap *);
	va_end(ap);
	fm->fm_mapped_extents = 3;
	for (i = 0; i < 3; i++) {
		fm->fm_extents[i].fe_logical = (__u64)i * 10 * block_size;
		fm->fm_extents[i].fe_physical = (__u64)(100 + i * 10) * block_size;
		fm->fm_extents[i].fe_length = 10 * block_size;
	}
	fm->fm_extents[2].fe_flags = FIEMAP_EXTENT_LAST;
	return 0;
}

static void check_extent_allocation_failure(void)
{
	unsigned int failure;
	struct fiemap_extent_list *head;

	/* FIEMAP buffer, three extent nodes, then the sorting sentinel. */
	for (failure = 1; failure <= 5; failure++) {
		head = NULL;
		allocations = 0;
		fail_allocation = failure;
		assert(get_file_extents(-1, &head) == -1);
		assert(errno == ENOMEM);
		free_ext(head);
	}
	head = NULL;
	fail_allocation = 0;
	assert(get_file_extents(-1, &head) == 0);
	assert(get_physical_count(head) == 1);
	free_ext(head);
}

int main(void)
{
	block_size = 4096;
	check_extent_allocation_failure();
	puts("e4defrag: extent bookkeeping OK");
	return 0;
}
