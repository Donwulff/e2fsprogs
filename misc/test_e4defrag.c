/* SPDX-License-Identifier: GPL-2.0 */
/* Exercise extent bookkeeping without issuing filesystem-changing ioctls. */
#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include "config.h"
#include <ext2fs/ext2fs.h>
#include <stdlib.h>
#include <stdarg.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <sys/statfs.h>
#include <sys/wait.h>
#include <mntent.h>

static int test_ioctl(int fd, unsigned long request, ...);
static void *test_malloc(size_t size);
static void *test_mmap64(void *addr, size_t length, int prot, int flags,
			 int fd, off64_t offset);
static int test_mincore(void *addr, size_t length, unsigned char *vec);
static int test_munmap(void *addr, size_t length);
static int test_fallocate(int fd, int mode, off_t offset, off_t len);
static int test_fcntl(int fd, int cmd, ...);
static int test_fstatfs64(int fd, struct statfs64 *buf);
static int test_statfs64(const char *path, struct statfs64 *buf);
static struct mntent *test_getmntent(FILE *stream);

#define main e4defrag_main
#define ioctl test_ioctl
#define malloc test_malloc
#define mmap64 test_mmap64
#define mincore test_mincore
#define munmap test_munmap
#define fallocate test_fallocate
#define fcntl test_fcntl
#define fstatfs64 test_fstatfs64
#define statfs64(path, buf) test_statfs64(path, buf)
#define getmntent test_getmntent
#include "e4defrag.c"
#undef getmntent
#undef statfs64
#undef fstatfs64
#undef fcntl
#undef fallocate
#undef munmap
#undef mincore
#undef mmap64
#undef malloc
#undef ioctl
#undef main

static unsigned int allocations, fail_allocation;
static struct move_extent moves[1024];
static unsigned int move_count;
static __u64 skipped_blocks;
static size_t mapping_limit = DEFRAG_MAX_BYTES;
static unsigned int mappings, mapping_failures;
static int mapping_error, mincore_error, move_error;
static struct defrag_result move_result;
static int fixture, fixture_maps, donor_runs = 1, source_runs = 3;
static int allocation_error, lock_file, no_space, after_error;
static __u64 partial_blocks;
static int fixture_mount;

static int test_statfs64(const char *path, struct statfs64 *buf)
{
	int ret = statfs64(path, buf);

	if (!ret && fixture_mount)
		buf->f_type = EXT4_SUPER_MAGIC;
	return ret;
}

static struct mntent *test_getmntent(FILE *stream)
{
	static int next;
	static struct mntent entry = {
		.mnt_fsname = "/dev/e4defrag-test-no-device",
		.mnt_dir = "/", .mnt_type = "ext4", .mnt_opts = "rw"
	};

	if (!fixture_mount)
		return getmntent(stream);
	next = !next;
	return next ? &entry : NULL;
}

static int test_fallocate(int fd, int mode, off_t offset, off_t len)
{
	if (allocation_error) {
		errno = allocation_error;
		return -1;
	}
	return fallocate(fd, mode, offset, len);
}

static int test_fcntl(int fd, int cmd, ...)
{
	va_list ap;
	struct flock *lock;

	assert(cmd == F_GETLK);
	va_start(ap, cmd);
	lock = va_arg(ap, struct flock *);
	va_end(ap);
	if (lock_file) {
		lock->l_type = F_WRLCK;
		return 0;
	}
	return fcntl(fd, cmd, lock);
}

static int test_fstatfs64(int fd, struct statfs64 *buf)
{
	int ret = fstatfs64(fd, buf);

	if (ret == 0 && no_space)
		buf->f_bfree = buf->f_bavail = 0;
	return ret;
}

static void *test_mmap64(void *addr, size_t length, int prot, int flags,
			 int fd, off64_t offset)
{
	void *result;

	/* A page-alignment adjustment may add up to one page. */
	assert(length < DEFRAG_MAX_BYTES + (size_t)sysconf(_SC_PAGESIZE));
	if (mapping_error || length > mapping_limit) {
		mapping_failures++;
		errno = mapping_error ? mapping_error : ENOMEM;
		return MAP_FAILED;
	}
	result = mmap64(addr, length, prot, flags, fd, offset);
	if (result != MAP_FAILED)
		mappings++;
	return result;
}

static int test_mincore(void *addr, size_t length, unsigned char *vec)
{
	if (mincore_error) {
		errno = mincore_error;
		return -1;
	}
	return mincore(addr, length, vec);
}

