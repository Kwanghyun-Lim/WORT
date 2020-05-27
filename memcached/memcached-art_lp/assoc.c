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
unsigned long leaf_count = 0;
unsigned long clflush_count = 0;
unsigned long mfence_count = 0;
unsigned long path_comp_count = 0;


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
#define sfence() asm volatile("sfence":::"memory")

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
	unsigned long i;
	len = len + ((unsigned long)(buf) & (CACHE_LINE_SIZE - 1));
	if (fence) {
		mfence();
		for (i = 0; i < len; i += CACHE_LINE_SIZE) {
			clflush_count++;
//			etsc = read_tsc() + (unsigned long)(LATENCY * CPU_FREQ_MHZ / 1000);
			asm volatile ("clflush %0\n" : "+m" (*(char *)((unsigned long)buf+i)));
//			while (read_tsc() < etsc)
//				cpu_pause();
		}
		mfence();
		mfence_count = mfence_count + 2;
	} else {
		for (i = 0; i < len; i += CACHE_LINE_SIZE) {
			clflush_count++;
//			etsc = read_tsc() + (unsigned long)(LATENCY * CPU_FREQ_MHZ / 1000);
			asm volatile ("clflush %0\n" : "+m" (*(char *)((unsigned long)buf+i)));
//			while (read_tsc() < etsc)
//				cpu_pause();
		}
	}
}
/**
 * Allocates a node of the given type,
 * initializes to zero and sets the type.
 */
static art_node* alloc_node() {
	art_node* n;
	n = (art_node*)calloc(1, sizeof(art_node16));
	node_count++;
	return n;
}

/**
 * Initializes an ART tree
 * @return 0 on success.
 */

static art_tree *T;

int art_tree_init(art_tree *t) {		//ok	//ok
	printf("[art_tree_init]\n");
    t->root = NULL;
    t->size = 0;
	printf("?\n");
    return 0;
}

static art_node** find_child(art_node *n, unsigned char c) {
	art_node16 *p;

	p = (art_node16 *)n;
	if (p->children[c])
		return &p->children[c];

	return NULL;
}

// Simple inlined if
static inline int min(int a, int b) {		//ok	//ok
    return (a < b) ? a : b;
}

/**
 * Returns the number of prefix characters shared between
 * the key and node.
 */
static int check_prefix(const art_node *n, const unsigned char *key, int key_len, int depth) {		//ok	//ok
    int max_cmp = min(min(n->partial_len, MAX_PREFIX_LEN), key_len - depth);
    int idx;
    for (idx=0; idx < max_cmp; idx++) {
        if (n->partial[idx] != key[depth+idx])
            return idx;
    }
    return idx;
}


/**
 * Checks if a leaf matches
 * @return 0 on success.
 */

/* [kh] I changed "art_leaf" to "item" */
static int item_matches(const item *it, const unsigned char *key, int key_len, int depth) {	//ok	//ok
    (void)depth;
    // Fail if the key lengths are different
    if (it->nkey != (uint32_t)key_len) return 1;

    // Compare the keys starting at the depth
    return memcmp(ITEM_key(it), key, key_len);
}


void assoc_init(const int hashtable_init) {  /*[KH] The variable "hashtable_init" is not used as real */
   printf("1\n");
   	T = (art_tree *)malloc(sizeof(art_tree));
	art_tree_init(T);
	printf("2\n");
}


item *assoc_find(const char *_key, const size_t nkey) { //ok	//ok
	//printf("[assoc_find]\n");
	unsigned char *key = (unsigned char *)_key;
    art_node **child;
    art_node *n = T->root;
    int prefix_len, depth = 0;
    while (n) {
        // Might be a leaf
        if (IS_ITEM(n)) {	/* [kh] I changed "art_leaf" to "item" */
            n = (art_node*)ITEM_RAW(n);
            // Check if the expanded path matches
            if (!item_matches((item*)n, key, nkey, depth)) {
                return (item*)n;
            }
            return NULL;
        }

        // Bail if the prefix does not match
        if (n->partial_len) {
            prefix_len = check_prefix(n, key, nkey, depth);
            if (prefix_len != min(MAX_PREFIX_LEN, n->partial_len))
                return NULL;
            depth = depth + n->partial_len;
        }

        // Recursively search
		
        child = find_child(n, key[depth]);
        n = (child) ? *child : NULL;
        depth++;
    }
    return NULL;
}


