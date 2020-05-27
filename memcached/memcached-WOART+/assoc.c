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

unsigned long node4_count = 0;
unsigned long node16_count = 0;
unsigned long node48_count = 0;
unsigned long node256_count = 0;
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

static void item_flush(void *buf)
{
	asm volatile ("clflush %0\n" : "+m" (*(char *)((unsigned long)buf)));
	clflush_count++;
}

/*
 * Find the next set bit in a memory region.
 */
static unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
			    unsigned long offset)
{
	const unsigned long *p = addr + BITOP_WORD(offset);
	unsigned long result = offset & ~(BITS_PER_LONG-1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset %= BITS_PER_LONG;
	if (offset) {
		tmp = *(p++);
		tmp &= (~0UL << offset);
		if (size < BITS_PER_LONG)
			goto found_first;
		if (tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	while (size & ~(BITS_PER_LONG-1)) {
		if ((tmp = *(p++)))
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp &= (~0UL >> (BITS_PER_LONG - size));
	if (tmp == 0UL)		/* Are any bits set? */
		return result + size;	/* Nope. */
found_middle:
	return result + __ffs(tmp);
}

/*
 * Find the next zero bit in a memory region
 */
static unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
		unsigned long offset)
{
	const unsigned long *p = addr + BITOP_WORD(offset);
	unsigned long result = offset & ~(BITS_PER_LONG - 1);
	unsigned long tmp;

	if (offset >= size)
		return size;
	size -= result;
	offset %= BITS_PER_LONG;
	if (offset) {
		tmp = *(p++);
		tmp |= ~0UL >> (BITS_PER_LONG - offset);
		if (size < BITS_PER_LONG)
			goto found_first;
		if (~tmp)
			goto found_middle;
		size -= BITS_PER_LONG;
		result += BITS_PER_LONG;
	}
	while (size & ~(BITS_PER_LONG - 1)) {
		if (~(tmp = *(p++)))
			goto found_middle;
		result += BITS_PER_LONG;
		size -= BITS_PER_LONG;
	}
	if (!size)
		return result;
	tmp = *p;

found_first:
	tmp |= ~0UL << size;
	if (tmp == ~0UL)
		return result + size;
found_middle:
	return result + ffz(tmp);
}

/**
 * Allocates a node of the given type,
 * initializes to zero and sets the type.
 */
static art_node* alloc_node(uint8_t type) {
	art_node* n;
	int i;
	switch (type) {
		case NODE4:
			//n = (art_node *)malloc(sizeof(art_node4));
			posix_memalign((void *)&n, 64, sizeof(art_node4));
			for (i = 0; i < 4; i++)
				((art_node4 *)n)->slot[i].i_ptr = -1;
			node4_count++;
			break;
		case NODE16:
			//n = (art_node *)malloc(sizeof(art_node16));
			posix_memalign((void *)&n, 64, sizeof(art_node16));
			((art_node16 *)n)->bitmap = 0;
			node16_count++;
			break;
		case NODE48:
			//n = (art_node *)calloc(1, sizeof(art_node48));
			posix_memalign((void *)&n, 64, sizeof(art_node48));
			memset(n, 0, sizeof(art_node48));
			node48_count++;
			break;
		case NODE256:
			//n = (art_node *)calloc(1, sizeof(art_node256));
			posix_memalign((void *)&n, 64, sizeof(art_node256));
			memset(n, 0, sizeof(art_node256));
			node256_count++;
			break;
		default:
			abort();
	}
	n->type = type;
	return n;
}

/**
 * Initializes an ART tree
 * @return 0 on success.
 */

static art_tree *T;

int art_tree_init(art_tree *t) {		//ok	//ok
	printf("[art_tree_init]500w300r\n");
    t->root = NULL;
    t->size = 0;
	printf("?\n");
    return 0;
}

static art_node** find_child(art_node *n, unsigned char c) {	//ok	//ok
	int i;
	union {
		art_node4 *p1;
		art_node16 *p2;
		art_node48 *p3;
		art_node256 *p4;
	} p;
	switch (n->type) {
		case NODE4:
			p.p1 = (art_node4 *)n;
			for (i = 0; (i < 4 && (p.p1->slot[i].i_ptr != -1)); i++) {
				if (p.p1->slot[i].key == c)
					return &p.p1->children[(int)p.p1->slot[i].i_ptr];
			}
			break;
		case NODE16:
			p.p2 = (art_node16 *)n;
			for (i = 0; i < 16; i++) {
				i = find_next_bit(&p.p2->bitmap, 16, i);
				if (i < 16 && p.p2->keys[i] == c)
					return &p.p2->children[i];
			}
			break;
		case NODE48:
			p.p3 = (art_node48 *)n;
			i = p.p3->keys[c];
			if (i)
				return &p.p3->children[i - 1];
			break;
		case NODE256:
			p.p4 = (art_node256 *)n;
			if (p.p4->children[c])
				return &p.p4->children[c];
			break;
		default:
			abort();
	}
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
    int max_cmp = min(min(n->path.partial_len, MAX_PREFIX_LEN), key_len - depth);
    int idx;
    for (idx=0; idx < max_cmp; idx++) {
        if (n->path.partial[idx] != key[depth+idx])
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
   printf("[assoc_init]500w300r\n");
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
		sum_depth++;
        // Might be a leaf
        if (IS_ITEM(n)) {	/* [kh] I changed "art_leaf" to "item" */
            n = (art_node*)ITEM_RAW(n);
            // Check if the expanded path matches
            if (!item_matches((item*)n, key, nkey, depth)) {
                return (item*)n;
            }
            return NULL;
        }

		if (n->path.depth == depth) {
			// Bail if the prefix does not match
			if (n->path.partial_len) {
				prefix_len = check_prefix(n, key, nkey, depth);
				if (prefix_len != min(MAX_PREFIX_LEN, n->path.partial_len))
					return NULL;
				depth = depth + n->path.partial_len;
			}
		} else {
			printf("Search: crash occured\n");
			exit(0);
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

	int i, idx, min;
	switch (n->type) {
		case NODE4:
			return minimum(((art_node4 *)n)->children[(int)((art_node4 *)n)->slot[0].i_ptr]);
		case NODE16:
			i = find_next_bit(&((art_node16 *)n)->bitmap, 16, 0);
			min = ((art_node16 *)n)->keys[i];
			idx = i;
			for (i = i + 1; i < 16; i++) {
				i = find_next_bit(&((art_node16 *)n)->bitmap, 16, i);
				if(((art_node16 *)n)->keys[i] < min && i < 16) {
					min = ((art_node16 *)n)->keys[i];
					idx = i;
				}
			}
			return minimum(((art_node16 *)n)->children[idx]);
		case NODE48:
			idx = 0;
			while (!((art_node48*)n)->keys[idx]) idx++;
			idx = ((art_node48*)n)->keys[idx] - 1;
			return minimum(((art_node48 *)n)->children[idx]);
		case NODE256:
			idx = 0;
			while (!((art_node256 *)n)->children[idx]) idx++;
			return minimum(((art_node256 *)n)->children[idx]);
		default:
			abort();
	}
}

/*
// Find the maximum leaf under a node
static item* maximum(const art_node *n) {		//ok	//ok
    // Handle base cases
    if (!n) return NULL;
    if (IS_ITEM(n)) return ITEM_RAW(n);

    int idx;
    switch (n->type) {
        case NODE4:
            return maximum(((art_node4*)n)->children[n->num_children-1]);
        case NODE16:
            return maximum(((art_node16*)n)->children[n->num_children-1]);
        case NODE48:
            idx=255;
            while (!((art_node48*)n)->keys[idx]) idx--;
            idx = ((art_node48*)n)->keys[idx] - 1;
            return maximum(((art_node48*)n)->children[idx]);
        case NODE256:
            idx=255;
            while (!((art_node256*)n)->children[idx]) idx--;
            return maximum(((art_node256*)n)->children[idx]);
        default:
            abort();
    }
}
*/


/**
 * Returns the minimum valued leaf
 */
item* art_minimum(art_tree *t) {		//ok	//ok
    return minimum((art_node*)t->root);
}

/**
 * Returns the maximum valued leaf
 */
/*
item* art_maximum(art_tree *t) {		//ok	//ok
    return maximum((art_node*)t->root);
}
*/
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
    return idx;	//[kh] check
}

static void copy_header(art_node *dest, art_node *src) {		//ok	//ok
	memcpy(&dest->path, &src->path, sizeof(path_comp));
}

static void add_child256(art_node256 *n, art_node **ref, unsigned char c, void *child) {	//ok	//ok
	(void)ref;
    n->children[c] = (art_node*)child;
    flush_buffer(&n->children[c], 8, true);
}

static void add_child48(art_node48 *n, art_node **ref, unsigned char c, void *child) {		//ok	//ok
	unsigned long bitmap = 0;
	int i, num = 0;

	for (i = 0; i < 256; i++){
		if (n->keys[i]) {
			bitmap += (0x1UL << (n->keys[i] - 1));
			num++;
			if (num == 48)
				break;
		}
	}

	if (num < 48) {
		unsigned long pos = find_next_zero_bit(&bitmap, 48, 0);
		n->children[pos] = (art_node *)child;
		n->keys[c] = pos + 1;
		flush_buffer(&n->children[pos], 8, false);
		flush_buffer(&n->keys[c], sizeof(unsigned char), true);
	} else {
		int i;
		art_node256 *new_node = (art_node256*)alloc_node(NODE256);
		for (i=0;i<256;i++) {
			if (n->keys[i]) {
				new_node->children[i] = n->children[n->keys[i] - 1];
				num--;
				if (num == 0)
					break;
			}
		}
		copy_header((art_node*)new_node, (art_node*)n);
		
		new_node->children[c] = (art_node*)child;
		*ref = (art_node*)new_node;
		flush_buffer(new_node, sizeof(art_node256), false);
		flush_buffer(ref, 8, true);

		node48_count--;
		free(n);
	} 
}

static void add_child16(art_node16 *n, art_node **ref, unsigned char c, void *child) {	//ok	//ok
	if (n->bitmap != ((0x1UL << 16) - 1)) {
		unsigned long empty_idx;

		empty_idx = find_next_zero_bit(&n->bitmap, 16, 0);
		if (empty_idx == 16) {
			printf("find next zero bit error add_child16\n");
			abort();
		}

		n->keys[empty_idx] = c;
		n->children[empty_idx] = child;

		n->bitmap += (0x1UL << empty_idx);

		flush_buffer(&n->keys[empty_idx], sizeof(unsigned char), false);
		flush_buffer(&n->children[empty_idx], 8, false);
		flush_buffer(&n->bitmap, sizeof(unsigned long), true);
	} else {
		int idx;
		art_node48 *new_node = (art_node48 *)alloc_node(NODE48);

		for (idx = 0; idx < 16; idx++) {
			new_node->keys[n->keys[idx]] = idx + 1;
			new_node->children[idx] = n->children[idx];
		}
		copy_header((art_node *)new_node, (art_node *)n);
		*ref = (art_node *)new_node;

		new_node->keys[c] = 17;
		new_node->children[16] = child;

		flush_buffer(new_node, sizeof(art_node48), false);
		flush_buffer(ref, 8, true);

		node16_count--;
		free(n);
	}
}


static void add_child4_noflush(art_node4 *n, art_node **ref, unsigned char c, void *child) {
	slot_array temp_slot[4];
	int i, idx, mid = -1;
	unsigned long p_idx = 0;

	for (idx = 0; (idx < 4 && (n->slot[idx].i_ptr != -1)); idx++) {
		p_idx = p_idx + (0x1UL << n->slot[idx].i_ptr);
		if (mid == -1 && c < n->slot[idx].key)
			mid = idx;
	}

	if (mid == -1)
		mid = idx;

	p_idx = find_next_zero_bit(&p_idx, 4, 0);
	if (p_idx == 4) {
		printf("find next zero bit error in child4\n");
		abort();
	}

	n->children[p_idx] = child;

	for (i = idx - 1; i >= mid; i--) {
		temp_slot[i + 1].key = n->slot[i].key;
		temp_slot[i + 1].i_ptr = n->slot[i].i_ptr;
	}

	if (idx < 3) {
		for (i = idx + 1; i < 4; i++)
			temp_slot[i].i_ptr = -1;
	}

	temp_slot[mid].key = c;
	temp_slot[mid].i_ptr = p_idx;

	for (i = mid - 1; i >=0; i--) {
		temp_slot[i].key = n->slot[i].key;
		temp_slot[i].i_ptr = n->slot[i].i_ptr;
	}

	*((uint64_t *)n->slot) = *((uint64_t *)temp_slot);
}

static void add_child4(art_node4 *n, art_node **ref, unsigned char c, void *child) {	//ok	//ok
	if (n->slot[3].i_ptr == -1) {
		slot_array temp_slot[4];
		int i, idx, mid = -1;
		unsigned long p_idx = 0;

		for (idx = 0; (idx < 4 && (n->slot[idx].i_ptr != -1)); idx++) {
			p_idx = p_idx + (0x1UL << n->slot[idx].i_ptr);
			if (mid == -1 && c < n->slot[idx].key)
				mid = idx;
		}

		if (mid == -1)
			mid = idx;

		p_idx = find_next_zero_bit(&p_idx, 4, 0);
		if (p_idx == 4) {
			printf("find next zero bit error in child4\n");
			abort();
		}
		n->children[p_idx] = child;

		for (i = idx - 1; i >= mid; i--) {
			temp_slot[i + 1].key = n->slot[i].key;
			temp_slot[i + 1].i_ptr = n->slot[i].i_ptr;
		}

		if (idx < 3) {
			for (i = idx + 1; i < 4; i++)
				temp_slot[i].i_ptr = -1;
		}

		temp_slot[mid].key = c;
		temp_slot[mid].i_ptr = p_idx;

		for (i = mid - 1; i >=0; i--) {
			temp_slot[i].key = n->slot[i].key;
			temp_slot[i].i_ptr = n->slot[i].i_ptr;
		}

		*((uint64_t *)n->slot) = *((uint64_t *)temp_slot);

		flush_buffer(&n->children[p_idx], 8, false);
		flush_buffer(n->slot, 8, true);
	} else {
		int idx;
		art_node16 *new_node = (art_node16 *)alloc_node(NODE16);

		for (idx = 0; idx < 4; idx++) {
			new_node->keys[(int)n->slot[idx].i_ptr] = n->slot[idx].key;
			new_node->children[(int)n->slot[idx].i_ptr] = n->children[(int)n->slot[idx].i_ptr];
			new_node->bitmap += (0x1UL << n->slot[idx].i_ptr);
		}
		copy_header((art_node *)new_node, (art_node *)n);

		new_node->keys[4] = c;
		new_node->children[4] = child;
		new_node->bitmap += (0x1UL << 4);

		*ref = (art_node *)new_node;
		flush_buffer(new_node, sizeof(art_node16), false);
		flush_buffer(ref, 8, true);

		node4_count--;
		free(n);
	}
}

static void add_child(art_node *n, art_node **ref, unsigned char c, void *child) {	//ok	//ok
    switch (n->type) {
        case NODE4:
             add_child4((art_node4*)n, ref, c, child);
			 return;
        case NODE16:
             add_child16((art_node16*)n, ref, c, child);
			 return;
        case NODE48:
             add_child48((art_node48*)n, ref, c, child);
			 return;
        case NODE256:
             add_child256((art_node256*)n, ref, c, child);
			 return;
        default:
            abort();
    }
}

/**
 * Calculates the index at which the prefixes mismatch
 */
static int prefix_mismatch(const art_node *n, const unsigned char *key, int key_len, int depth, item **it) { //ok	//ok
    int max_cmp = min(min(MAX_PREFIX_LEN, n->path.partial_len), key_len - depth);
    int idx;
    for (idx=0; idx < max_cmp; idx++) {
        if (n->path.partial[idx] != key[depth+idx])
            return idx;
    }

    // If the prefix is short we can avoid finding a leaf
    if (n->path.partial_len > MAX_PREFIX_LEN) {
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
		//flush_buffer(*ref, key_len + 16, false);
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
		art_node4 *new_node = (art_node4*)alloc_node(NODE4);
		new_node->n.path.depth = depth;

		// Determine longest prefix
		int longest_prefix = longest_common_prefix(existing_it, inserted_it, depth);
		new_node->n.path.partial_len = longest_prefix;
		memcpy(new_node->n.path.partial, key+depth, min(MAX_PREFIX_LEN, longest_prefix));
		// Add the leafs to the new node4
		*ref = (art_node*)new_node;
		add_child4_noflush(new_node, ref, key[depth+longest_prefix], SET_ITEM(inserted_it));
		add_child4_noflush(new_node, ref, ITEM_key(existing_it)[depth+longest_prefix], SET_ITEM(existing_it));

		flush_buffer(new_node, sizeof(art_node4), false);
		//flush_buffer(inserted_it, key_len + 16, false);
		item_flush(inserted_it);
		flush_buffer(ref, 8, true);
		return NULL;
	}

	if (n->path.depth != depth) {
		printf("Insert: system is previously crashed!!\n");
		exit(0);
	}

	// Check if given node has a prefix
	if (n->path.partial_len) {
		item *min_it = NULL;
		// Determine if the prefixes differ, since we need to split
		int prefix_diff = prefix_mismatch(n, key, key_len, depth, &min_it);
		if ((uint32_t)prefix_diff >= n->path.partial_len) {
			depth += n->path.partial_len;
			goto RECURSE_SEARCH;
		}

		path_comp_count++;

		// Create a new node
		art_node4 *new_node = (art_node4*)alloc_node(NODE4);
		new_node->n.path.depth = depth;
		*ref = (art_node*)new_node;
		new_node->n.path.partial_len = prefix_diff;
		memcpy(new_node->n.path.partial, n->path.partial, min(MAX_PREFIX_LEN, prefix_diff));

		// Adjust the prefix of the old node
		if (n->path.partial_len <= MAX_PREFIX_LEN) {
			path_comp temp_path;
			if (min_it == NULL)
				min_it = minimum(n);
			add_child4_noflush(new_node, ref, n->path.partial[prefix_diff], n);
			temp_path.partial_len = n->path.partial_len - (prefix_diff + 1);
			temp_path.depth = (depth + prefix_diff + 1);
			memcpy(temp_path.partial, n->path.partial + prefix_diff + 1,
					min(MAX_PREFIX_LEN, temp_path.partial_len));
			*((uint64_t *)&n->path) = *((uint64_t *)&temp_path);
		} else {
			path_comp temp_path;
			add_child4_noflush(new_node, ref, ITEM_key(min_it)[depth + prefix_diff], n);
			temp_path.partial_len = n->path.partial_len - (prefix_diff + 1);
			memcpy(temp_path.partial, ITEM_key(min_it)+depth+prefix_diff+1,
					min(MAX_PREFIX_LEN, temp_path.partial_len));
			temp_path.depth = (depth + prefix_diff + 1);
			*((uint64_t *)&n->path) = *((uint64_t *)&temp_path);
		}

		// Insert the new leaf
		add_child4_noflush(new_node, ref, key[depth + prefix_diff], SET_ITEM(inserted_it));
		
		flush_buffer(new_node, sizeof(art_node4), false);
	//	flush_buffer(inserted_it, key_len + 16, false);
		item_flush(inserted_it);
		flush_buffer(&n->path, sizeof(path_comp), false);
		flush_buffer(ref, 8, true);
		return NULL;
	}

RECURSE_SEARCH:;

    // Find a child to recurse to
    art_node **child = find_child(n, key[depth]);
    if (child) {
        return recursive_insert(*child, child, inserted_it, depth+1);
    }

	//flush_buffer(inserted_it, key_len + 16, false);
	item_flush(inserted_it);
    // No child, node goes within us
    add_child(n, ref, key[depth], SET_ITEM(inserted_it));
    return NULL;
}

int assoc_insert(item *it) {	//ok	//ok
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

static void remove_child256(art_node256 *n, art_node **ref, unsigned char c) {		//ok	//ok
    n->children[c] = NULL;
}

static void remove_child48(art_node48 *n, art_node **ref, unsigned char c) {	//ok	//ok
    int pos = n->keys[c];
    n->keys[c] = 0;
    n->children[pos-1] = NULL;
}

static void remove_child16(art_node16 *n, art_node **ref, unsigned char c) {		//ok	//ok
    unsigned long pos;

	for (pos = 0; pos < 16; pos++) {
		pos = find_next_bit(&n->bitmap, 16, pos);
		if (pos < 16 && n->keys[pos] == c) {
			n->bitmap -= (0x1U << pos);
		}
	}
}

static void remove_child4(art_node4 *n, art_node **ref, unsigned char c) {	//ok	//ok
    int pos, mid = -1, i;

	for (pos = 0; ((n->slot[pos].i_ptr != -1) && (pos < 4)); pos++) {
		if (n->slot[pos].key == c) {
			mid = pos;
		}
	}

	if (mid == -1)
		return ;

	for (i = mid; i < pos - 1; i++) {
		n->slot[i].key = n->slot[i + 1].key;
		n->slot[i].i_ptr = n->slot[i + 1].i_ptr;
	}

	for (i = pos - 1; i < 4; i++)
		n->slot[i].i_ptr = -1;
}

static void remove_child(art_node *n, art_node **ref, unsigned char c, art_node **l) {	//ok	//ok
    switch (n->type) {
        case NODE4:
             remove_child4((art_node4*)n, ref, c);
			 return;
        case NODE16:
             remove_child16((art_node16*)n, ref, c);
			 return;
        case NODE48:
             remove_child48((art_node48*)n, ref, c);
			 return;
        case NODE256:
             remove_child256((art_node256*)n, ref, c);
			 return;
        default:
            abort();
    }
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
    if (n->path.partial_len) {
        int prefix_len = check_prefix(n, key, key_len, depth);
        if (prefix_len != min(MAX_PREFIX_LEN, n->path.partial_len)) {
            return NULL;
        }
        depth = depth + n->path.partial_len;
    }

    // Find child node
    art_node **child = find_child(n, key[depth]);
    if (!child) return NULL;

    // If the child is leaf, delete from this node
    if (IS_ITEM(*child)) {
        item *_it = ITEM_RAW(*child);
        if (!item_matches(_it, key, key_len, depth)) {
            remove_child(n, ref, key[depth], child);
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
