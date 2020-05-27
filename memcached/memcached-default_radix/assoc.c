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
#include <strings.h>
#include <emmintrin.h>

#define mfence() asm volatile("mfence":::"memory")

unsigned long node_count = 0;
unsigned long clflush_count = 0;
unsigned long mfence_count = 0;

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


#define mfence() asm volatile("mfence":::"memory")

#define IS_ITEM(x) (((uintptr_t)x & 1))
#define SET_ITEM(x) ((void*)((uintptr_t)x | 1))
#define ITEM_RAW(x) ((item*)((void*)((uintptr_t)x & ~1)))

#define LATENCY			200
#define CPU_FREQ_MHZ	2400

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
			asm volatile ("clflush %0\n" : "+m" (*(char *)((unsigned long)buf+i)));
			while (read_tsc() < etsc)
				cpu_pause();
		}
		mfence();
		mfence_count = mfence_count + 2;
	} else {
		for (i = 0; i < len; i += CACHE_LINE_SIZE) {
			clflush_count++;
			etsc = read_tsc() + (unsigned long)(LATENCY * CPU_FREQ_MHZ / 1000);
			asm volatile ("clflush %0\n" : "+m" (*(char *)((unsigned long)buf+i)));
			while (read_tsc() < etsc)
				cpu_pause();
		}
	}
}

static tree *T;

static node *allocNode(void)
{
	//node *new_node = calloc(1, sizeof(node));
	node *new_node;
	posix_memalign((void *)&new_node, 64, sizeof(node));
	memset(new_node, 0, sizeof(node));
	node_count++;
	return new_node;
}

tree *initTree(void)
{
	tree *wradix_tree = malloc(sizeof(tree));
	wradix_tree->root = allocNode();
	wradix_tree->height = 1;
	flush_buffer(wradix_tree, sizeof(tree), true);
	return wradix_tree;
}

void assoc_init(const int hashtable_init) {  /*[KH] The variable "hashtable_init" is not used as real */
	printf("Init default radix 1\n");
	T = initTree();
	printf("2\n");
}

item *assoc_find(const char *_key, const size_t nkey) { //ok	//ok
	unsigned char *key = (unsigned char *)_key;
	int key_len = (int)nkey;

	node *level_ptr;
	int height, idx;
	void *value;

	height = T->height;
	level_ptr = T->root;

	while (height > 1) {
		if (key_len - height >= 0)
			idx = key[key_len - height];
		else
			idx = 0;

		level_ptr = level_ptr->entry_ptr[idx];
		if (level_ptr == NULL)
			return NULL;
		height--;
	}
	idx = key[key_len - height];
	value = level_ptr->entry_ptr[idx];
	return (item *)value;
}

static tree *CoW_Tree(node *changed_root, int height)
{
	tree *changed_tree = malloc(sizeof(tree));
	changed_tree->root = changed_root;
	changed_tree->height = height;
	flush_buffer(changed_tree, sizeof(tree), false);
	return changed_tree;
}

static int increase_radix_tree_height(tree **t, int new_height)
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

static int recursive_alloc_nodes(node *temp_node, unsigned char *key, int key_len,
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

static int recursive_search_leaf(node *level_ptr, unsigned char *key, int key_len,
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

static int Insert(tree **t, unsigned char *key, int key_len, void *value) 
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

int assoc_insert(item *it) {	//ok	//ok
	unsigned char *key = (unsigned char *)ITEM_key(it);
	int errval, key_len = it->nkey;
	void *value = (void *)it;

	errval = Insert(&T, key, key_len, value);
	if (errval == 0)
		return 1;
	else
		return -1;
}

void assoc_delete(const char *key, const size_t nkey) {			//ok
	unsigned char *_key = (unsigned char *)key;
	int key_len = (int)nkey;

	node *level_ptr;
	int height, idx;

	height = T->height;
	level_ptr = T->root;

	while (height > 1) {
		if (key_len - height >= 0)
			idx = _key[key_len - height];
		else
			idx = 0;

		level_ptr = level_ptr->entry_ptr[idx];
		if (level_ptr == NULL)
			return ;
		height--;
	}
	idx = _key[key_len - height];
	level_ptr->entry_ptr[idx] = NULL;
	return ;
}