// Find the minimum leaf under a node
static item* minimum(const art_node *n) {		//ok	//ok
    // Handle base cases
    if (!n) return NULL;
    if (IS_ITEM(n)) return ITEM_RAW(n);

	int idx = 0;

	while (!((art_node16 *)n)->children[idx]) idx++;
	return minimum(((art_node16 *)n)->children[idx]);
}

/**
 * Returns the minimum valued leaf
 */
item* art_minimum(art_tree *t) {		//ok	//ok
    return minimum((art_node*)t->root);
}
/*
static art_leaf* make_leaf(const char *key, int key_len, void *value) {
    art_leaf *l = (art_leaf*)malloc(sizeof(art_leaf)+key_len);
    l->value = value;
    l->key_len = key_len;
    memcpy(l->key, key, key_len);
    return l;
}
*/

static int longest_common_prefix(item *it1, item *it2, int depth) { //ok	//ok
    int max_cmp = min(it1->nkey, it2->nkey) - depth;
    int idx;
    for (idx=0; idx < max_cmp; idx++) {
        if (ITEM_key(it1)[depth+idx] != ITEM_key(it2)[depth+idx])
            return idx;
    }
    return idx;
}
/*
static void copy_header(art_node *dest, art_node *src) {		//ok	//ok
    dest->partial_len = src->partial_len;
    memcpy(dest->partial, src->partial, min(MAX_PREFIX_LEN, src->partial_len));
}
*/

static void add_child(art_node16 *n, art_node **ref, unsigned char c, void *child) {
	(void)ref;
	n->children[c] = (art_node*)child;
}

/**
 * Calculates the index at which the prefixes mismatch
 */
static int prefix_mismatch(const art_node *n, const unsigned char *key, int key_len, int depth, item **it) { //ok	//ok
    int max_cmp = min(min(MAX_PREFIX_LEN, n->partial_len), key_len - depth);
    int idx;
    for (idx=0; idx < max_cmp; idx++) {
        if (n->partial[idx] != key[depth+idx])
            return idx;
    }

    // If the prefix is short we can avoid finding a leaf
    if (n->partial_len > MAX_PREFIX_LEN) {
        // Prefix is longer than what we've checked, find a leaf
        *it = minimum(n);
        max_cmp = min((*it)->nkey, key_len)- depth;
        for (; idx < max_cmp; idx++) {
            if (ITEM_key(*it)[idx+depth] != key[depth+idx])
                return idx;
        }
    }
    return idx;
}