static int test_munmap(void *addr, size_t length)
{
	int ret = munmap(addr, length);

	if (ret == 0) {
		assert(mappings > 0);
		mappings--;
	}
	return ret;
}

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
		if (move_error) {
			move->moved_len = partial_blocks;
			errno = move_error;
			return -1;
		}
		move->moved_len = move->len - min(skipped_blocks, move->len);
		return 0;
	}
	assert(request == FS_IOC_FIEMAP);
	fm = va_arg(ap, struct fiemap *);
	va_end(ap);
	if (!fm->fm_extent_count) {
		if (after_error) {
			errno = after_error;
			return -1;
		}
		fm->fm_mapped_extents = donor_runs;
		return 0;
	}
	if (fixture) {
		unsigned int count = fixture_maps++ ? donor_runs : source_runs;

		fm->fm_mapped_extents = count;
		for (i = 0; i < count; i++) {
			fm->fm_extents[i].fe_logical = (__u64)i * 30 / count * block_size;
			fm->fm_extents[i].fe_physical = (__u64)(100 + i * 40) * block_size;
			fm->fm_extents[i].fe_length = (__u64)30 / count * block_size;
		}
		fm->fm_extents[count - 1].fe_flags = FIEMAP_EXTENT_LAST;
		return 0;
	}
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
	/* Capture interactive updates to check logical progress across holes. */
	interactive_output = 1;
	assert(call_defrag(fd, fd, "sparse", &st, groups, &move_result) == 0);
	interactive_output = 0;
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

static void check_large_group(int retry)
{
	struct fiemap_extent_list nodes[2] = {0};
	struct fiemap_extent_group *groups = NULL;
	struct stat64 st = {0};
	FILE *file = tmpfile();
	__u64 next;
	unsigned int i;

	assert(file);
	nodes[0].next = nodes[0].prev = &nodes[1];
	nodes[1].next = nodes[1].prev = &nodes[0];
	/* Two 128 MiB extents above 2 GiB, joined into one donor allocation. */
	nodes[0].data.logical = (3ULL << 30) / block_size;
	nodes[0].data.len = (128U << 20) / block_size;
	nodes[1].data.logical = nodes[0].data.logical + nodes[0].data.len;
	nodes[1].data.len = nodes[0].data.len;
	assert(join_extents(nodes, &groups) == 0);
	assert(groups->next == groups && groups->len == 2 * nodes[0].data.len);
	st.st_size = (nodes[1].data.logical + nodes[1].data.len) * block_size;
	st.st_blocks = groups->len * block_size / 512;
	assert(ftruncate64(fileno(file), st.st_size) == 0);
	move_count = mapping_failures = 0;
	mapping_limit = retry ? (8U << 20) : DEFRAG_MAX_BYTES;
	skipped_blocks = 3; /* Success can move fewer blocks than requested. */
	assert(call_defrag(fileno(file), fileno(file), "large", &st, groups, &move_result) == 0);
	assert(move_count == (retry ? 32 : 4));
	assert(mapping_failures == (retry ? 3 : 0));
	next = nodes[0].data.logical;
	for (i = 0; i < move_count; i++) {
		assert(moves[i].orig_start == next);
		assert(moves[i].len * block_size <= mapping_limit);
		next += moves[i].len;
	}
	assert(next * block_size == (__u64)st.st_size);
	assert(mappings == 0);
	skipped_blocks = 0;
	mapping_limit = DEFRAG_MAX_BYTES;
	free_exts_group(groups);
	fclose(file);
}

static void check_mapping_errors(void)
{
	FILE *file = tmpfile();
	struct move_extent move = {0};
	struct fiemap_extent_list node = {0};
	struct fiemap_extent_group group = {0};
	struct stat64 st = {0};
	unsigned char *vec = NULL;
	unsigned int pages;

	assert(file);
	st.st_size = block_size;
	assert(ftruncate64(fileno(file), st.st_size) == 0);
	move.len = 1;
	mincore_error = EIO;
	assert(page_in_core(fileno(file), move, &vec, &pages) == -1);
	assert(errno == EIO && vec == NULL && mappings == 0);
	mincore_error = 0;
	group.start = group.end = &node;
	group.next = group.prev = &group;
	group.len = 1;
	move_count = mapping_failures = 0;
	mapping_error = ENOMEM;
	assert(call_defrag(fileno(file), fileno(file), "oom", &st, &group, &move_result) == -1);
	assert(move_count == 0 && mapping_failures == 1);
	mapping_error = 0;
	move_error = EIO;
	assert(call_defrag(fileno(file), fileno(file), "io", &st, &group, &move_result) == -1);
	assert(errno == EIO && move_count == 1 && mappings == 0);
	move_error = 0;
	/* A range containing only holes must still advance to completion. */
	skipped_blocks = 1;
	move_count = 0;
	assert(call_defrag(fileno(file), fileno(file), "hole", &st, &group, &move_result) == 0);
	assert(move_count == 1);
	skipped_blocks = 0;
	fclose(file);
}

