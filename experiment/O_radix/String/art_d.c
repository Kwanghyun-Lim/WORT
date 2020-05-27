#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <x86intrin.h>
#include <malloc.h>
#include <time.h>
#include "art_d.h"

#define mfence() asm volatile("mfence":::"memory")

#define LATENCY			200
#define CPU_FREQ_MHZ	2400

unsigned long node_count = 0;
unsigned long clflush_count = 0;
unsigned long mfence_count = 0;

static inline void cpu_pause()
{
	__asm__ volatile ("pause" ::: "memory");
}

static inline unsigned long read_tsc(void)
{
	unsigned long var;
	unsigned int hi, lo;

	asm volatile ("rdtsc" : "=a" (lo), "=d" (hi));
	var = ((unsigned long long int) hi << 32) | lo;

	return var;
}

void flush_buffer(void *buf, unsigned long len, bool fence)
{
	unsigned long i, etsc;
	len = len + ((unsigned long)(buf) & (CACHE_LINE_SIZE - 1));
	if (fence) {
		mfence();
		for (i = 0; i < len; i += CACHE_LINE_SIZE) {
			clflush_count++;
//			etsc = read_tsc() + (unsigned long)(LATENCY * CPU_FREQ_MHZ / 1000);
			asm volatile ("clflush %0\n" : "+m" (*(char *)(buf+i)));
//			while (read_tsc() < etsc)
//				cpu_pause();
		}
		mfence();
		mfence_count = mfence_count + 2;
	} else {
		for (i = 0; i < len; i += CACHE_LINE_SIZE) {
			clflush_count++;
//			etsc = read_tsc() + (unsigned long)(LATENCY * CPU_FREQ_MHZ / 1000);
			asm volatile ("clflush %0\n" : "+m" (*(char *)(buf+i)));
//			while (read_tsc() < etsc)
//				cpu_pause();
		}
	}
}

void flush_buffer_nocount(void *buf, unsigned long len, bool fence)
{
	unsigned long i;
	len = len + ((unsigned long)(buf) & (CACHE_LINE_SIZE - 1));
	if (fence) {
		mfence();
		for (i = 0; i < len; i += CACHE_LINE_SIZE)
			asm volatile ("clflush %0\n" : "+m" (*(char *)(buf+i)));
		mfence();
	} else {
		for (i = 0; i < len; i += CACHE_LINE_SIZE)
			asm volatile ("clflush %0\n" : "+m" (*(char *)(buf+i)));
	}
}

node *allocNode()
{
	node *new_node = calloc(1, sizeof(node));
	node_count++;
	return new_node;
}

tree *initTree()
{
	tree *wradix_tree = malloc(sizeof(tree));
	wradix_tree->root = allocNode(NULL, 0);
	wradix_tree->height = 1;
	flush_buffer(wradix_tree, sizeof(tree), true);
	return wradix_tree;
}

tree *CoW_Tree(node *changed_root, int height)
{
	tree *changed_tree = malloc(sizeof(tree));
	changed_tree->root = changed_root;
	changed_tree->height = height;
	flush_buffer(changed_tree, sizeof(tree), false);
	return changed_tree;
}

int increase_radix_tree_height(tree **t, int new_height)
{
	int height = (*t)->height;
	node *root, *prev_root;
	int errval = 0;
	//	struct timespec t1, t2;

	prev_root = (*t)->root;

	while (height < new_height) {
		/* allocate the tree nodes for increasing height */
		root = allocNode();
		if (root == NULL){
			errval = 1;
			return errval;
		}
		root->entry_ptr[0] = prev_root;
		flush_buffer(prev_root, sizeof(node), false);
		prev_root = root;
		height++;
	}
	flush_buffer(prev_root, sizeof(node), false);
	*t = CoW_Tree(prev_root, height);
	return errval;
}

int recursive_alloc_nodes(node *temp_node, unsigned char *key, int key_len,
		void *value, int height)
{
	int errval = -1;
	int index;

	if (key_len - height >= 0)
		index = key[key_len - height];
	else
		index = 0;

	if (height == 1) {
		temp_node->entry_ptr[index] = value;
		flush_buffer(temp_node, sizeof(node), false);
		if (temp_node->entry_ptr[index] == NULL)
			goto fail;
	}
	else {
		if (temp_node->entry_ptr[index] == NULL) {
			temp_node->entry_ptr[index] = allocNode();
			flush_buffer(temp_node, sizeof(node), false);
			if (temp_node->entry_ptr[index] == NULL)
				goto fail;
		}

		errval = recursive_alloc_nodes(temp_node->entry_ptr[index], key, key_len,
				(void *)value, height - 1);
		if (errval < 0)
			goto fail;
	}
	errval = 0;
fail:
	return errval;
}


int recursive_search_leaf(node *level_ptr, unsigned char *key, int key_len,
		void *value, int height)
{
	int errval = -1;
	int index;

	if (key_len - height >= 0)
		index = key[key_len - height];
	else
		index = 0;

	if (height == 1) {
		level_ptr->entry_ptr[index] = value;
		flush_buffer(&level_ptr->entry_ptr[index], 8, true);
		if (level_ptr->entry_ptr[index] == NULL)
			goto fail;
	}
	else {
		if (level_ptr->entry_ptr[index] == NULL) {
			/* delayed commit */
			node *tmp_node = allocNode();
			errval = recursive_alloc_nodes(tmp_node, key, key_len, (void *)value, height - 1);

			if (errval < 0)
				goto fail;

			level_ptr->entry_ptr[index] = tmp_node;
			flush_buffer(&level_ptr->entry_ptr[index], 8, true);
			return errval;
		}

		errval = recursive_search_leaf(level_ptr->entry_ptr[index], key, key_len, 
				(void *)value, height - 1);
		if (errval < 0)
			goto fail;
	}
	errval = 0;
fail:
	return errval;
}

int Insert(tree **t, unsigned char *key, int key_len, void *value) 
{
	int errval;
	int height;

	height = (*t)->height;

	if (height < key_len)
		height = key_len;

	if(height > (*t)->height) {
		/* delayed commit */
		tree *tmp_t = *t;
		tree *prev_tree = *t;
		errval = increase_radix_tree_height(&tmp_t, height);
		if(errval) {
			printf ("Increase radix tree height error!\n");
			goto fail;
		}

		errval = recursive_alloc_nodes(tmp_t->root, key, key_len, (void *)value, height);
		if (errval < 0)
			goto fail;

		*t = tmp_t;
		flush_buffer(*t, 8, true);
		free(prev_tree);
		return 0;
	}
	errval = recursive_search_leaf((*t)->root, key, key_len, (void *)value, height);
	if (errval < 0)
		goto fail;

	return 0;
fail:
	return errval;
}

void *Lookup(tree *t, unsigned char *key, int key_len)
{
	node *level_ptr;
	int height, idx;
	void *value;

	height = t->height;
	level_ptr = t->root;

	while (height > 1) {
		if (key_len - height >= 0)
			idx = key[key_len - height];
		else
			idx = 0;

		level_ptr = level_ptr->entry_ptr[idx];
		if (level_ptr == NULL)
			return level_ptr;
		height--;
	}
	idx = key[key_len - height];
	value = level_ptr->entry_ptr[idx];
	return value;
}
