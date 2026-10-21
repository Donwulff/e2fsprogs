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
static struct move_extent moves[1024];
static unsigned int move_count;
static __u64 skipped_blocks;

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

	va_start(ap, request);
	if (request == EXT4_IOC_MOVE_EXT) {
		struct move_extent *move = va_arg(ap, struct move_extent *);

		va_end(ap);
		assert(move_count < sizeof(moves) / sizeof(moves[0]));
		assert(move->orig_start == move->donor_start);
		moves[move_count++] = *move;
		move->moved_len = move->len - min(skipped_blocks, move->len);
		return 0;
	}
	assert(request == FS_IOC_FIEMAP);
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

static void check_sparse_groups(void)
{
	struct fiemap_extent_list nodes[3] = {0};
	struct fiemap_extent_group *groups = NULL;
	struct stat64 st = {0};
	char output[2048];
	FILE *file = tmpfile(), *progress = tmpfile();
	int saved_stdout, fd;
	unsigned int i;
	size_t len;

	assert(file && progress);
	fd = fileno(file);
	for (i = 0; i < 3; i++) {
		nodes[i].next = &nodes[(i + 1) % 3];
		nodes[i].prev = &nodes[(i + 2) % 3];
		nodes[i].data.len = 10;
	}
	/* First group bridges a 3-block hole, then a separate group at 100. */
	nodes[1].data.logical = 13;
	nodes[2].data.logical = 100;
	assert(join_extents(nodes, &groups) == 0);
	assert(groups->len == 23 && groups->next->len == 10);
	st.st_size = 120 * block_size; /* Includes a trailing hole. */
	st.st_blocks = 30 * block_size / 512;
	st.st_blksize = 65536; /* Preferred I/O size is not the FS block size. */
	assert(ftruncate64(fd, st.st_size) == 0);
	move_count = 0;
	skipped_blocks = 3;
	fflush(stdout);
	saved_stdout = dup(STDOUT_FILENO);
	assert(saved_stdout >= 0);
	assert(dup2(fileno(progress), STDOUT_FILENO) >= 0);
	assert(call_defrag(fd, fd, "sparse", &st, groups) == 0);
	fflush(stdout);
	assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
	close(saved_stdout);
	rewind(progress);
	len = fread(output, 1, sizeof(output) - 1, progress);
	output[len] = 0;
	assert(move_count == 2);
	assert(moves[0].orig_start == 0 && moves[0].len == 23);
	assert(moves[1].orig_start == 100 && moves[1].len == 10);
	/* 23 logical blocks / 120, then the last group, then trailing hole. */
	assert(strstr(output, " 19%") && strstr(output, " 91%"));
	assert(strstr(output, "100%"));
	fclose(progress);
	fclose(file);
	free_exts_group(groups);
	skipped_blocks = 0;
}

int main(void)
{
	block_size = 4096;
	check_extent_allocation_failure();
	check_sparse_groups();
	puts("e4defrag: extent bookkeeping OK");
	return 0;
}