static void run_file_case(const char *path, const struct stat64 *st,
			  int flag, enum defrag_outcome expected, const char *message)
{
	FILE *capture = tmpfile();
	char output[8192];
	size_t len;
	int saved_stdout;

	assert(capture);
	fflush(stdout);
	saved_stdout = dup(STDOUT_FILENO);
	assert(saved_stdout >= 0);
	assert(dup2(fileno(capture), STDOUT_FILENO) >= 0);
	fixture_maps = move_count = 0;
	had_error = 0;
	memset(outcomes, 0, sizeof(outcomes));
	assert(file_defrag(path, st, flag, NULL) == 0);
	fflush(stdout);
	assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
	close(saved_stdout);
	rewind(capture);
	len = fread(output, 1, sizeof(output) - 1, capture);
	output[len] = 0;
	fclose(capture);
	assert(outcomes[expected] == 1);
	assert(had_error == (expected == RESULT_FAILED));
	assert(!strchr(output, '\033') && !strchr(output, '\r'));
	if (message)
		assert(strstr(output, message));
	if (expected == RESULT_MOVED || expected == RESULT_UNCHANGED)
		assert(move_count > 0);
	if (expected == RESULT_SKIPPED || expected == RESULT_EXCLUDED)
		assert(move_count == 0);
}

static void run_main_case(int argc, char **argv, int expected)
{
	pid_t pid;
	int status;

	fflush(NULL);
	pid = fork();
	assert(pid >= 0);
	if (pid == 0) {
		fixture_mount = 1;
		fixture_maps = 0;
		mode_flag = 0;
		optind = 0;
		exit(e4defrag_main(argc, argv));
	}
	assert(waitpid(pid, &status, 0) == pid);
	assert(WIFEXITED(status) && WEXITSTATUS(status) == expected);
}

static void check_file_results(void)
{
	char path[] = "/tmp/e4defrag-status-XXXXXX";
	struct stat64 st, other;
	int fd = mkstemp(path);

	assert(fd >= 0);
	assert(fallocate(fd, 0, 0, 30 * block_size) == 0);
	assert(fstat64(fd, &st) == 0);
	close(fd);
	fixture = 1;
	current_uid = getuid();
	interactive_output = mode_flag = 0;
	lost_found_dir[0] = 0;
	run_file_case(path, &st, FTW_F, RESULT_MOVED, "MOVED; moved 122880 bytes");
	mode_flag = DETAIL;
	run_file_case(path, &st, FTW_F, RESULT_MOVED, "extents 3 -> 1");
	after_error = EIO;
	run_file_case(path, &st, FTW_F, RESULT_MOVED, "after-map unavailable");
	after_error = 0;
	mode_flag = 0;
	donor_runs = 3;
	run_file_case(path, &st, FTW_F, RESULT_SKIPPED, "donor not better");
	donor_runs = 1;
	source_runs = 1;
	run_file_case(path, &st, FTW_F, RESULT_SKIPPED, "already meets estimated target");
	source_runs = 3;
	allocation_error = ENOSPC;
	run_file_case(path, &st, FTW_F, RESULT_FAILED, "allocating donor");
	allocation_error = 0;
	no_space = 1;
	run_file_case(path, &st, FTW_F, RESULT_FAILED, "checking free space");
	no_space = 0;
	lock_file = 1;
	run_file_case(path, &st, FTW_F, RESULT_SKIPPED, "file locked");
	lock_file = 0;
	move_error = EIO;
	partial_blocks = 7;
	run_file_case(path, &st, FTW_F, RESULT_FAILED, "moved 28672 bytes");
	move_error = EFAULT;
	run_file_case(path, &st, FTW_F, RESULT_FAILED, "total unknown");
	move_error = 0;
	partial_blocks = 0;
	skipped_blocks = 30;
	run_file_case(path, &st, FTW_F, RESULT_UNCHANGED, "UNCHANGED");
	skipped_blocks = 0;
	other = st;
	other.st_size = 0;
	run_file_case(path, &other, FTW_F, RESULT_SKIPPED, "empty file");
	other.st_mode = S_IFDIR | 0700;
	run_file_case(path, &other, FTW_D, RESULT_EXCLUDED, NULL);
	run_file_case("bad\n\033name", NULL, FTW_NS, RESULT_FAILED,
		      "\"bad\\x0a\\x1bname\": FAILED");
	{
		char *good[] = { "e4defrag", path, NULL };
		char *mixed[] = { "e4defrag", path, "/no/such/e4defrag-target", NULL };
		char *reverse[] = { "e4defrag", "/no/such/e4defrag-target", path, NULL };

		run_main_case(2, good, 0);
		run_main_case(3, mixed, 1);
		run_main_case(3, reverse, 1);
	}
	assert(unlink(path) == 0);
	run_file_case(path, &st, FTW_F, RESULT_FAILED, "opening source");
	fixture = 0;
}

int main(void)
{
	FILE *progress = tmpfile();
	int saved_stdout;

	assert(progress);
	fflush(stdout);
	saved_stdout = dup(STDOUT_FILENO);
	assert(saved_stdout >= 0);
	assert(dup2(fileno(progress), STDOUT_FILENO) >= 0);
	block_size = 4096;
	check_extent_allocation_failure();
	check_sparse_groups();
	check_large_group(0);
	check_large_group(1);
	check_mapping_errors();
	check_file_results();
	fflush(stdout);
	assert(dup2(saved_stdout, STDOUT_FILENO) >= 0);
	close(saved_stdout);
	fclose(progress);
	puts("e4defrag: extent bookkeeping OK");
	return 0;
}