static void* recursive_insert(art_node *n, art_node **ref, item *inserted_it, int depth) {  //ok	//ok
   	const unsigned char *key = (unsigned char *)ITEM_key(inserted_it);
	int key_len = inserted_it->nkey;
	
	// If we are at a NULL node, inject a leaf
    if (!n) {
        *ref = (art_node*)SET_ITEM(inserted_it);
		flush_buffer(*ref, sizeof(item), false);
		flush_buffer(ref, 8, true);
        return NULL;
    }

    // If we are at a leaf, we need to replace it with a node
    if (IS_ITEM(n)) {
        item *existing_it = ITEM_RAW(n);
		
        // Check if we are updating an existing value
        if (!item_matches(existing_it, key, key_len, depth)) {
			printf("[assoc_insert] The same value has been inserted !!!!\n");
            exit(1);
        }
		
        // New value, we must split the leaf into a node4
		art_node16 *new_node = (art_node16 *)alloc_node();

        // Determine longest prefix
        int longest_prefix = longest_common_prefix(existing_it, inserted_it, depth);
        new_node->n.partial_len = longest_prefix;
        memcpy(new_node->n.partial, key+depth, min(MAX_PREFIX_LEN, longest_prefix));
        // Add the leafs to the new node4
        add_child(new_node, ref, key[depth+longest_prefix], SET_ITEM(inserted_it));
        add_child(new_node, ref, ITEM_key(existing_it)[depth+longest_prefix], SET_ITEM(existing_it));

		flush_buffer(new_node, sizeof(art_node16), false);
		flush_buffer(inserted_it, sizeof(item), false);

        *ref = (art_node*)new_node;
		flush_buffer(ref, 8, true);
        return NULL;
    }

    // Check if given node has a prefix
    if (n->partial_len) {
		item *min_it = NULL;
        // Determine if the prefixes differ, since we need to split
        int prefix_diff = prefix_mismatch(n, key, key_len, depth, &min_it);
        if ((uint32_t)prefix_diff >= n->partial_len) {
            depth += n->partial_len;
            goto RECURSE_SEARCH;
        }

		path_comp_count++;

        // Create a new node
		art_node16 *new_node = (art_node16 *)alloc_node();
		art_node *copy_node = (art_node *)alloc_node();
		memcpy(copy_node, n, sizeof(art_node16));

        new_node->n.partial_len = prefix_diff;
        memcpy(new_node->n.partial, copy_node->partial, min(MAX_PREFIX_LEN, prefix_diff));

        // Adjust the prefix of the old node
        if (copy_node->partial_len <= MAX_PREFIX_LEN) {
            add_child(new_node, ref, copy_node->partial[prefix_diff], copy_node);
            copy_node->partial_len -= (prefix_diff+1);
            memmove(copy_node->partial, copy_node->partial+prefix_diff+1,
                    min(MAX_PREFIX_LEN, copy_node->partial_len));
        } else {
            copy_node->partial_len -= (prefix_diff+1);
			if (min_it == NULL)
				min_it = minimum(copy_node);
       	    add_child(new_node, ref, ITEM_key(min_it)[depth + prefix_diff], copy_node);
            memcpy(copy_node->partial, ITEM_key(min_it)+depth+prefix_diff+1,
                    min(MAX_PREFIX_LEN, copy_node->partial_len));
        }

        // Insert the new leaf
		add_child(new_node, ref, key[depth + prefix_diff], SET_ITEM(inserted_it));
	
		flush_buffer(new_node, sizeof(art_node16), false);
		flush_buffer(inserted_it, sizeof(item), false);
		flush_buffer(copy_node, sizeof(art_node16), false);
        
		*ref = (art_node*)new_node;	//
		flush_buffer(ref, 8, true);

		node_count--;
		free(n);
        
        return NULL;
    }

RECURSE_SEARCH:;

    // Find a child to recurse to
    art_node **child = find_child(n, key[depth]);
    if (child) {
        return recursive_insert(*child, child, inserted_it, depth+1);
    }

	flush_buffer(inserted_it, sizeof(item), false);
    // No child, node goes within us
    add_child((art_node16 *)n, ref, key[depth], SET_ITEM(inserted_it));
    flush_buffer(&((art_node16 *)n)->children[key[depth]], 8, true);
    return NULL;
}

int assoc_insert(item *it) {	//ok	//ok
	//printf("[assoc_insert]\n");
    void *old = recursive_insert(T->root, &T->root, it, 0);
    if (!old) 
    {
        return 1;
    } else 
    {
        printf("[assoc_insert] fail!!\n");
        exit(1);
    }
}

void assoc_delete(const char *_key, const size_t nkey) {			//ok
	//printf("[assoc_delete]\n");

	unsigned char *key = (unsigned char *)_key;
    art_node **child = NULL;
    art_node *n = T->root;
    int prefix_len, depth = 0;
    while (n) {
        // Might be a leaf
        if (IS_ITEM(n)) {	/* [kh] I changed "art_leaf" to "item" */
            n = (art_node*)ITEM_RAW(n);
            // Check if the expanded path matches
            if (!item_matches((item*)n, key, nkey, depth)) {
                *child = NULL;
				return;
            }
			return;
        }

        // Bail if the prefix does not match
        if (n->partial_len) {
            prefix_len = check_prefix(n, key, nkey, depth);
            if (prefix_len != min(MAX_PREFIX_LEN, n->partial_len))
                return;
            depth = depth + n->partial_len;
        }

        // Recursively search
		
        child = find_child(n, key[depth]);
        n = (child) ? *child : NULL;
        depth++;
    }
    return;
}



