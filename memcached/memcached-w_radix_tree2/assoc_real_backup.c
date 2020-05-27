/* -*- Mode: C; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil -*- */
/*
 * Hash table
 *
 * The hash function used here is by Bob Jenkins, 1996:
 *    <http://burtleburtle.net/bob/hash/doobs.html>
 *       "By Bob Jenkins, 1996.  bob_jenkins@burtleburtle.net.
 *       You may use this code any way you wish, private, educational,
 *       or commercial.  It's free."
 *
 * The rest of the file is licensed under the BSD license.  See LICENSE.
 */

#include "memcached.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/signal.h>
#include <sys/resource.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pthread.h>

typedef  unsigned long  int  ub4;   /* unsigned 4-byte quantities */
typedef  unsigned       char ub1;   /* unsigned 1-byte quantities */


/* how many powers of 2's worth of buckets we use */
/* [KH] These three lines are actually not used, however, don't remove those. There are some dependency somewhere else. */
unsigned int hashpower = HASHPOWER_DEFAULT;
#define hashsize(n) ((ub4)1<<(n))
#define hashmask(n) (hashsize(n)-1)


/* added by [KH] */
#include <string.h>
#include <malloc.h>
#include <time.h>
#include <x86intrin.h>
/*			    */


#define mfence() asm volatile("mfence":::"memory")
#define sfence() asm volatile("sfence":::"memory")

void flush_buffer(void *buf, unsigned long len, bool fence)
{	
	unsigned long i;
	len = len + ((unsigned long)(buf) & (CACHE_LINE_SIZE - 1));
	if (fence) {
		//		sfence();
		for (i = 0; i < len; i += CACHE_LINE_SIZE)
			asm volatile ("clflush %0\n" : "+m" (*(char *)((unsigned long)buf+i)));
		sfence();
	} else {
		for (i = 0; i < len; i += CACHE_LINE_SIZE)
			asm volatile ("clflush %0\n" : "+m" (*(char *)((unsigned long)buf+i)));
	}

}


/* W_RADIX_TREE */
static tree *T;

node *allocNode(node *parent, unsigned int index)
{
	node *new_node = malloc(sizeof(node));
	memset(new_node, 0, sizeof(node));
	if (parent != NULL) {
		new_node->parent_ptr = parent;
		new_node->p_index = index;
	}

	return new_node;
}


tree *initTree()
{
	tree *wradix_tree = malloc(sizeof(tree));
	wradix_tree->root = allocNode(NULL, 0);
	wradix_tree->height = 1;
	return wradix_tree;
}


void assoc_init(const int hashtable_init) {  /*[KH] The variable "hashtable_init" is not used as real */
	T = initTree();
	flush_buffer(T, 8, true);

	if (! T) {
		fprintf(stderr, "Failed to init w_radix_tree.\n");
		exit(EXIT_FAILURE);
	}
}


item *assoc_find(const char *key, const size_t nkey, const uint32_t hv) {
	unsigned long _key = hv;

	node *level_ptr;
	unsigned long max_keys;
	unsigned char height;
	unsigned int bit_shift, idx, meta_bits = META_NODE_SHIFT, blk_shift;
	void *value;

	height = T->height;
	level_ptr = T->root;

	blk_shift = height * meta_bits;

	max_keys = 0x1UL << blk_shift;

	if (_key > max_keys - 1) {
		return NULL;
	}

	while (height > 1) {
		bit_shift = (height - 1) * META_NODE_SHIFT;
		idx = _key >> bit_shift;
		level_ptr = level_ptr->entry_ptr[idx];
		if (level_ptr == NULL)
			return NULL;

		_key = _key & ((0x1UL << bit_shift) - 1);
		height--;
	}
	bit_shift = (height - 1) * META_NODE_SHIFT;
	idx = _key >> bit_shift;

	value = level_ptr->entry_ptr[idx];

	return (item *)value;
}



tree *CoW_Tree(node *changed_root, unsigned char height)
{
	tree *changed_tree = malloc(sizeof(tree)); 
	changed_tree->root = changed_root;	
	changed_tree->height = height;	
	flush_buffer(changed_tree, sizeof(tree), false);
	return changed_tree;
}


int increase_radix_tree_height(tree **t, unsigned char new_height)
{
	unsigned char height = (*t)->height;
	node *root, *prev_root;
	int errval = 0;

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
	return errval;
}

int recursive_alloc_nodes(node *temp_node, unsigned long key, item *value,
		unsigned char height)
{
	int errval = -1;
	unsigned int meta_bits = META_NODE_SHIFT, node_bits;
	unsigned long next_key;
	unsigned int index;

	node_bits = (height - 1) * meta_bits;

	index = key >> node_bits;

	if (height == 1) {
		temp_node->entry_ptr[index] = value;		// [KH] type mismatch, so needed to cast when search
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
				value, height - 1);
		if (errval < 0)
			goto fail;
	}
	errval = 0;
fail:
	return errval;
}


