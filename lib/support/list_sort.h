/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_LIST_SORT_H
#define _LINUX_LIST_SORT_H

#include <ext2fs/ext2_types.h>

typedef unsigned long long ext4_fsblk_t;

struct fiemap_extent_data {
        __u64 len;                      /* blocks count */
        __u64 logical;          /* start logical block number */
        ext4_fsblk_t physical;          /* start physical block number */
};

struct fiemap_extent_list {
        struct fiemap_extent_list *next;
        struct fiemap_extent_list *prev;
        struct fiemap_extent_data data; /* extent belong to file */
} __attribute__((__packed__));

/* I wanted to use list_head to keep code identical to kernel,
 * and allow using same function for different types of lists.
 * However, at least O3 LTO garbles cast structs,
 * and forcing same head struct would require hard to merge
 * changes in e4defrag.c, so it's hardcoded to 
 * fiemap_extent_list for now.

struct list_head {
        struct list_head *next, *prev;
} __attribute__((__packed__));

*/

#define list_head	fiemap_extent_list

__attribute__((nonnull(2,3)))
void list_sort(void *priv, struct list_head *head,
	       int (*cmp)(void *priv, struct list_head *a,
			  struct list_head *b));
#endif
