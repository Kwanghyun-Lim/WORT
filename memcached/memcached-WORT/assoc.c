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
unsigned long sum_depth = 0;


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

#define LATENCY			400
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
	unsigned long etsc;
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

void item_flush(void *buf)
{
	asm volatile ("clflush %0\n" : "+m" (*(char *)((unsigned long)buf)));
	clflush_count++;
}

static unsigned char get_partial(const unsigned char *key, int depth)
{
	unsigned char partial;
	if ((depth % 2) == 0)
		partial = ((key[depth / 2] & HIGH_BIT_MASK) >> 4);
	else
		partial = key[depth / 2] & LOW_BIT_MASK;
	return partial;
}

/**
 * Allocates a node of the given type,
 * initializes to zero and sets the type.
 */
static art_node* alloc_node(void) { //ok	//ok
	//printf("[alloc_node]\n");
	art_node* n;
	posix_memalign((void *)&n, 64, sizeof(art_node16));
	memset(n, 0, sizeof(art_node16));
	node_count++;
	return n;
}

/**
 * Initializes an ART tree
 * @return 0 on success.
 */

static art_tree *T;

int art_tree_init(art_tree *t) {		//ok	//ok
	printf("[art_tree_init] 100w100r\n");
    t->root = NULL;
    t->size = 0;
	printf("?\n");
    return 0;
}

