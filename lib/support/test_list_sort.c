/* SPDX-License-Identifier: GPL-2.0 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "list_sort.h"

static int compare(void *priv, const struct list_head *a,
		   const struct list_head *b)
{
	unsigned int *calls = priv;

	(*calls)++;
	return a->data.physical > b->data.physical;
}

static void check_sort(unsigned int count, unsigned int order)
{
	struct list_head head, *nodes, *p;
	unsigned int i, calls = 0;

	nodes = calloc(count ? count : 1, sizeof(*nodes));
	assert(nodes);
	head.next = head.prev = &head;
	for (i = 0; i < count; i++) {
		nodes[i].data.logical = i;
		/* Equal keys exercise stability as well as ordering. */
		nodes[i].data.physical = order == 0 ? i / 4 :
			order == 1 ? (count - i) / 4 : (i * 7919U) % 31;
		nodes[i].prev = head.prev;
		nodes[i].next = &head;
		head.prev->next = &nodes[i];
		head.prev = &nodes[i];
	}
	list_sort(&calls, &head, compare);
	p = head.next;
	for (i = 0; i < count; i++, p = p->next) {
		assert(p != &head);
		assert(p->next->prev == p && p->prev->next == p);
		if (p->next != &head) {
			assert(p->data.physical <= p->next->data.physical);
			if (p->data.physical == p->next->data.physical)
				assert(p->data.logical < p->next->data.logical);
		}
	}
	assert(p == &head);
	assert(head.next->prev == &head && head.prev->next == &head);
	assert(count < 2 || calls > 0);
	free(nodes);
}

int main(void)
{
	unsigned int count, order;

	for (order = 0; order < 3; order++) {
		for (count = 0; count <= 257; count++)
			check_sort(count, order);
		check_sort(65537, order);
	}
	puts("list_sort: ordering, stability and links OK");
	return 0;
}
