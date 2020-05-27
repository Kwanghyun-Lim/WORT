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

unsigned long IN_count = 0;
unsigned long LN_count = 0;
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
#include <stdint.h>
#include <sys/time.h>

#define mfence() asm volatile("mfence":::"memory")
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
 
static inline int max(int a, int b) {
     return (a > b) ? a : b;
}
 
key_item *make_key_item(unsigned char *key, int key_len)
{
     key_item *new_key = malloc(sizeof(key_item) + key_len);
     new_key->key_len = key_len;
     memcpy(new_key->key, key, key_len);
 
     flush_buffer(new_key, sizeof(key_item) + key_len, false);
 
    return new_key;
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



void *allocINode(unsigned long num)
{   
     IN *new_INs = calloc(num, sizeof(IN));
     IN_count = num;
     return (void *)new_INs;
}

/* Alloc non-volatile leaf node */
LN *allocLNode()
{
     LN *new_LN = calloc(1, sizeof(LN));
     flush_buffer(new_LN, sizeof(LN), false);
     LN_count++;
     return new_LN;
}

tree *initTree()
{
     tree *t = calloc(1, sizeof(tree));
     return t;
}


int binary_search_IN(unsigned char *key, int key_len, IN *node)
{   
     int low = 0, mid = 0, high = (node->nKeys - 1);
     int len, decision;
     
     while (low <= high) {
         mid = (low + high) / 2;
         len = max((node->key[mid])->key_len, key_len);
         decision = memcmp((node->key[mid])->key, key, len);
         if (decision > 0)
             high = mid - 1; 
         else if (decision < 0)
             low = mid + 1;
         else
             break;
     }
    
     if (low > mid)
         mid = low;
    
     return mid;
}
 
int binary_search_PLN(unsigned char *key, int key_len, PLN *node)
{
     int low = 0, mid = 0, high = (node->nKeys - 1);
     int len, decision;
 
     while (low <= high) {
         mid = (low + high) / 2;
         len = max((node->entries[mid].key)->key_len, key_len);
         decision = memcmp((node->entries[mid].key)->key, key, len);
         if (decision > 0)
             high = mid - 1;
         else if (decision < 0)
             low = mid + 1;
         else
             break;
     }

	if (low > mid)
         mid = low;

    return mid;
}
 
LN *find_leaf(tree *t, unsigned char *key, int key_len)
{
     unsigned int pos, id, i;
     IN *current_IN;
     PLN *current_PLN;
     LN *current_LN;
 
     if (t->is_leaf == 2) {
         id = 0;
 
         current_IN = (IN *)t->root;
 
         while (id < t->first_PLN_id) {
             pos = binary_search_IN(key, key_len, &current_IN[id]);
             /* MAX_NUM_ENTRY_IN = 2m + 1 */
             id = id * (MAX_NUM_ENTRY_IN)+ 1 + pos;
         }
 
         current_PLN = (PLN *)t->root;
 
         pos = binary_search_PLN(key, key_len, &current_PLN[id]);
 
         if (pos < current_PLN[id].nKeys)
             return current_PLN[id].entries[pos].ptr;
         else
             return NULL;
     } else if (t->is_leaf == 1) {
         current_PLN = (PLN *)t->root;
 
         pos = binary_search_PLN(key, key_len, &current_PLN[0]);
 
		if (pos < current_PLN[0].nKeys)
             return current_PLN[0].entries[pos].ptr;
        else
             return NULL;
     } else {
         current_LN = (LN *)t->root;
         return current_LN;
     }   
}   


int search_leaf_node(LN *node, unsigned char *key, int key_len)
{
     int i, pos;
 
     for (i = 0; i < node->nElements; i++) {
         if (memcmp((node->LN_Element[i].key)->key, key, max((node->LN_Element[i].key)->key_len, key_len)) == 0 &&
                 node->LN_Element[i].flag == true) {
             pos = i;
             return pos;
         }
 
         if (memcmp((node->LN_Element[i].key)->key, key, max((node->LN_Element[i].key)->key_len, key_len)) == 0 &&
                 node->LN_Element[i].flag == false) {
             pos = -1;
             return pos;
         }
     }
     return -1;
}
 
void *Lookup(tree *t, unsigned char *key, int key_len)
{
     int pos, i;
     LN *current_LN;
     IN *current_IN = t->root;
 
     current_LN = find_leaf(t, key, key_len);
     if (current_LN == NULL)
 		 goto fail;

     pos = search_leaf_node(current_LN, key, key_len);
     if (pos < 0)
         goto fail;
 
     return current_LN->LN_Element[pos].value;
fail:
    return;
}


/* ----------------NV Tree ---------------------*/
#define mfence() asm volatile("mfence":::"memory")

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

static inline int max(int a, int b) {
	return (a > b) ? a : b;
}

key_item *make_key_item(unsigned char *key, int key_len)
{
	key_item *new_key = malloc(sizeof(key_item) + key_len);
	new_key->key_len = key_len;
	memcpy(new_key->key, key, key_len);

	flush_buffer(new_key, sizeof(key_item) + key_len, false);

	return new_key;
}

/* Alloc volatile node */
void *allocINode(unsigned long num)
{
	IN *new_INs = calloc(num, sizeof(IN));
	IN_count = num;
	return (void *)new_INs;
}

/* Alloc non-volatile leaf node */
LN *allocLNode()
{
	LN *new_LN = calloc(1, sizeof(LN));
	flush_buffer(new_LN, sizeof(LN), false);
	LN_count++;
	return new_LN;
}

tree *initTree()
{
	tree *t = calloc(1, sizeof(tree));
	return t;
}

int binary_search_IN(unsigned char *key, int key_len, IN *node)
{
	int low = 0, mid = 0, high = (node->nKeys - 1);
	int len, decision;

	while (low <= high) {
		mid = (low + high) / 2;
		len = max((node->key[mid])->key_len, key_len);
		decision = memcmp((node->key[mid])->key, key, len);
		if (decision > 0)
			high = mid - 1;
		else if (decision < 0)
			low = mid + 1;
		else
			break;
	}

	if (low > mid)
		mid = low;

	return mid;
}

int binary_search_PLN(unsigned char *key, int key_len, PLN *node)
{
	int low = 0, mid = 0, high = (node->nKeys - 1);
	int len, decision;

	while (low <= high) {
		mid = (low + high) / 2;
		len = max((node->entries[mid].key)->key_len, key_len);
		decision = memcmp((node->entries[mid].key)->key, key, len);
		if (decision > 0)
			high = mid - 1;
		else if (decision < 0)
			low = mid + 1;
		else
			break;
	}

	if (low > mid)
		mid = low;

	return mid;
}

LN *find_leaf(tree *t, unsigned char *key, int key_len)
{
	unsigned int pos, id, i;
	IN *current_IN;
	PLN *current_PLN;
	LN *current_LN;

	if (t->is_leaf == 2) {
		id = 0;

		current_IN = (IN *)t->root;

		while (id < t->first_PLN_id) {
			pos = binary_search_IN(key, key_len, &current_IN[id]);
			/* MAX_NUM_ENTRY_IN = 2m + 1 */
			id = id * (MAX_NUM_ENTRY_IN)+ 1 + pos;
		}

		current_PLN = (PLN *)t->root;

		pos = binary_search_PLN(key, key_len, &current_PLN[id]);

		if (pos < current_PLN[id].nKeys)
			return current_PLN[id].entries[pos].ptr;
		else
			return NULL;
	} else if (t->is_leaf == 1) {
		current_PLN = (PLN *)t->root;

		pos = binary_search_PLN(key, key_len, &current_PLN[0]);

		if (pos < current_PLN[0].nKeys)
			return current_PLN[0].entries[pos].ptr;
		else
			return NULL;
	} else {
		current_LN = (LN *)t->root;
		return current_LN;
	}
}

int search_leaf_node(LN *node, unsigned char *key, int key_len)
{
	int i, pos;

	for (i = 0; i < node->nElements; i++) {
		if (memcmp((node->LN_Element[i].key)->key, key, max((node->LN_Element[i].key)->key_len, key_len)) == 0 &&
				node->LN_Element[i].flag == true) {
			pos = i;
			return pos;
		}

		if (memcmp((node->LN_Element[i].key)->key, key, max((node->LN_Element[i].key)->key_len, key_len)) == 0 &&
				node->LN_Element[i].flag == false) {
			pos = -1;
			return pos;
		}
	}
	return -1;
}

void *Lookup(tree *t, unsigned char *key, int key_len)
{
	int pos, i;
	LN *current_LN;
	IN *current_IN = t->root;

	current_LN = find_leaf(t, key, key_len);
	if (current_LN == NULL)
		goto fail;

	pos = search_leaf_node(current_LN, key, key_len);
	if (pos < 0)
		goto fail;

	return current_LN->LN_Element[pos].value;
fail:
	return;
}

void insertion_sort(struct LN_entry *base, int num)
{
	int i, j;
	struct LN_entry temp;

	for (i = 1; i < num; i++) {
		for (j = i; j > 0; j--) {
			if (memcmp((base[j - 1].key)->key, (base[j].key)->key,
						max((base[j - 1].key)->key_len, (base[j].key)->key_len)) > 0) {
				temp = base[j - 1];
				base[j - 1] = base[j];
				base[j] = temp;
			} else
				break;
		}
	}
}

int create_new_tree(tree *t, key_item *new_item, void *value)
{
	int errval = -1;
	LN *current_LN;
	t->root = (LN *)allocLNode();
	if (t->root == NULL)
		return errval;

	current_LN = (LN *)t->root;

	current_LN->parent_id = -1;
	current_LN->sibling = NULL;
	current_LN->LN_Element[0].flag = true;
	current_LN->LN_Element[0].key = new_item;
	current_LN->LN_Element[0].value = value;
	current_LN->nElements++;
	flush_buffer(current_LN, sizeof(LN), true);

	t->height = 0;
	t->first_leaf = (LN *)t->root;
	t->is_leaf = 0;
	t->first_PLN_id = 0;
	t->last_PLN_id = 0;

	return 0;
}

int leaf_scan_divide(tree *t, LN *leaf, LN *split_node1, LN *split_node2)
{
	int i, j = 0, count = 0, invalid_count = 0, errval = -1;
	struct LN_entry *invalid_key = 
		malloc(((MAX_NUM_ENTRY_LN)/2 + 1) * sizeof(struct LN_entry));
	struct LN_entry *valid_Element =
		malloc(MAX_NUM_ENTRY_LN * sizeof(struct LN_entry));

	for (i = 0; i < leaf->nElements; i++) {
		if (leaf->LN_Element[i].flag == false) {
			invalid_key[invalid_count] = leaf->LN_Element[i];
			invalid_count++;
		}
	}

	for (i = 0; i < leaf->nElements; i++) {
		if (leaf->LN_Element[i].flag == false)
			continue;

		if (invalid_count > 0) {
			if (memcmp((leaf->LN_Element[i].key)->key, (invalid_key[count].key)->key,
						max((leaf->LN_Element[i].key)->key_len, (invalid_key[count].key)->key_len)) == 0) {
				count++;
				invalid_count--;
				continue;
			}
		}
		valid_Element[j] = leaf->LN_Element[i];
		j++;
	}

//	quick_sort(valid_Element, 0, j - 1);
	insertion_sort(valid_Element, j);

	memcpy(split_node1->LN_Element, valid_Element, 
			sizeof(struct LN_entry) * (j / 2));
	split_node1->nElements = (j / 2);

	memcpy(split_node2->LN_Element, &valid_Element[j / 2],
			sizeof(struct LN_entry) * (j - (j / 2)));
	split_node2->nElements = (j - (j / 2));

	return 0;
}

int recursive_insert_IN(void *root_addr, unsigned long first_IN_id,
		unsigned long num_IN)
{
	int errval = -1;
	unsigned long id, last_id, pos, prev_id, IN_count = 0;
	IN *current_IN, *prev_IN;

	id = first_IN_id;
	last_id = first_IN_id + num_IN - 1;

	if (id > 0) {
		while (id <= last_id) {
			for (pos = 0; pos < MAX_NUM_ENTRY_IN; pos++) {
				current_IN = (IN *)root_addr;
				prev_id = (id - pos - 1) / MAX_NUM_ENTRY_IN;
				prev_IN = (IN *)root_addr;
				prev_IN[prev_id].key[prev_IN[prev_id].nKeys] =
					current_IN[id].key[current_IN[id].nKeys - 1];
				prev_IN[prev_id].nKeys++;
				id++;
				if (id > last_id)
					break;
			}
			IN_count++;
		}

		first_IN_id = (first_IN_id - 1) / MAX_NUM_ENTRY_IN;
		errval = recursive_insert_IN(root_addr, first_IN_id, IN_count);
		if (errval < 0)
			goto fail;
	}

	errval = 0;
fail:
	return errval;
}

int reconstruct_from_PLN(void *root_addr, unsigned long first_PLN_id, 
		unsigned long num_PLN)
{
	unsigned long prev_id, id, pos, last_id, IN_count = 0, first_IN_id;
	int i, errval;
	IN *prev_IN;
	PLN *current_PLN;

	id = first_PLN_id;
	last_id = first_PLN_id + num_PLN - 1;

	while (id <= last_id) {
		for (pos = 0; pos < MAX_NUM_ENTRY_IN; pos++) {
			current_PLN = (PLN *)root_addr;
			prev_id = (id - pos - 1) / MAX_NUM_ENTRY_IN;

			prev_IN = (IN *)root_addr;
			prev_IN[prev_id].key[prev_IN[prev_id].nKeys] =
				current_PLN[id].entries[current_PLN[id].nKeys - 1].key;
			prev_IN[prev_id].nKeys++;

			for (i = 0; i < current_PLN[id].nKeys; i++) {
				current_PLN[id].entries[i].ptr->parent_id = id;
	//			flush_buffer(&current_PLN[id].entries[i].ptr->parent_id,
	//					sizeof(unsigned long), false);
			}
	//		sfence();

			id++;
			if (id > last_id)
				break;
		}
		IN_count++;	//num_IN
	}

	first_IN_id = (first_PLN_id - 1) / MAX_NUM_ENTRY_IN;
	if (first_IN_id > 0) {
		errval = recursive_insert_IN(root_addr, first_IN_id, IN_count);
		if (errval < 0)
			return errval;
	}

	return 0;
}

int insert_node_to_PLN(PLN *new_PLNs, unsigned long parent_id, key_item *insert_key,
		key_item *split_max_key, LN *split_node1, LN *split_node2)
{
	int entry_index, i;
	unsigned long inserted_PLN_id;

	if (memcmp(split_max_key->key, (new_PLNs[parent_id].entries[new_PLNs[parent_id].nKeys - 1].key)->key,
				max(split_max_key->key_len, (new_PLNs[parent_id].entries[new_PLNs[parent_id].nKeys - 1].key)->key_len)) > 0)
		inserted_PLN_id = parent_id + 1;
	else
		inserted_PLN_id = parent_id;

	for (i = 0; i < new_PLNs[inserted_PLN_id].nKeys; i++) {
		if (memcmp(split_max_key->key, (new_PLNs[inserted_PLN_id].entries[i].key)->key,
					max(split_max_key->key_len, (new_PLNs[inserted_PLN_id].entries[i].key)->key_len)) <= 0) {
			struct PLN_entry temp[new_PLNs[inserted_PLN_id].nKeys - i];
			memcpy(temp, &new_PLNs[inserted_PLN_id].entries[i],
					sizeof(struct PLN_entry) * (new_PLNs[inserted_PLN_id].nKeys - i));
			memcpy(&new_PLNs[inserted_PLN_id].entries[i + 1], temp,
					sizeof(struct PLN_entry) * (new_PLNs[inserted_PLN_id].nKeys - i));
			new_PLNs[inserted_PLN_id].entries[i].key = insert_key;
			new_PLNs[inserted_PLN_id].entries[i].ptr = split_node1;
			new_PLNs[inserted_PLN_id].entries[i + 1].ptr = split_node2;
			new_PLNs[inserted_PLN_id].nKeys++;
			entry_index = i;
			break;
		}
	}

	return entry_index;
}


int reconstruct_PLN(tree *t, unsigned long parent_id, key_item *insert_key,
		key_item *split_max_key, LN *split_node1, LN *split_node2)
{
	unsigned long height, max_PLN, total_PLN, total_IN = 1;
	unsigned long new_parent_id;
	unsigned int i, entry_index;
	int errval;
	IN *new_INs;
	PLN *old_PLNs, *new_PLNs;

	height = t->height;

	max_PLN = 1;

	while (height) {
		max_PLN = max_PLN * MAX_NUM_ENTRY_IN;
		height--;
	}

	total_PLN = (t->last_PLN_id - t->first_PLN_id) + 2;

	if (t->is_leaf != 1) {
		if (total_PLN > max_PLN) {
			height = t->height;
			height++;
			for (i = 1; i < height; i++)
				total_IN += total_IN * MAX_NUM_ENTRY_IN;

			new_parent_id = total_IN + parent_id - t->first_PLN_id;

			new_PLNs = (PLN *)allocINode(total_IN + total_PLN);
			old_PLNs = (PLN *)t->root;
			/* total IN == new_first_PLN_id */
			memcpy(&new_PLNs[total_IN], &old_PLNs[t->first_PLN_id],
					sizeof(PLN) * (parent_id - t->first_PLN_id + 1));
			memcpy(&new_PLNs[new_parent_id + 2], &old_PLNs[parent_id + 1], 
					sizeof(PLN) * (t->last_PLN_id - parent_id));
			memcpy(&new_PLNs[new_parent_id + 1].entries,
					&new_PLNs[new_parent_id].entries[new_PLNs[new_parent_id].nKeys / 2],
					sizeof(struct PLN_entry) * (new_PLNs[new_parent_id].nKeys -
						(new_PLNs[new_parent_id].nKeys / 2)));

			new_PLNs[new_parent_id + 1].nKeys = (new_PLNs[new_parent_id].nKeys -
				 (new_PLNs[new_parent_id].nKeys / 2));
			new_PLNs[new_parent_id].nKeys = (new_PLNs[new_parent_id].nKeys / 2);

			entry_index = insert_node_to_PLN(new_PLNs, new_parent_id, 
					insert_key, split_max_key, split_node1, split_node2);

			errval = reconstruct_from_PLN(new_PLNs, total_IN, total_PLN);
			if (errval < 0)
				goto fail;

			free(t->root);
			t->height = height;
			t->root = (IN *)new_PLNs;
			t->first_PLN_id = total_IN;
			t->last_PLN_id = total_IN + total_PLN - 1;
			t->is_leaf = 2;
		} else {
			old_PLNs = (PLN *)t->root;
			new_PLNs = (PLN *)allocINode(t->first_PLN_id + total_PLN);

			/* copy from first PLN of old PLNs to parent_id's PLN of new PLNs */
			memcpy(&new_PLNs[t->first_PLN_id], &old_PLNs[t->first_PLN_id],
					sizeof(PLN) * (parent_id - t->first_PLN_id + 1));
			/* copy from (parent_id + 1) ~ last of old PLNs to parent_id + 2
			 * of newPLNsnew */
			memcpy(&new_PLNs[parent_id + 2], &old_PLNs[parent_id + 1],
					sizeof(PLN) * (t->last_PLN_id - parent_id));
			/* copy the half of PLN's entries to (parent_id + 1)'s PLN */
			memcpy(&new_PLNs[parent_id + 1].entries, 
					&new_PLNs[parent_id].entries[new_PLNs[parent_id].nKeys / 2],
					sizeof(struct PLN_entry) * (new_PLNs[parent_id].nKeys -
						(new_PLNs[parent_id].nKeys / 2)));

			new_PLNs[parent_id + 1].nKeys = (new_PLNs[parent_id].nKeys -
					(new_PLNs[parent_id].nKeys / 2));
			new_PLNs[parent_id].nKeys = (new_PLNs[parent_id].nKeys / 2);

			entry_index = insert_node_to_PLN(new_PLNs, parent_id, insert_key,
					split_max_key, split_node1, split_node2);

			errval = reconstruct_from_PLN(new_PLNs, t->first_PLN_id, total_PLN);
			if (errval < 0)
				goto fail;

			free(t->root);
			t->root = (IN *)new_PLNs;
			t->last_PLN_id++;
			t->is_leaf = 2;
		}
	} else {
		total_IN = 1;
		old_PLNs = (PLN *)t->root;
		new_PLNs = (PLN *)allocINode(total_IN + total_PLN);
		new_parent_id = total_IN + parent_id - t->first_PLN_id;

		memcpy(&new_PLNs[total_IN], &old_PLNs[t->first_PLN_id], sizeof(PLN) * 1);
		memcpy(&new_PLNs[2].entries, &new_PLNs[1].entries[new_PLNs[1].nKeys / 2],
				sizeof(struct PLN_entry) * (new_PLNs[1].nKeys - (new_PLNs[1].nKeys / 2)));

		new_PLNs[new_parent_id + 1].nKeys = (new_PLNs[new_parent_id].nKeys -
			 (new_PLNs[new_parent_id].nKeys / 2));
		new_PLNs[new_parent_id].nKeys = (new_PLNs[new_parent_id].nKeys / 2);

		entry_index = insert_node_to_PLN(new_PLNs, new_parent_id, 
				insert_key, split_max_key, split_node1, split_node2);

		errval = reconstruct_from_PLN(new_PLNs, total_IN, total_PLN);
		if (errval < 0)
			goto fail;

		free(t->root);
		t->height = 1;
		t->root = (IN *)new_PLNs;
		t->first_PLN_id = total_IN;
		t->last_PLN_id = total_IN + total_PLN - 1;
		t->is_leaf = 2;
	}
	return entry_index;
fail:
	return errval;
}

int insert_to_PLN(tree *t, unsigned long parent_id, 
		LN *split_node1, LN *split_node2)
{
	int entry_index;
	/* Newly inserted key to PLN */
	key_item *insert_key = split_node1->LN_Element[split_node1->nElements - 1].key;
	key_item *split_max_key = split_node2->LN_Element[split_node2->nElements - 1].key;

	PLN *parent = (PLN *)t->root;

	if (parent[parent_id].nKeys == MAX_NUM_ENTRY_PLN) {
		/* PLN split */
		entry_index = reconstruct_PLN(t, parent_id, insert_key, 
				split_max_key, split_node1, split_node2);
		if (entry_index < 0)
			goto fail;
	} else if (memcmp(split_max_key->key, (parent[parent_id].entries[parent[parent_id].nKeys - 1].key)->key,
				max(split_max_key->key_len, (parent[parent_id].entries[parent[parent_id].nKeys - 1].key)->key_len)) <= 0) {
		/* Not PLN split */
		for (entry_index = 0; entry_index < parent[parent_id].nKeys; entry_index++) {
			if (memcmp(split_max_key->key, (parent[parent_id].entries[entry_index].key)->key,
						max(split_max_key->key_len, (parent[parent_id].entries[parent[parent_id].nKeys - 1].key)->key_len)) <= 0) {

				struct PLN_entry temp[parent[parent_id].nKeys - entry_index];

				memcpy(temp, &parent[parent_id].entries[entry_index],
						sizeof(struct PLN_entry) * (parent[parent_id].nKeys - entry_index));
				memcpy(&parent[parent_id].entries[entry_index + 1], temp,
						sizeof(struct PLN_entry) * (parent[parent_id].nKeys - entry_index));

				parent[parent_id].entries[entry_index].key = insert_key;
				parent[parent_id].entries[entry_index].ptr = split_node1;
				parent[parent_id].entries[entry_index + 1].ptr = split_node2;

				split_node1->parent_id = parent_id;
				split_node2->parent_id = parent_id;
				parent[parent_id].nKeys++;
				break;
			}
		}
	}

fail:
	return entry_index;	//새로운 key가 삽입된 PLN의 entry index번호
}

void insert_entry_to_leaf(LN *leaf, key_item *new_item, void *value, bool flush)
{
	if (flush == true) {
		leaf->LN_Element[leaf->nElements].flag = true;
		leaf->LN_Element[leaf->nElements].key = new_item;
		leaf->LN_Element[leaf->nElements].value = value;
		flush_buffer(&leaf->LN_Element[leaf->nElements], 
				sizeof(struct LN_entry), false);
		leaf->nElements++;
		flush_buffer(&leaf->nElements, sizeof(unsigned char), true);
	} else {
		leaf->LN_Element[leaf->nElements].flag = true;
		leaf->LN_Element[leaf->nElements].key = new_item;
		leaf->LN_Element[leaf->nElements].value = value;
		leaf->nElements++;
	}
}

int leaf_split_and_insert(tree *t, LN *leaf, key_item *new_item, void *value)
{
	int errval = -1, current_idx;
	LN *split_node1, *split_node2, *prev_leaf, *new_leaf;
	PLN *prev_PLN;

	split_node1 = allocLNode();
	split_node2 = allocLNode();
	split_node1->sibling = split_node2;
	split_node2->sibling = leaf->sibling;

	if (split_node1 == NULL || split_node2 == NULL)
		return errval;

	leaf_scan_divide(t, leaf, split_node1, split_node2);

	if (t->is_leaf != 0) {
		current_idx = insert_to_PLN(t, leaf->parent_id, split_node1, split_node2);
		if (current_idx < 0)
			goto fail;

		if (current_idx != 0) {
			prev_PLN = (PLN *)t->root;
			prev_leaf = prev_PLN[split_node1->parent_id].entries[current_idx - 1].ptr;
		} else {
			if (split_node1->parent_id > t->first_PLN_id) {
				prev_PLN = (PLN *)t->root;
				prev_leaf = prev_PLN[split_node1->parent_id - 1].entries[prev_PLN[split_node1->parent_id - 1].nKeys - 1].ptr;
			} else {
				t->first_leaf = split_node1;
				goto end;
			}
		}

		new_leaf = find_leaf(t, new_item->key, new_item->key_len);

		insert_entry_to_leaf(new_leaf, new_item, value, false);

		flush_buffer(split_node1, sizeof(LN), false);
		flush_buffer(split_node2, sizeof(LN), false);

		prev_leaf->sibling = split_node1;
		flush_buffer(&prev_leaf->sibling, sizeof(&prev_leaf->sibling), true);
	} else {
		PLN *new_PLN = (PLN *)allocINode(1);

		split_node1->parent_id = 0;
		split_node2->parent_id = 0;
		split_node2->sibling = NULL;

		new_PLN->entries[new_PLN->nKeys].key = 
			split_node1->LN_Element[split_node1->nElements - 1].key;
		new_PLN->entries[new_PLN->nKeys].ptr = split_node1;
		new_PLN->nKeys++;
		new_PLN->entries[new_PLN->nKeys].key = make_key_item(MAX_KEY, strlen(MAX_KEY));
		new_PLN->entries[new_PLN->nKeys].ptr = split_node2;
		new_PLN->nKeys++;

		t->height = 0;
		t->is_leaf = 1;
		t->first_PLN_id = 0;
		t->last_PLN_id = 0;
		t->root = new_PLN;

		new_leaf = find_leaf(t, new_item->key, new_item->key_len);
		
		insert_entry_to_leaf(new_leaf, new_item, value, false);

		flush_buffer(split_node1, sizeof(LN), false);
		flush_buffer(split_node2, sizeof(LN), true);
	}

end:
	free(leaf);
	return 0;
fail:
	return current_idx;
}

int Insert(tree *t, unsigned char *key, int key_len, void *value)
{
	int errval = -1;
	LN *leaf;

	/* Make new key item */
	key_item *new_item = make_key_item(key, key_len);

	if (t->root == NULL) {
		errval = create_new_tree(t, new_item, value);
		if (errval < 0)
			goto fail;
		return errval;
	}

	leaf = find_leaf(t, new_item->key, new_item->key_len);
	
	if (leaf == NULL) {
		printf("key = %lu\n",key);
		goto fail;
	}	

	if (leaf->nElements < MAX_NUM_ENTRY_LN)
		insert_entry_to_leaf(leaf, new_item, value, true);
	else {
		/* Insert after split */
		errval = leaf_split_and_insert(t, leaf, new_item, value);
		if (errval < 0)
			goto fail;
	}

	return 0;
fail:
	return errval;
}