static art_node** find_child(art_node *n, unsigned char c) {	//ok	//ok
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
    int max_cmp = min(min(n->partial_len, MAX_PREFIX_LEN), (key_len * 2) - depth);
    int idx;
    for (idx=0; idx < max_cmp; idx++) {
        if (n->partial[idx] != get_partial(key, depth+idx))
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


item *assoc_find(const char *_key, const size_t nkey) {
	unsigned char *key = (unsigned char *)_key;
	//int _nkey = nkey;
	//printf("key(%d) : %s\n", _nkey, key);
    art_node **child;
    art_node *n = T->root;
    int prefix_len, depth = 0;

    while (n) {
		sum_depth++;
        // Might be a leaf
        if (IS_ITEM(n)) {	/* [kh] I changed "art_leaf" to "item" */
            n = (art_node*)ITEM_RAW(n);
            // Check if the expanded path matches
            if (!item_matches((item*)n, key, nkey, depth)) {
                return (item*)n;
            }
			//printf("there is no item\n");
            return NULL;
        }

		if (n->depth == depth) {
			// Bail if the prefix does not match
			if (n->partial_len) {
				prefix_len = check_prefix(n, key, nkey, depth);
				if (prefix_len != min(MAX_PREFIX_LEN, n->partial_len)) {
					return NULL;
				}
				depth = depth + n->partial_len;
			}
		} else {
			printf("Crash occured\n");
			exit(0);
		}

        // Recursively search
		
        child = find_child(n, get_partial(key, depth));
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
    int max_cmp = (min(it1->nkey, it2->nkey) * 2) - depth;
    int idx;
    for (idx=0; idx < max_cmp; idx++) {
        if (get_partial((unsigned char *)ITEM_key(it1), depth+idx) != 
				get_partial((unsigned char *)ITEM_key(it2), depth+idx))
            return idx;
    }
    return idx;	//[kh] check
}

static void add_child(art_node16 *n, art_node **ref, unsigned char c, void *child) {
	(void)ref;
	n->children[c] = (art_node*)child;
}

/**
 * Calculates the index at which the prefixes mismatch
 */
static int prefix_mismatch(const art_node *n, const unsigned char *key, int key_len, int depth, item **it) { //ok	//ok
    int max_cmp = min(min(MAX_PREFIX_LEN, n->partial_len), (key_len * 2) - depth);
    int idx;
    for (idx=0; idx < max_cmp; idx++) {
        if (n->partial[idx] != get_partial(key, depth+idx))
            return idx;
    }

    // If the prefix is short we can avoid finding a leaf
    if (n->partial_len > MAX_PREFIX_LEN) {
        // Prefix is longer than what we've checked, find a leaf
        *it = minimum(n);
        max_cmp = (min((*it)->nkey, key_len) * 2) - depth;
        for (; idx < max_cmp; idx++) {
            if (get_partial((unsigned char *)ITEM_key(*it),idx+depth) != 
					get_partial(key, depth+idx))
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
//		flush_buffer(*ref, key_len + 16, false);
		item_flush(*ref);
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
		art_node16 *new_node = (art_node16*)alloc_node();
		new_node->n.depth = depth;

		// Determine longest prefix
		int i, longest_prefix = longest_common_prefix(existing_it, inserted_it, depth);
		new_node->n.partial_len = longest_prefix;
		for (i = 0; i < min(MAX_PREFIX_LEN, longest_prefix); i++)
			new_node->n.partial[i] = get_partial(key, depth + i);
		// Add the leafs to the new node4
		*ref = (art_node*)new_node;
		add_child(new_node, ref, get_partial(key, depth + longest_prefix), SET_ITEM(inserted_it));
		add_child(new_node, ref, get_partial((unsigned char *)ITEM_key(existing_it), depth+longest_prefix), SET_ITEM(existing_it));

		flush_buffer(new_node, sizeof(art_node16), false);
	//	flush_buffer(inserted_it, key_len + 16, false);
		item_flush(inserted_it);
		flush_buffer(ref, 8, true);
		return NULL;
	}

	if (n->depth != depth) {
		printf("Insert: crash occured\n");
		exit(0);
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
		art_node16 *new_node = (art_node16*)alloc_node();
		new_node->n.depth = depth;
		*ref = (art_node*)new_node;
		new_node->n.partial_len = prefix_diff;
		memcpy(new_node->n.partial, n->partial, min(MAX_PREFIX_LEN, prefix_diff));

		// Adjust the prefix of the old node
		if (n->partial_len <= MAX_PREFIX_LEN) {
			art_node temp_path;
			add_child(new_node, ref, n->partial[prefix_diff], n);
			temp_path.partial_len = n->partial_len - (prefix_diff + 1);
			temp_path.depth = (depth + prefix_diff + 1);
			memcpy(temp_path.partial, n->partial + prefix_diff + 1,
					min(MAX_PREFIX_LEN, temp_path.partial_len));
			*((uint64_t *)n) = *((uint64_t *)&temp_path);
		} else {
			int i;
			art_node temp_path;
			if (min_it == NULL)
				min_it = minimum(n);
			add_child(new_node, ref, get_partial((unsigned char *)ITEM_key(min_it),depth + prefix_diff), n);
			temp_path.partial_len = n->partial_len - (prefix_diff + 1);
			for (i = 0; i < min(MAX_PREFIX_LEN, temp_path.partial_len); i++)
				temp_path.partial[i] = get_partial((unsigned char *)ITEM_key(min_it), depth + prefix_diff + 1 + i);
			temp_path.depth = (depth + prefix_diff + 1);
			memcpy(n, &temp_path, sizeof(art_node));
			*((uint64_t *)n) = *((uint64_t *)&temp_path);
		}

		// Insert the new leaf
		add_child(new_node, ref, get_partial(key, depth + prefix_diff), SET_ITEM(inserted_it));
		
		flush_buffer(new_node, sizeof(art_node16), false);
	//	flush_buffer(inserted_it, key_len + 16, false);
		item_flush(inserted_it);
		flush_buffer(n, sizeof(art_node), false);
		flush_buffer(ref, 8, true);
		return NULL;
	}

RECURSE_SEARCH:;

    // Find a child to recurse to
    art_node **child = find_child(n, get_partial(key, depth));
    if (child) {
        return recursive_insert(*child, child, inserted_it, depth+1);
    }

    // No child, node goes within us
    add_child((art_node16 *)n, ref, get_partial(key, depth), SET_ITEM(inserted_it));
	//flush_buffer(inserted_it, key_len + 16, false);
	item_flush(inserted_it);
	flush_buffer(&((art_node16 *)n)->children[get_partial(key, depth)], 8, true);
    return NULL;
}

int assoc_insert(item *it) {	//ok	//ok
	//const unsigned char *key = (unsigned char *)ITEM_key(it);
	//int key_len = it->nkey;
	//printf("key(%d) : %s\n", key_len, key);
	
	
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

static void remove_child(art_node16 *n, art_node **ref, unsigned char c, art_node **l) {	//ok	//ok
	n->children[c] = NULL;
}

static item* recursive_delete(art_node *n, art_node **ref, const unsigned char *key, int key_len, int depth) {	//ok
    // Search terminated
    if (!n) return NULL;

    // Handle hitting a leaf node
    if (IS_ITEM(n)) {
        item *it = ITEM_RAW(n);
        if (!item_matches(it, key, key_len, depth)) {
            *ref = NULL;
            return it;
        }
        return NULL;
    }

    // Bail if the prefix does not match
    if (n->partial_len) {
        int prefix_len = check_prefix(n, key, key_len, depth);
        if (prefix_len != min(MAX_PREFIX_LEN, n->partial_len)) {
            return NULL;
        }
        depth = depth + n->partial_len;
    }

    // Find child node
    art_node **child = find_child(n, get_partial(key, depth));
    if (!child) return NULL;

    // If the child is leaf, delete from this node
    if (IS_ITEM(*child)) {
        item *_it = ITEM_RAW(*child);
        if (!item_matches(_it, key, key_len, depth)) {
            remove_child((art_node16 *)n, ref, get_partial(key, depth), child);
            return _it;
        }
        return NULL;

    // Recurse
    } else {
        return recursive_delete(*child, child, key, key_len, depth+1);
    }
}

void assoc_delete(const char *key, const size_t nkey) {			//ok
    item *deleted_it = recursive_delete(T->root, &T->root, (unsigned char *)key, nkey, 0);
    if (deleted_it) {
       //  T->size--;
       // free(deleted_it);
        return;
    } else {
		printf("[assoc_delete] Maybe failed! There is no deleted item!!\n");
		exit(1);
	}
}
