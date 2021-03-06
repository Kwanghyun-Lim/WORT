#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <x86intrin.h>
#include <malloc.h>
#include <time.h>
#include "art_d.h"

#define mfence() asm volatile("mfence":::"memory")

#define LATENCY			400
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
			etsc = read_tsc() + (unsigned long)(LATENCY * CPU_FREQ_MHZ / 1000);
			asm volatile ("clflush %0\n" : "+m" (*(char *)(buf+i)));
			while (read_tsc() < etsc)
				cpu_pause();
		}
		mfence();
		mfence_count = mfence_count + 2;
	} else {
		for (i = 0; i < len; i += CACHE_LINE_SIZE) {
			clflush_count++;
			etsc = read_tsc() + (unsigned long)(LATENCY * CPU_FREQ_MHZ / 1000);
			asm volatile ("clflush %0\n" : "+m" (*(char *)(buf+i)));
			while (read_tsc() < etsc)
				cpu_pause();
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

node *allocNode(node *parent, unsigned long index)
{
	//node *new_node = calloc(1, sizeof(node));
	node *new_node;
	posix_memalign((void *)&new_node, 64, sizeof(node));
	memset(new_node, 0, sizeof(node));
	if (parent != NULL) {
		new_node->parent_ptr = parent;
		new_node->p_index = index;
	}
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

tree *CoW_Tree(node *changed_root, unsigned long height)
{
	tree *changed_tree = malloc(sizeof(tree));
	changed_tree->root = changed_root;
	changed_tree->height = height;
	flush_buffer(changed_tree, sizeof(tree), false);
	return changed_tree;
}

int increase_radix_tree_height(tree **t, unsigned long new_height)
{
	unsigned long height = (*t)->height;
	node *root, *prev_root;
	int errval = 0;
	//	struct timespec t1, t2;

	prev_root = (*t)->root;

	while (height < new_height) {
		/* allocate the tree nodes for increasing height */
		root = allocNode(NULL, 0);
		if (root == NULL){
			errval = 1;
			return errval;
		}
		root->entry_ptr[0] = prev_root;
		prev_root->parent_ptr = root;
		flush_buffer(prev_root, sizeof(node), false);
		prev_root = root;
		height++;
	}
	flush_buffer(prev_root, sizeof(node), false);
	*t = CoW_Tree(prev_root, height);
	//	flush_buffer(*t, 8);
	return errval;
}

int recursive_alloc_nodes(node *temp_node, unsigned long key, void *value,
		unsigned long height)
{
	int errval = -1;
	unsigned long meta_bits = META_NODE_SHIFT, node_bits;
	unsigned long next_key;
	unsigned long index;

	node_bits = (height - 1) * meta_bits;

	index = key >> node_bits;

	if (height == 1) {
		temp_node->entry_ptr[index] = value;
		flush_buffer(temp_node, sizeof(node), false);
		if (temp_node->entry_ptr[index] == NULL)
			goto fail;
	}
	else {
		if (temp_node->entry_ptr[index] == NULL) {
			temp_node->entry_ptr[index] = allocNode(temp_node, index);
			flush_buffer(temp_node, sizeof(node), false);
			if (temp_node->entry_ptr[index] == NULL)
				goto fail;
		}
		next_key = (key & ((0x1UL << node_bits) - 1));

		errval = recursive_alloc_nodes(temp_node->entry_ptr[index], next_key,
				(void *)value, height - 1);
		if (errval < 0)
			goto fail;
	}
	errval = 0;
fail:
	return errval;
}


int recursive_search_leaf(node *level_ptr, unsigned long key, void *value, 
		unsigned long height)
{
	int errval = -1;
	unsigned long meta_bits = META_NODE_SHIFT, node_bits;
	unsigned long next_key;
	unsigned long index;

	node_bits = (height - 1) * meta_bits;

	index = key >> node_bits;

	if (height == 1) {
		level_ptr->entry_ptr[index] = value;
		flush_buffer(&level_ptr->entry_ptr[index], 8, true);
		if (level_ptr->entry_ptr[index] == NULL)
			goto fail;
	}
	else {
		if (level_ptr->entry_ptr[index] == NULL) {
			/* delayed commit */
			node *tmp_node = allocNode(level_ptr, index);
			next_key = (key & ((0x1UL << node_bits) - 1));
			errval = recursive_alloc_nodes(tmp_node, next_key, (void *)value, 
					height - 1);

			if (errval < 0)
				goto fail;

			level_ptr->entry_ptr[index] = tmp_node;
			flush_buffer(&level_ptr->entry_ptr[index], 8, true);
			return errval;
		}
		next_key = (key & ((0x1UL << node_bits) - 1));

		errval = recursive_search_leaf(level_ptr->entry_ptr[index], next_key, 
				(void *)value, height - 1);
		if (errval < 0)
			goto fail;
	}
	errval = 0;
fail:
	return errval;
}

int Insert(tree **t, unsigned long key, void *value) 
{
	int errval;
	unsigned long max_keys;
	unsigned long height;
	unsigned long blk_shift, meta_bits = META_NODE_SHIFT;
	unsigned long total_keys;

	height = (*t)->height;

	blk_shift = height * meta_bits;

	if (blk_shift < 64) {
		max_keys = 0x1UL << blk_shift;

		if (key > max_keys - 1) {
			/* Radix tree height increases as a result of this allocation */
			total_keys = key >> blk_shift;
			while (total_keys > 0) {
				total_keys = total_keys >> meta_bits;
				height++;
			}
		}
	}

	if (height == 0)
		return 0;

	if(height > (*t)->height) {
		/* delayed commit */
		tree *tmp_t = *t;
		tree *prev_tree = *t;
		errval = increase_radix_tree_height(&tmp_t, height);
		if(errval) {
			printf ("Increase radix tree height error!\n");
			goto fail;
		}

		errval = recursive_alloc_nodes(tmp_t->root, key, (void *)value, height);
		if (errval < 0)
			goto fail;

		*t = tmp_t;
		flush_buffer(*t, 8, true);
		free(prev_tree);
		return 0;
	}
	errval = recursive_search_leaf((*t)->root, key, (void *)value, height);
	if (errval < 0)
		goto fail;

	return 0;
fail:
	return errval;
}

void *Update(tree *t, unsigned long key, void *value)
{
	node *level_ptr;
	unsigned long height;
	unsigned long bit_shift, idx;

	height = t->height;
	level_ptr = t->root;

	while (height > 1) {
		bit_shift = (height - 1) * META_NODE_SHIFT;
		idx = key >> bit_shift;

		level_ptr = level_ptr->entry_ptr[idx];
		if (level_ptr == NULL)
			return level_ptr;

		key = key & ((0x1UL << bit_shift) - 1);
		height--;
	}
	bit_shift = (height - 1) * META_NODE_SHIFT;
	idx = key >> bit_shift;
	level_ptr->entry_ptr[idx] = value;
	flush_buffer(&level_ptr->entry_ptr[idx], 8, true);
	return value;
}

void *Lookup(tree *t, unsigned long key)
{
	node *level_ptr;
	unsigned long height;
	unsigned long bit_shift, idx;
	void *value;

	height = t->height;
	level_ptr = t->root;

	while (height > 1) {
		bit_shift = (height - 1) * META_NODE_SHIFT;
		idx = key >> bit_shift;

		level_ptr = level_ptr->entry_ptr[idx];
		if (level_ptr == NULL)
			return level_ptr;

		key = key & ((0x1UL << bit_shift) - 1);
		height--;
	}
	bit_shift = (height - 1) * META_NODE_SHIFT;
	idx = key >> bit_shift;
	value = level_ptr->entry_ptr[idx];
	return value;
}

node *search_to_next_leaf(node *next_branch, unsigned long height)
{
	int i;
	node *next_leaf;

	if (height != 1) {
		for (i = 0; i < (0x1UL << META_NODE_SHIFT); i++) {
			if (next_branch->entry_ptr[i] != NULL) {
				next_leaf = search_to_next_leaf(next_branch->entry_ptr[i], 
						height - 1);
				return next_leaf;
			}
		}
	}
	else {
		next_leaf = next_branch;
		return next_leaf;
	}
}

node *find_next_leaf(tree *t, node *parent, unsigned long index, 
		unsigned long height)
{
	int i;
	node *next_leaf;

	for (i = (index + 1); i < (0x1UL << META_NODE_SHIFT); i++) {
		if (parent->entry_ptr[i] != NULL) {
			next_leaf = search_to_next_leaf(parent->entry_ptr[i], height - 1);
			return next_leaf;
		}
	}

	if (t->height > height) {
		next_leaf = find_next_leaf(t, parent->parent_ptr, parent->p_index, 
				height + 1);
		return next_leaf;
	}
	else
		return NULL;
}

void Range_Lookup(tree *t, unsigned long start_key, unsigned long num,
		unsigned long buf[])
{
	node *level_ptr;
	unsigned long height;
	unsigned long bit_shift, idx, i;
	unsigned long search_count = 0;
	void *value;

	height = t->height;
	level_ptr = t->root;

	while (height > 1) {
		bit_shift = (height - 1) * META_NODE_SHIFT;
		idx = start_key >> bit_shift;

		level_ptr = level_ptr->entry_ptr[idx];
		start_key = start_key & ((0x1UL << bit_shift) - 1);
		height--;
	}
	bit_shift = (height - 1) * META_NODE_SHIFT;
	idx = start_key >> bit_shift;

	while (search_count < num) {
		for (i = idx; i < (0x1UL << META_NODE_SHIFT); i++) {
			if (level_ptr->entry_ptr[i] != NULL) {
				buf[search_count] = *(unsigned long *)level_ptr->entry_ptr[i];
				search_count++;
				if (search_count == num)
					return ;
			}
		}
		level_ptr = find_next_leaf(t, level_ptr->parent_ptr,
				level_ptr->p_index, 2);
		if (level_ptr == NULL) {
			printf("error\n");
			return ;
		}
		idx = 0;
	}
}

int recursive_free_nodes(tree *t, node *parent, unsigned long index,
		unsigned long height)
{
	int i, errval = 0;

	if (height < t->height) {
		for (i = 0; i < NUM_ENTRY; i++) {
			/* parent node의 entry에 valid entry가 있을 경우 */
			if (i != index && parent->entry_ptr[i] != NULL) {
				parent->entry_ptr[index] = NULL;
				flush_buffer(&parent->entry_ptr[index], 8, true);
				return errval;
			}
		}
		errval = recursive_free_nodes(t, parent->parent_ptr, 
				parent->p_index, height + 1);
	} else {
		parent->entry_ptr[index] = NULL;
		flush_buffer(&parent->entry_ptr[index], 8, true);
	}

	return errval;
}

void Delete(tree *t, unsigned long key)
{
	node *level_ptr;
	unsigned long height;
	unsigned long bit_shift, idx;

	height = t->height;
	level_ptr = t->root;

	while (height > 1) {
		bit_shift = (height - 1) * META_NODE_SHIFT;
		idx = key >> bit_shift;

		level_ptr = level_ptr->entry_ptr[idx];

		key = key & ((0x1UL << bit_shift) - 1);
		height--;
	}
	bit_shift = (height - 1) * META_NODE_SHIFT;
	idx = key >> bit_shift;

	level_ptr->entry_ptr[idx] = NULL;
	flush_buffer(&level_ptr->entry_ptr[idx], 8, true);

	//	recursive_free_nodes(t, level_ptr, idx, 1);
}
