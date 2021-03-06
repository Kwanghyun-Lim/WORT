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
static art_node* alloc_node(uint8_t type) { //ok	//ok
	art_node* n;
	switch (type) {
		case NODE4:
			//n = (art_node*)malloc(sizeof(art_node4));
			posix_memalign((void *)&n, 64, sizeof(art_node4));
			n->num_children = 0;
			n->partial_len = 0;
			node4_count++;
			break;
		case NODE16:
			//n = (art_node*)malloc(sizeof(art_node16));
			posix_memalign((void *)&n, 64, sizeof(art_node16));
			n->num_children = 0;
			n->partial_len = 0;
			node16_count++;
			break;
		case NODE48:
			//n = (art_node*)calloc(1, sizeof(art_node48));
			posix_memalign((void *)&n, 64, sizeof(art_node48));
			memset(n, 0, sizeof(art_node48));
			node48_count++;
			break;
		case NODE256:
			//n = (art_node*)calloc(1, sizeof(art_node256));
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
	printf("[art_tree_init]\n");
    t->root = NULL;
    t->size = 0;
	printf("?\n");
    return 0;
}

static art_node** find_child(art_node *n, unsigned char c) {	//ok	//ok
    int i, mask, bitfield;
    union {
        art_node4 *p1;
        art_node16 *p2;
        art_node48 *p3;
        art_node256 *p4;
    } p;
    switch (n->type) {
        case NODE4:
            p.p1 = (art_node4*)n;
            for (i=0;i < n->num_children; i++) {
                if (p.p1->keys[i] == c)
                    return &p.p1->children[i];
            }
            break;

        {
        __m128i cmp;
        case NODE16:
            p.p2 = (art_node16*)n;

            // Compare the key to all 16 stored keys
            cmp = _mm_cmpeq_epi8(_mm_set1_epi8(c),
                    _mm_loadu_si128((__m128i*)p.p2->keys));

            // Use a mask to ignore children that don't exist
            mask = (1 << n->num_children) - 1;
            bitfield = _mm_movemask_epi8(cmp) & mask;

            /*
             * If we have a match (any bit set) then we can
             * return the pointer match using ctz to get
             * the index.
             */
            if (bitfield)
                return &p.p2->children[__builtin_ctz(bitfield)];
            break;
        }

        case NODE48:
            p.p3 = (art_node48*)n;
            i = p.p3->keys[c];
            if (i)
                return &p.p3->children[i-1];
            break;

        case NODE256:
            p.p4 = (art_node256*)n;
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

    int idx;
    switch (n->type) {
        case NODE4:
             return minimum(((art_node4*)n)->children[0]);
        case NODE16:
             return minimum(((art_node16*)n)->children[0]);
        case NODE48:
            idx=0;
            while (!((art_node48*)n)->keys[idx]) idx++;
            idx = ((art_node48*)n)->keys[idx] - 1;
            return minimum(((art_node48*)n)->children[idx]);
        case NODE256:
            idx=0;
            while (!((art_node256*)n)->children[idx]) idx++;
            return minimum(((art_node256*)n)->children[idx]);
        default:
            abort();
    }
}


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


/**
 * Returns the minimum valued leaf
 */
item* art_minimum(art_tree *t) {		//ok	//ok
    return minimum((art_node*)t->root);
}

/**
 * Returns the maximum valued leaf
 */
item* art_maximum(art_tree *t) {		//ok	//ok
    return maximum((art_node*)t->root);
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
    return idx;	//[kh] check
}

static void copy_header(art_node *dest, art_node *src) {		//ok	//ok
    dest->num_children = src->num_children;
	dest->partial_len = src->partial_len;
	memcpy(dest->partial, src->partial, min(MAX_PREFIX_LEN, src->partial_len));
}

static void add_child256(art_node256 *n, art_node **ref, unsigned char c, void *child) {	//ok	//ok
	(void)ref;
    n->n.num_children++;
    n->children[c] = (art_node*)child;
    flush_buffer(&n->children[c], 8, true);
}

static void add_child48(art_node48 *n, art_node **ref, unsigned char c, void *child) {		//
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
	if (n->n.num_children < 16) {
		__m128i cmp;

		// Compare the key to all 16 stored keys
		cmp = _mm_cmplt_epi8(_mm_set1_epi8(c),
				_mm_loadu_si128((__m128i*)n->keys));

		// Use a mask to ignore children that don't exist
		unsigned mask = (1 << n->n.num_children) - 1;
		unsigned bitfield = _mm_movemask_epi8(cmp) & mask;

		// Check if less than any
		unsigned idx;
		if (bitfield) {
			art_node16 *copy_node = (art_node16 *)alloc_node(NODE16);
			memcpy(copy_node, n, sizeof(art_node16));

			idx = __builtin_ctz(bitfield);
			memmove(copy_node->keys+idx+1,copy_node->keys+idx,copy_node->n.num_children-idx);
			memmove(copy_node->children+idx+1,copy_node->children+idx,
					(copy_node->n.num_children-idx)*sizeof(void*));

			copy_node->keys[idx] = c;
			copy_node->children[idx] = (art_node *)child;
			copy_node->n.num_children++;

			*ref = (art_node *)copy_node;
			flush_buffer(copy_node, sizeof(art_node16), false);
			flush_buffer(ref, 8, true);

			node16_count--;
			free(n);
		} else {
			idx = n->n.num_children;
			n->keys[idx] = c;
			n->children[idx] = (art_node*)child;
			n->n.num_children++;
			
			flush_buffer(&n->keys[idx], sizeof(unsigned char), false);
			flush_buffer(&n->children[idx], 8, false);
			flush_buffer(&n->n.num_children, sizeof(uint8_t), true);
		}
	} else { //from here
		int i, pos;
		art_node48 *new_node = (art_node48*)alloc_node(NODE48);

		// Copy the child pointers and populate the key map
		memcpy(new_node->children, n->children,
				sizeof(void*)*n->n.num_children);
		for (i=0;i<n->n.num_children;i++) {
			new_node->keys[n->keys[i]] = i + 1;
		}
		copy_header((art_node*)new_node, (art_node*)n);

		pos = 0;
		while (new_node->children[pos]) pos++;
		new_node->children[pos] = (art_node*)child;
		new_node->keys[c] = pos + 1;
		new_node->n.num_children++;

		*ref = (art_node *)new_node;
		flush_buffer(new_node, sizeof(art_node48), false);
		flush_buffer(ref, 8, true);

		node16_count--;
		free(n);
	}
}


static void add_child4_noflush(art_node4 *n, art_node **ref, unsigned char c, void *child) {
	int idx;
	for (idx=0; idx < n->n.num_children; idx++) {
		if (c < n->keys[idx]) break;
	}

	// Shift to make room
	memmove(n->keys+idx+1, n->keys+idx, n->n.num_children - idx);
	memmove(n->children+idx+1, n->children+idx,
			(n->n.num_children - idx)*sizeof(void*));

	// Insert element
	n->keys[idx] = c;
	n->children[idx] = (art_node*)child;
	n->n.num_children++;
}

static void add_child4(art_node4 *n, art_node **ref, unsigned char c, void *child) {	//ok	//ok
    if (n->n.num_children < 4) {
		int idx;
		for (idx=0; idx < n->n.num_children; idx++) {
			if (c < n->keys[idx]) break;
		}

//		if (idx != n->n.num_children) {
		art_node4 *copy_node = (art_node4 *)alloc_node(NODE4);
		memcpy(copy_node, n, sizeof(art_node4));
		// Shift to make room
		memmove(copy_node->keys+idx+1, copy_node->keys+idx, copy_node->n.num_children - idx);
		memmove(copy_node->children+idx+1, copy_node->children+idx,
				(copy_node->n.num_children - idx)*sizeof(void*));

		// Insert element
		copy_node->keys[idx] = c;
		copy_node->children[idx] = (art_node*)child;
		copy_node->n.num_children++;

		*ref = (art_node *)copy_node;
		flush_buffer(copy_node, sizeof(art_node4), false);
		flush_buffer(ref, 8, true);

		node4_count--;
		free(n);
//		} else {
//			n->keys[idx] = c;
//			n->children[idx] = (art_node*)child;
//			n->n.num_children++;
//
//			flush_buffer(&n->keys[idx], sizeof(unsigned char), false);
//			flush_buffer(&n->children[idx], 8, false);
//			flush_buffer(&n->n.num_children, sizeof(uint8_t), true);
//		}
	} else {
		__m128i cmp;
		art_node16 *new_node = (art_node16*)alloc_node(NODE16);

		// Copy the child pointers and the key map
		memcpy(new_node->children, n->children,
				sizeof(void*)*n->n.num_children);
		memcpy(new_node->keys, n->keys,
				sizeof(unsigned char)*n->n.num_children);
		copy_header((art_node*)new_node, (art_node*)n);

		// Compare the key to all 16 stored keys
		cmp = _mm_cmplt_epi8(_mm_set1_epi8(c),
				_mm_loadu_si128((__m128i*)new_node->keys));

		// Use a mask to ignore children that don't exist
		unsigned mask = (1 << new_node->n.num_children) - 1;
		unsigned bitfield = _mm_movemask_epi8(cmp) & mask;

		// Check if less than any
		unsigned idx;
		if (bitfield) {
			idx = __builtin_ctz(bitfield);
			memmove(new_node->keys+idx+1,new_node->keys+idx,new_node->n.num_children-idx);
			memmove(new_node->children+idx+1,new_node->children+idx,
					(new_node->n.num_children-idx)*sizeof(void*));
		} else
			idx = new_node->n.num_children;

		// Set the child
		new_node->keys[idx] = c;
		new_node->children[idx] = (art_node*)child;
		new_node->n.num_children++;

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

		// Determine longest prefix
		int longest_prefix = longest_common_prefix(existing_it, inserted_it, depth);
		new_node->n.partial_len = longest_prefix;
		memcpy(new_node->n.partial, key+depth, min(MAX_PREFIX_LEN, longest_prefix));
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
		art_node4 *new_node = (art_node4*)alloc_node(NODE4);

		art_node *copy_node;
		switch (n->type) {
			case NODE4:
				posix_memalign((void *)&copy_node, 64, sizeof(art_node4));
				memcpy(copy_node, n, sizeof(art_node4));
				break;
			case NODE16:
				posix_memalign((void *)&copy_node, 64, sizeof(art_node16));
				memcpy(copy_node, n, sizeof(art_node16));
				break;
			case NODE48:
				posix_memalign((void *)&copy_node, 64, sizeof(art_node48));
				memcpy(copy_node, n, sizeof(art_node48));
				break;
			case NODE256:
				posix_memalign((void *)&copy_node, 64, sizeof(art_node256));
				memcpy(copy_node, n, sizeof(art_node256));
				break;
			default:
				abort();
		}

		*ref = (art_node*)new_node;
		new_node->n.partial_len = prefix_diff;
		memcpy(new_node->n.partial, copy_node->partial, min(MAX_PREFIX_LEN, prefix_diff));

		// Adjust the prefix of the old node
		if (copy_node->partial_len <= MAX_PREFIX_LEN) {
			add_child4_noflush(new_node, ref, n->partial[prefix_diff], copy_node);
			copy_node->partial_len -= (prefix_diff + 1);
			memmove(copy_node->partial, copy_node->partial + prefix_diff + 1,
					min(MAX_PREFIX_LEN, copy_node->partial_len));
		} else {
			if (min_it == NULL)
				min_it = minimum(n);
			copy_node->partial_len -= (prefix_diff + 1);
			add_child4_noflush(new_node, ref, ITEM_key(min_it)[depth + prefix_diff], copy_node);
			memcpy(copy_node->partial, ITEM_key(min_it)+depth+prefix_diff+1,
					min(MAX_PREFIX_LEN, copy_node->partial_len));
		}

		// Insert the new leaf
		add_child4_noflush(new_node, ref, key[depth + prefix_diff], SET_ITEM(inserted_it));
		
		flush_buffer(new_node, sizeof(art_node4), false);
	//	flush_buffer(inserted_it, key_len + 16, false);
		item_flush(inserted_it);
		switch (copy_node->type) {
			case NODE4:
				flush_buffer(copy_node, sizeof(art_node4), false);
				break;
			case NODE16:
				flush_buffer(copy_node, sizeof(art_node16), false);
				break;
			case NODE48:
				flush_buffer(copy_node, sizeof(art_node48), false);
				break;
			case NODE256:
				flush_buffer(copy_node, sizeof(art_node256), false);
				break;
			default:
				abort();
		}
		flush_buffer(ref, 8, true);

		free(n);
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
    n->n.num_children--;
}

static void remove_child48(art_node48 *n, art_node **ref, unsigned char c) {	//ok	//ok
    int pos = n->keys[c];
    n->keys[c] = 0;
    n->children[pos-1] = NULL;
    n->n.num_children--;
}

static void remove_child16(art_node16 *n, art_node **ref, art_node **l) {		//ok	//ok
    int pos = l - n->children;
    memmove(n->keys+pos, n->keys+pos+1, n->n.num_children - 1 - pos);
    memmove(n->children+pos, n->children+pos+1, (n->n.num_children - 1 - pos)*sizeof(void*));
    n->n.num_children--;
}

static void remove_child4(art_node4 *n, art_node **ref, art_node **l) {	//ok	//ok
    int pos = l - n->children;
    memmove(n->keys+pos, n->keys+pos+1, n->n.num_children - 1 - pos);
    memmove(n->children+pos, n->children+pos+1, (n->n.num_children - 1 - pos)*sizeof(void*));
    n->n.num_children--;
}

static void remove_child(art_node *n, art_node **ref, unsigned char c, art_node **l) {	//ok	//ok
    switch (n->type) {
        case NODE4:
             remove_child4((art_node4*)n, ref, l);
			 return;
        case NODE16:
             remove_child16((art_node16*)n, ref, l);
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
    if (n->partial_len) {
        int prefix_len = check_prefix(n, key, key_len, depth);
        if (prefix_len != min(MAX_PREFIX_LEN, n->partial_len)) {
            return NULL;
        }
        depth = depth + n->partial_len;
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
