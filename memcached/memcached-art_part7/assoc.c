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


unsigned long node4_count = 0;
unsigned long node16_count = 0;
unsigned long node128_count = 0;
unsigned long leaf_count = 0;
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
#define BITOP_WORD(nr)	((nr) / BITS_PER_LONG)

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


/*
 * Find the next set bit in a memory region.
 */
unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
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
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
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
static art_node* alloc_node(uint8_t type) {	//[kh] changed 
	art_node* n;
	int i;
	switch (type) {
		case NODE4:
			n = (art_node *)malloc(sizeof(art_node4));
			for (i = 0; i < 4; i++)
				((art_node4 *)n)->slot[i].key = -1;	//[kh] ?
			node4_count++;
			break;
		case NODE16:
			n = (art_node *)malloc(sizeof(art_node16));
			memset(((art_node16 *)n)->bitmap, 0, sizeof(((art_node16 *)n)->bitmap));
			node16_count++;
			break;
//		case NODE32:
//			n = (art_node *)calloc(1, sizeof(art_node32));
//			break;
		case NODE128:
			n = (art_node *)calloc(1, sizeof(art_node128));
			node128_count++;
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
	printf("[PART7_init]\n");
    t->root = NULL;
    t->size = 0;
	printf("[PART7_init]!\n");
    return 0;
}



// Simple inlined if
static inline int min(int a, int b) {		//ok	//ok
    return (a < b) ? a : b;
}



static art_node** find_child(art_node *n, unsigned char c) {	//[kh] changed
	int i;
	union {
		art_node4 *p1;
		art_node16 *p2;
		art_node128 *p3;
	} p;
	switch (n->type) {
		case NODE4:
			p.p1 = (art_node4 *)n;
			for (i = 0; ((p.p1->slot[i].key != -1) && i < 4); i++) { //[kh] ?
				if (p.p1->slot[i].key == c)
					return &p.p1->children[(int)p.p1->slot[i].i_ptr];
			}
			break;
		case NODE16:
			p.p2 = (art_node16 *)n;
			if(p.p2->bitmap[c / 32][0] & (0x1U << (c % 32)))
				return &p.p2->children[p.p2->keys[c]];
			break;
		case NODE128:
			p.p3 = (art_node128 *)n;
			if (p.p3->children[c])
				return &p.p3->children[c];
			break;
		default:
			abort();
	}
	return NULL;
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
   	printf("[assoc_init]\n");
   	T = (art_tree *)malloc(sizeof(art_tree));
	art_tree_init(T);
	printf("[assoc_init]\n");
	printf("PART7\n");
}


item *assoc_find(const char *_key, const size_t nkey) { //ok	//ok
	printf("[assoc_find]\n");
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
static item* minimum(const art_node *n) {
	// Handle base cases
	if (!n) return NULL;
	if (IS_ITEM(n)) return ITEM_RAW(n);

	int i, j, idx;
	switch (n->type) {
		case NODE4:
			return minimum(((art_node4 *)n)->children[(int)((art_node4 *)n)->slot[0].i_ptr]);
		case NODE16:
			for (i = 0; i < 4; i++) {
				for (j = 0; j < 32; j++) { 
					j = find_next_bit((unsigned long *)&((art_node16 *)n)->bitmap[i][0], 32, j);
					if (j < 32)
						return minimum(((art_node16 *)n)->children[((art_node16 *)n)->keys[j + (i * 32)]]);
				}
			}
		case NODE128:
			idx = 0;
			while (!((art_node128 *)n)->children[idx]) idx++;
			return minimum(((art_node128 *)n)->children[idx]);
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
    dest->partial_len = src->partial_len;
    memcpy(dest->partial, src->partial, min(MAX_PREFIX_LEN, src->partial_len));
}

static void add_child128_noflush(art_node128 *n, art_node **ref, unsigned char c, void *child) {
	(void)ref;
	n->children[c] = (art_node *)child;
}

static void add_child128(art_node128 *n, art_node **ref, unsigned char c, void *child) {
	(void)ref;
	n->children[c] = (art_node *)child;
	flush_buffer(&n->children[c], 8, true);
}

static void add_child16_noflush(art_node16 *n, art_node **ref, unsigned char c, void *child) {
	int idx;
	unsigned int p_bitmap = 0;
	p_bitmap = n->bitmap[0][1] + n->bitmap[1][1] + n->bitmap[2][1] + n->bitmap[3][1];

	idx = find_next_zero_bit((unsigned long *)&p_bitmap, 16, 0);
	if (idx == 16) {
		printf("find next zero bit error in child 16\n");
		abort();
	}

	n->keys[c] = idx;
	n->children[idx] = child;

	n->bitmap[c / 32][0] = n->bitmap[c / 32][0] + (0x1U << (c % 32));
	n->bitmap[c / 32][1] = n->bitmap[c / 32][1] + (0x1U << idx);
}

static void add_child16(art_node16 *n, art_node **ref, unsigned char c, void *child) {
	unsigned int p_bitmap = 0;
	p_bitmap = n->bitmap[0][1] + n->bitmap[1][1] + n->bitmap[2][1] + n->bitmap[3][1];

	if (p_bitmap != ((0x1U << 16) - 1)) {
		int idx;

		idx = find_next_zero_bit((unsigned long *)&p_bitmap, 16, 0);
		if (idx == 16) {
			printf("find next zero bit error in child 16\n");
			abort();
		}

		n->keys[c] = idx;
		n->children[idx] = child;
		
		n->bitmap[c / 32][0] = n->bitmap[c / 32][0] + (0x1U << (c % 32));
		n->bitmap[c / 32][1] = n->bitmap[c / 32][1] + (0x1U << idx);

		flush_buffer(&n->keys[c], sizeof(unsigned char), false);
		flush_buffer(&n->children[idx], 8, false);
		flush_buffer(n->bitmap[c / 32], sizeof(n->bitmap[c / 32]), true);
	} else {
		int i, j, num = 0;
		art_node128 *new_node = (art_node128 *)alloc_node(NODE128);

		for (i = 0; i < 4; i++) {
			for (j = 0; j < 32; j++) { 
				j = find_next_bit((unsigned long *)&n->bitmap[i][0], 32, j);
				if (j < 32) {
					new_node->children[j + (i * 32)] = n->children[n->keys[j + (i * 32)]];
					num++;
					if (num == 16)
						break;
				}
			}
			if (num == 16)
				break;
		}
		copy_header((art_node *)new_node, (art_node *)n);
		*ref = (art_node *)new_node;
		add_child128_noflush(new_node, ref, c, child);
		flush_buffer(new_node, sizeof(art_node128), false);
		flush_buffer(ref, 8, true);

		node16_count--;
		free(n);
	}
}

static void add_child4_noflush(art_node4 *n, art_node **ref, unsigned char c, void *child) {
	slot_array temp_slot[4];
	int i, idx, mid = -1;
	unsigned long p_idx = 0;

	for (idx = 0; ((n->slot[idx].key != -1) && (idx < 4)); idx++) {
		p_idx = p_idx + (1 << n->slot[idx].i_ptr);
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
			temp_slot[i].key = -1;
	}

	temp_slot[mid].key = c;
	temp_slot[mid].i_ptr = p_idx;

	for (i = mid - 1; i >=0; i--) {
		temp_slot[i].key = n->slot[i].key;
		temp_slot[i].i_ptr = n->slot[i].i_ptr;
	}

	*((uint64_t *)n->slot) = *((uint64_t *)temp_slot);
}

static void add_child4(art_node4 *n, art_node **ref, unsigned char c, void *child) {
	if (n->slot[3].key == -1) {
		slot_array temp_slot[4];
		int i, idx, mid = -1;
		unsigned long p_idx = 0;

		for (idx = 0; ((n->slot[idx].key != -1) && (idx < 4)); idx++) {
			p_idx = p_idx + (1 << n->slot[idx].i_ptr);
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
				temp_slot[i].key = -1;
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
			new_node->bitmap[n->slot[idx].key / 32][0] = 
				new_node->bitmap[n->slot[idx].key / 32][0] + 
				(0x1U << (n->slot[idx].key % 32));
			new_node->bitmap[n->slot[idx].key / 32][1] = 
				new_node->bitmap[n->slot[idx].key / 32][1] + 
				(0x1U << (n->slot[idx].i_ptr));
			new_node->keys[n->slot[idx].key] = n->slot[idx].i_ptr;
			new_node->children[(int)n->slot[idx].i_ptr] = n->children[(int)n->slot[idx].i_ptr];
		}
		copy_header((art_node *)new_node, (art_node *)n);
		add_child16_noflush(new_node, ref, c, child);

		*ref = (art_node *)new_node;
		flush_buffer(new_node, sizeof(art_node16), false);
		flush_buffer(ref, 8, true);

		node4_count--;
		free(n);
	}
}

static void add_child(art_node *n, art_node **ref, unsigned char c, void *child) {
	switch (n->type) {
		case NODE4:
			return add_child4((art_node4 *)n, ref, c, child);
		case NODE16:
			return add_child16((art_node16 *)n, ref, c, child);
//		case NODE32:
//			return add_child32((art_node32 *)n, ref, c, child);
		case NODE128:
			return add_child128((art_node128 *)n, ref, c, child);
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
		flush_buffer(inserted_it, sizeof(item), false);
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

		// Create a new node
		art_node4 *new_node = (art_node4*)alloc_node(NODE4);
		art_node *copy_node;

		switch (n->type) {
			case NODE4:
				copy_node = (art_node *)malloc(sizeof(art_node4));
				memcpy(copy_node, n, sizeof(art_node4));
				break;
			case NODE16:
				copy_node = (art_node *)malloc(sizeof(art_node16));
				memcpy(copy_node, n, sizeof(art_node16));
				break;
			case NODE128:
				copy_node = (art_node *)malloc(sizeof(art_node128));
				memcpy(copy_node, n, sizeof(art_node128));
				break;
			default:
				abort();
			
		}

		new_node->n.partial_len = prefix_diff;
		memcpy(new_node->n.partial, copy_node->partial, min(MAX_PREFIX_LEN, prefix_diff));

		// Adjust the prefix of the old node
		if (copy_node->partial_len <= MAX_PREFIX_LEN) {
			add_child4_noflush(new_node, ref, copy_node->partial[prefix_diff], copy_node);
			copy_node->partial_len -= (prefix_diff+1);
			memmove(copy_node->partial, copy_node->partial+prefix_diff+1,
					min(MAX_PREFIX_LEN, copy_node->partial_len));
		} else {
			copy_node->partial_len -= (prefix_diff+1);
			add_child4_noflush(new_node, ref, ITEM_key(min_it)[depth + prefix_diff], copy_node);
			memcpy(copy_node->partial, ITEM_key(min_it)+depth+prefix_diff+1,
					min(MAX_PREFIX_LEN, copy_node->partial_len));
		}

		// Insert the new leaf
		add_child4_noflush(new_node, ref, key[depth + prefix_diff], SET_ITEM(inserted_it));
		
		if(copy_node->type == NODE4)
			flush_buffer(copy_node, sizeof(art_node4), false);
		else if (copy_node->type == NODE16)
			flush_buffer(copy_node, sizeof(art_node16), false);
		else
			flush_buffer(copy_node, sizeof(art_node128), false);		


		flush_buffer(new_node, sizeof(art_node4), false);
		flush_buffer(inserted_it, sizeof(item), false);
		

		*ref = (art_node*)new_node;	
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

	flush_buffer(inserted_it, sizeof(item), false);
    // No child, node goes within us
    add_child(n, ref, key[depth], SET_ITEM(inserted_it));
    return NULL;
}

int assoc_insert(item *it) {	//ok	//ok
	printf("[assoc_insert]\n");
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


/*
static void remove_child256(art_node256 *n, art_node **ref, unsigned char c) {		//ok	//ok
    n->children[c] = NULL;

    // Resize to a node48 on underflow, not immediately to prevent
    // trashing if we sit on the 48/49 boundary
    if (n->n.num_children == 37) {
        art_node48 *new_node = (art_node48*)alloc_node(NODE48);
        *ref = (art_node*)new_node;
        copy_header((art_node*)new_node, (art_node*)n);

        int pos = 0;
        for (int i=0;i<256;i++) {
            if (n->children[i]) {
                new_node->children[pos] = n->children[i];
                new_node->keys[i] = pos + 1;
                pos++;
            }
        }
        free(n);
    }
}

static void remove_child48(art_node48 *n, art_node **ref, unsigned char c) {	//ok	//ok
    int pos = n->keys[c];
    n->keys[c] = 0;
    n->children[pos-1] = NULL;
    n->n.num_children--;

    if (n->n.num_children == 12) {
        art_node16 *new_node = (art_node16*)alloc_node(NODE16);
        *ref = (art_node*)new_node;
        copy_header((art_node*)new_node, (art_node*)n);

        int child = 0;
        for (int i=0;i<256;i++) {
            pos = n->keys[i];
            if (pos) {
                new_node->keys[child] = i;
                new_node->children[child] = n->children[pos - 1];
                child++;
            }
        }
        free(n);
    }
}

static void remove_child16(art_node16 *n, art_node **ref, art_node **l) {		//ok	//ok
    int pos = l - n->children;
    memmove(n->keys+pos, n->keys+pos+1, n->n.num_children - 1 - pos);
    memmove(n->children+pos, n->children+pos+1, (n->n.num_children - 1 - pos)*sizeof(void*));
    n->n.num_children--;

    if (n->n.num_children == 3) {
        art_node4 *new_node = (art_node4*)alloc_node(NODE4);
        *ref = (art_node*)new_node;
        copy_header((art_node*)new_node, (art_node*)n);
        memcpy(new_node->keys, n->keys, 4);
        memcpy(new_node->children, n->children, 4*sizeof(void*));
        free(n);
    }
}

static void remove_child4(art_node4 *n, art_node **ref, art_node **l) {	//ok	//ok
    int pos = l - n->children;
    memmove(n->keys+pos, n->keys+pos+1, n->n.num_children - 1 - pos);
    memmove(n->children+pos, n->children+pos+1, (n->n.num_children - 1 - pos)*sizeof(void*));
    n->n.num_children--;

    // Remove nodes with only a single child
    if (n->n.num_children == 1) {
        art_node *child = n->children[0];
        if (!IS_ITEM(child)) {
            // Concatenate the prefixes
            int prefix = n->n.partial_len;
            if (prefix < MAX_PREFIX_LEN) {
                n->n.partial[prefix] = n->keys[0];
                prefix++;
            }
            if (prefix < MAX_PREFIX_LEN) {
                int sub_prefix = min(child->partial_len, MAX_PREFIX_LEN - prefix);
                memcpy(n->n.partial+prefix, child->partial, sub_prefix);
                prefix += sub_prefix;
            }

            // Store the prefix in the child
            memcpy(child->partial, n->n.partial, min(prefix, MAX_PREFIX_LEN));
            child->partial_len += n->n.partial_len + 1;
        }
        *ref = child;
        free(n);
    }
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
*/
/*
void assoc_delete(const char *_key, const size_t nkey) {			//ok
	//printf("[assoc_delete]\n");

	unsigned char *key = (unsigned char *)_key;
    art_node **child = NULL;
    art_node *n = T->root;
    int prefix_len, depth = 0;
    while (n) {
        // Might be a leaf
        if (IS_ITEM(n)) {
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
*/

void assoc_delete(const char *key, const size_t nkey) {			//ok
 //   item *deleted_it = recursive_delete(T->root, &T->root, (unsigned char *)key, nkey, 0);
  //  if (deleted_it) {
       //  T->size--;
       // free(deleted_it);
  //      return;
  //  } else {
	//	printf("[assoc_delete] Maybe failed! There is no deleted item!!\n");
	//	exit(1);
	//}
	//
	printf("Not implemented yet\n");
}