int recursive_search_leaf(node *level_ptr, unsigned long key, item *value, 
		unsigned char height)
{
	int errval = -1;
	unsigned int meta_bits = META_NODE_SHIFT, node_bits;
	unsigned long next_key;
	unsigned int index;

	node_bits = (height - 1) * meta_bits;

	index = key >> node_bits;

	if (height == 1) {
		level_ptr->entry_ptr[index] = value;
		flush_buffer(level_ptr->entry_ptr[index], 8, true); 

		if (level_ptr->entry_ptr[index] == NULL)
			goto fail;
	}
	else {
		if (level_ptr->entry_ptr[index] == NULL) {
			/* delayed commit */
			node *tmp_node = allocNode(level_ptr, index);
			next_key = (key & ((0x1UL << node_bits) - 1));
			errval = recursive_alloc_nodes(tmp_node, next_key, value, 
					height - 1);

			if (errval < 0)
				goto fail;

			sfence();   
			level_ptr->entry_ptr[index] = tmp_node;
			flush_buffer(level_ptr->entry_ptr[index], 8, true);
			return errval;
		}
		next_key = (key & ((0x1UL << node_bits) - 1));

		errval = recursive_search_leaf(level_ptr->entry_ptr[index], next_key, 
				value, height - 1);
		if (errval < 0)
			goto fail;
	}
	errval = 0;
fail:
	return errval;
}


int assoc_insert(item *it, const uint32_t hv) {
	unsigned long key = hv;
	tree **t = &T;

	int errval;
	unsigned long max_keys;
	unsigned char height;
	unsigned int blk_shift, meta_bits = META_NODE_SHIFT;
	unsigned long total_keys;

	height = (*t)->height;

	blk_shift = height * meta_bits;

	max_keys = 0x1UL << blk_shift;

	if (key > max_keys - 1) {
		/* Radix tree height increases as a result of this allocation */
		total_keys = key >> blk_shift;
		while (total_keys > 0) {
			total_keys = total_keys >> meta_bits;
			height++;
		}
	}

	if (height == 0) {
		printf("[assoc_insert] height==0 | return 1\n");
		return 1; 
	}

	if(height > (*t)->height) {
		/* delayed commit */
		printf("[assoc_insert] JUST BEFORE PAUSE_ALL_THREADS!\n");
		pause_threads(PAUSE_ALL_THREADS);
		printf("[assoc_insert] PAUSE_ALL_THREADS!\n");
		tree *tmp_t = *t;
		tree *prev_tree = *t;
		printf("[assoc_insert] hv : %u, tmp_t : %p, prev_tree : %p ,(height:%u, (*t)->height:%u)\n", hv, (void *)tmp_t, (void *)prev_tree, height, (*t)->height);
		errval = increase_radix_tree_height(&tmp_t, height);		/* [KH] if there is an error, then errval = 0 */
		printf("[assoc_insert] increase_radix_tree_height | hv : %u, tmp_t : %p, prev_tree : %p ,(height:%u, (*t)->height:%u)\n", hv, (void *)tmp_t, (void *)prev_tree, height, (*t)->height);

		if(errval) {
			printf ("Increase radix tree height error!\n");
			goto fail;
		}

		errval = recursive_alloc_nodes(tmp_t->root, key, it, height);

		if (errval < 0)
			goto fail;

		sfence(); 
		*t = tmp_t;
		flush_buffer(*t, 8, true); 
		printf("[assoc_insert] hv : %u, free(%p), (height:%u, (*t)->height:%u)\n", hv, (void *)prev_tree, height, (*t)->height);
		free(prev_tree);
		printf("[assoc_insert] RESUME_ALL_THREADS!\n");
		pause_threads(RESUME_ALL_THREADS);
		printf("[assoc_insert] JUST AFTER RESUME_ALL_THREADS!\n"); 

		return 1; 
	}

	errval = recursive_search_leaf((*t)->root, key, it, height);

	if (errval < 0)
		goto fail;

	return 1; 
fail:
	printf("[assoc_insert] fail!\n");
	exit(0);

}

void assoc_delete(const char *key, const size_t nkey, const uint32_t hv) {
	unsigned long _key = hv;

	node *level_ptr;
	unsigned long max_keys;
	unsigned char height;
	unsigned int bit_shift, idx, meta_bits = META_NODE_SHIFT, blk_shift;
	void *value;

	height = T->height;
	level_ptr = T->root;

	blk_shift = height * meta_bits;

	max_keys = 0x1UL << blk_shift;

	if (_key > max_keys - 1) {
		return;
	}

	while (height > 1) {
		bit_shift = (height - 1) * META_NODE_SHIFT;
		idx = _key >> bit_shift;
		level_ptr = level_ptr->entry_ptr[idx];
		if (level_ptr == NULL)
			return;

		_key = _key & ((0x1UL << bit_shift) - 1);
		height--;
	}
	bit_shift = (height - 1) * META_NODE_SHIFT;
	idx = _key >> bit_shift;

	value = level_ptr->entry_ptr[idx];
	if (value != NULL) {
		level_ptr->entry_ptr[idx] = NULL;
		flush_buffer(&level_ptr->entry_ptr[idx], 8, true);
		return;
	} else
		assert((item *)value != 0);
}



