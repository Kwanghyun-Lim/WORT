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

unsigned long node_count = 0;
unsigned long mfence_count = 0;
unsigned long clflush_count = 0;
unsigned long split_count = 0;
unsigned long sum_depth = 0;

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


void flush_buffer_nocount(void *buf, unsigned long len, bool fence)
{
	unsigned long i;
	len = len + ((unsigned long)(buf) & (CACHE_LINE_SIZE - 1));
	if (fence) {
		mfence();
		for (i = 0; i < len; i += CACHE_LINE_SIZE)
			asm volatile ("clflush %0\n" : "+m" (*(char *)((unsigned long)buf+i)));
		mfence();
	} else {
		for (i = 0; i < len; i += CACHE_LINE_SIZE)
			asm volatile ("clflush %0\n" : "+m" (*(char *)((unsigned long)buf+i)));
	}
}

static inline int max(int a, int b) {
	return (a > b) ? a : b;
}


void add_log_entry(tree *t, void *addr, unsigned int size, unsigned char type)	//ok
{
	log_entry *log;
	int i, remain_size;

	remain_size = size - ((size / LOG_DATA_SIZE) * LOG_DATA_SIZE);

	if ((char *)t->start_log->next_offset == 
			(t->start_log->log_data + LOG_AREA_SIZE))
		t->start_log->next_offset = (log_entry *)t->start_log->log_data;

	if (size <= LOG_DATA_SIZE) {
		log = t->start_log->next_offset;
		log->size = size;
		log->type = type;
		log->addr = addr;
		memcpy(log->data, addr, size);

		if (type == LE_DATA)
			flush_buffer(log, sizeof(log_entry), false);
		else
			flush_buffer(log, sizeof(log_entry), true);

		t->start_log->next_offset = t->start_log->next_offset + 1;
	} else {
		void *next_addr = addr;

		for (i = 0; i < size / LOG_DATA_SIZE; i++) {
			log = t->start_log->next_offset;
			log->size = LOG_DATA_SIZE;
			log->type = type;
			log->addr = next_addr;
			memcpy(log->data, next_addr, LOG_DATA_SIZE);

			flush_buffer(log, sizeof(log_entry), false);

			t->start_log->next_offset = t->start_log->next_offset + 1;
			if ((char *)t->start_log->next_offset == 
					(t->start_log->log_data + LOG_AREA_SIZE))
				t->start_log->next_offset = (log_entry *)t->start_log->log_data;

			next_addr = (char *)next_addr + LOG_DATA_SIZE;
		}

		if (remain_size > 0) {
			log = t->start_log->next_offset;
			log->size = LOG_DATA_SIZE;
			log->type = type;
			log->addr = next_addr;
			memcpy(log->data, next_addr, remain_size);

			flush_buffer(log, sizeof(log_entry), false);
			
			t->start_log->next_offset = t->start_log->next_offset + 1;
		}
	}
}


node *allocNode(void) //ok
{
//	node *n = malloc(sizeof(node));
	node *n;
	posix_memalign((void *)&n, 64, sizeof(node));
	memset(n->slot,0,sizeof(n->slot));
	n->bitmap = 1;
	n->isleaf = 1;
	node_count++;
	return n;
}

static tree *T;


void initTree(void) //ok
{
	T =malloc(sizeof(tree)); 
	T->root = allocNode(); 
//	T->start_log = malloc(sizeof(log_area));
	posix_memalign((void *)&T->start_log, 64, sizeof(log_area));
	T->start_log->next_offset = (log_entry *)T->start_log->log_data;
	return;
}

void assoc_init(const int hashtable_init) {  /*[KH] The variable "hashtable_init" is not used as real */
   	printf("[assoc_init] 500w500r\n");
   	initTree();
	printf("[assoc_init] Tree is well initialized!\n");
}


int Search(node *curr, char *temp, unsigned char *key, int key_len)	//ok ok
{
	int low = 1; 
	int mid = 1;
	int high = (int)temp[0];
	int len, decision;

	while (low <= high){
		mid = (low + high) / 2;
		len = max((curr->entries[(int)temp[mid]].key)->key_len, key_len);	//[kh] ?
		decision = memcmp((unsigned char *)(curr->entries[(int)temp[mid]].key)->key, key, len);
		if (decision > 0)
			high = mid - 1;
		else if (decision < 0)
			low = mid + 1;
		else
			break;
	}

	if (low > mid) {
		mid = low;
	}

	return mid;
} 

node *find_leaf_node(node *curr, unsigned char *key, int key_len)  //ok ok
{	
	int loc;
	sum_depth++;
	if (curr->isleaf) 
		return curr;
	loc = Search(curr, curr->slot, key, key_len);
	if (loc > curr->slot[0]) 
		return find_leaf_node(curr->entries[(int)curr->slot[loc - 1]].ptr, key, key_len);
	else if (memcmp((unsigned char *)(curr->entries[(int)curr->slot[loc]].key)->key, key, max((curr->entries[(int)curr->slot[loc]].key)->key_len, key_len)) <= 0) 
		return find_leaf_node(curr->entries[(int)curr->slot[loc]].ptr, key, key_len);
	else if (loc == 1) 
		return find_leaf_node(curr->leftmostPtr, key, key_len);
	else 
		return find_leaf_node(curr->entries[(int)curr->slot[loc - 1]].ptr, key, key_len);
}

item *assoc_find(const char *_key, const size_t nkey) { //ok
	unsigned char *key = (unsigned char *)_key;
	int key_len = (int)nkey;
	node *curr = T->root;

	curr = find_leaf_node(curr, key, key_len);  //ok
	if (curr->slot[0] == 0)
		return NULL;

	int loc = Search(curr, curr->slot, key, key_len); //ok
	if (loc > curr->slot[0]) {
		loc = curr->slot[0];
	}

	if (key_len != (curr->entries[(int)curr->slot[loc]].key)->key_len)
		return NULL;

	if (memcmp((unsigned char*)(curr->entries[(int)curr->slot[loc]].key)->key, key, key_len) != 0 || loc > curr->slot[0])
		return NULL;
	
	return (item *)curr->entries[(int)curr->slot[loc]].ptr;
}



unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
		unsigned long offset)	//ok ok
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
	if (tmp == ~0UL)	/* Are any bits zero? */
		return result + size;	/* Nope */
found_middle:
	return result + ffz(tmp);
}

int Append(node *n, key_item *new_item, void *value)	//ok ok
{
	int errval = -1;
	unsigned long index;

	index = find_next_zero_bit(&n->bitmap, BITMAP_SIZE, 1) - 1;
	if (index == BITMAP_SIZE - 1)
		return errval;

	n->entries[index].key = new_item;
	n->entries[index].ptr = value;
	return index;
}

int Append_in_inner(node *n, key_item *inserted_item, void *value)	//ok ok ok
{
	int errval = -1;
	unsigned long index;

	index = find_next_zero_bit(&n->bitmap, BITMAP_SIZE, 1) - 1;
	if (index == BITMAP_SIZE - 1)
		return errval;

	n->entries[index].key = inserted_item;
	n->entries[index].ptr = value;
	return index;
}

int insert_in_leaf_noflush(node *curr, key_item *new_item, void *value) //ok ok ok
{
	int loc, mid, j;

	curr->bitmap = curr->bitmap - 1;
	loc = Append(curr, new_item, value);

	mid = Search(curr, curr->slot, new_item->key, new_item->key_len);

	for (j = curr->slot[0]; j >= mid; j--)
		curr->slot[j + 1] = curr->slot[j];

	curr->slot[mid] = loc;

	curr->slot[0] = curr->slot[0] + 1;

	curr->bitmap = curr->bitmap + 1 + (0x1UL << (loc + 1));
	return loc;
}

void insert_in_leaf(node *curr, key_item *new_item, void *value) //ok ok
{
	int loc, mid, j;

	curr->bitmap = curr->bitmap - 1;
	flush_buffer(&curr->bitmap, sizeof(unsigned long), true);
	loc = Append(curr, new_item, value);
	flush_buffer(&(curr->entries[loc]), sizeof(entry), false);

	mid = Search(curr, curr->slot, new_item->key, new_item->key_len);
	
	for (j = curr->slot[0]; j >= mid; j--)
		curr->slot[j + 1] = curr->slot[j];

	curr->slot[mid] = loc;

	curr->slot[0] = curr->slot[0] + 1;
	flush_buffer(curr->slot, sizeof(curr->slot), false);
	
	curr->bitmap = curr->bitmap + 1 + (0x1UL << (loc + 1));
	flush_buffer(&curr->bitmap, sizeof(unsigned long), true);
}


int insert_in_inner_noflush(node *curr, key_item *inserted_item, void *value)	//ok ok
{
	int loc, mid, j;

	curr->bitmap = curr->bitmap - 1;
	loc = Append_in_inner(curr, inserted_item, value);

	mid = Search(curr, curr->slot, inserted_item->key, inserted_item->key_len);

	for (j = curr->slot[0]; j >= mid; j--)
		curr->slot[j + 1] = curr->slot[j];

	curr->slot[mid] = loc;

	curr->slot[0] = curr->slot[0] + 1;

	curr->bitmap = curr->bitmap + 1 + (0x1UL << (loc + 1));
	return loc;
}

void insert_in_parent(tree *t, node *curr, key_item *inserted_item, node *splitNode) { //ok ok
	if (curr == t->root) {
		node *root = allocNode();
		root->isleaf = 0;
		root->leftmostPtr = curr;
		root->bitmap = root->bitmap + (0x1UL << 1);
		root->entries[0].ptr = splitNode;
		root->entries[0].key = inserted_item;
		splitNode->parent = root;

		root->slot[1] = 0;
		root->slot[0] = 1;
		flush_buffer(root, sizeof(node), false);
		flush_buffer(splitNode, sizeof(node), false);

		curr->parent = root;
		flush_buffer(&curr->parent, 8, false);

		t->root = root;
		return ;
	}

	node *parent = curr->parent;

	if (parent->slot[0] < NODE_SIZE) {
		int mid, j, loc;

		add_log_entry(t, parent->slot, (char *)parent->entries - (char *)parent->slot, LE_DATA);
		
		parent->bitmap = parent->bitmap - 1;

		loc = Append_in_inner(parent, inserted_item, splitNode);
		flush_buffer(&(parent->entries[loc]), sizeof(entry), false);

		splitNode->parent = parent;
		flush_buffer(splitNode, sizeof(node), false);

		mid = Search(parent, parent->slot, inserted_item->key, inserted_item->key_len);

		for (j = parent->slot[0]; j >= mid; j--)
			parent->slot[j + 1] = parent->slot[j];

		parent->slot[mid] = loc;
		parent->slot[0] = parent->slot[0] + 1;

		parent->bitmap = parent->bitmap + 1 + (0x1UL << (loc + 1));
		flush_buffer(parent->slot, (char *)parent->entries - (char *)parent->slot, false);
	} else {
		int len, j, loc, cp = parent->slot[0];
		node *splitParent = allocNode();
		splitParent->isleaf = 0;

		add_log_entry(t, parent, sizeof(node), LE_DATA);

		for (j = MIN_LIVE_ENTRIES; j > 0; j--) {
			loc = Append_in_inner(splitParent, parent->entries[(int)parent->slot[cp]].key, parent->entries[(int)parent->slot[cp]].ptr);
			node *child = parent->entries[(int)parent->slot[cp]].ptr;
			add_log_entry(t, &child->parent, 8, LE_DATA);
			child->parent = splitParent;
			flush_buffer(&child->parent, 8, false);
			splitParent->slot[j] = loc;
			splitParent->slot[0]++;
			splitParent->bitmap = splitParent->bitmap + (0x1UL << (loc + 1));
			parent->bitmap = parent->bitmap & (~(0x1UL << (parent->slot[cp] + 1)));
			cp--;
		}

		parent->slot[0] -= MIN_LIVE_ENTRIES;

		len = max((splitParent->entries[(int)splitParent->slot[1]].key)->key_len, inserted_item->key_len);
		if (memcmp((unsigned char *)(splitParent->entries[(int)splitParent->slot[1]].key)->key, inserted_item->key, len) > 0) {
			loc = insert_in_inner_noflush(parent, inserted_item, splitNode);
			flush_buffer(&(parent->entries[loc]), sizeof(entry), false);
			splitNode->parent = parent;
			flush_buffer(splitNode, sizeof(node), false);
		}
		else {
			splitNode->parent = splitParent;
			flush_buffer(splitNode, sizeof(node), false);
			insert_in_inner_noflush(splitParent, inserted_item, splitNode);
		}

		flush_buffer(parent->slot, (char *)parent->entries - (char *)parent->slot, false);

		insert_in_parent(t, parent, 
				splitParent->entries[(int)splitParent->slot[1]].key, splitParent);
	}
}

key_item *make_key_item(unsigned char *key, int key_len)
{
//	key_item *new_key = malloc(sizeof(key_item) + key_len);
	key_item *new_key;
	posix_memalign((void *)&new_key, 64, sizeof(key_item) + key_len);
	new_key->key_len = key_len;
	memcpy(new_key->key, key, key_len);

	flush_buffer(new_key, sizeof(key_item) + key_len, false);

	return new_key;
}

int assoc_insert(item *it) {	//ok ok
	int numEntries;
	node *curr = T->root;
	/* Find proper leaf */
	curr = find_leaf_node(curr, (unsigned char *)ITEM_key(it), it->nkey);

	/* Make new key item */
	key_item *new_item = make_key_item((unsigned char *)ITEM_key(it), it->nkey);

	/* Check overflow & split */
	numEntries = curr->slot[0];
	if (numEntries == NODE_SIZE) {
		split_count++;
		add_log_entry(T, curr, sizeof(node), LE_DATA);
		node *splitNode = allocNode();
		int len, j, loc, cp = curr->slot[0];
		splitNode->leftmostPtr = curr->leftmostPtr;

		//overflown node
		for (j = MIN_LIVE_ENTRIES; j > 0; j--) {
			loc = Append_in_inner(splitNode, curr->entries[(int)curr->slot[cp]].key,
					curr->entries[(int)curr->slot[cp]].ptr);
			splitNode->slot[j] = loc;
			splitNode->slot[0]++;
			splitNode->bitmap = splitNode->bitmap + (0x1UL << (loc + 1));
			curr->bitmap = curr->bitmap & (~(0x1UL << (curr->slot[cp] + 1)));
			cp--;
		}

		curr->slot[0] -= MIN_LIVE_ENTRIES;

		len = max((splitNode->entries[(int)splitNode->slot[1]].key)->key_len, new_item->key_len);	// [kh] ?
		if (memcmp((unsigned char *)(splitNode->entries[(int)splitNode->slot[1]].key)->key, (unsigned char *)new_item->key, len) > 0) {
			loc = insert_in_leaf_noflush(curr, new_item, (void *)it);
			flush_buffer(&(curr->entries[loc]), sizeof(entry), false);
		}
		else
			insert_in_leaf_noflush(splitNode, new_item, (void *)it);

		insert_in_parent(T, curr, splitNode->entries[(int)splitNode->slot[1]].key, splitNode);

		curr->leftmostPtr = splitNode;
		
		flush_buffer(curr->slot, (char *)curr->entries - (char *)curr->slot, false);
		flush_buffer(&curr->leftmostPtr, 8, false);

		add_log_entry(T, NULL, 0, LE_COMMIT);
	}
	else{
		insert_in_leaf(curr, new_item, (void *)it);
	}

	return 1;
}


void assoc_delete(const char *_key, const size_t nkey) {			//ok ok
	unsigned char *key = (unsigned char*)_key;
	int i, loc, key_len = (int)nkey;
	node *curr = T->root;

	curr = find_leaf_node(curr, key, key_len);
	if (curr->slot[0] == 0)
		return ;

	int mid = Search(curr, curr->slot, key, key_len);
	
	if (mid > curr->slot[0])
		mid = curr->slot[0];
/*
	if (!curr->entries[(int)curr->slot[mid]].it) {// [kh] I added
		printf("[assoc_delete] error!\n");
		return;
	}
*/
	if (key_len != (curr->entries[(int)curr->slot[mid]].key)->key_len)
		return;

	if (memcmp((unsigned char *)(curr->entries[(int)curr->slot[mid]].key)->key, (unsigned char *)key, key_len) != 0 || mid > curr->slot[0])
		return;

	curr->bitmap = curr->bitmap - 1;
	loc = curr->slot[mid];
	for (i = mid; i < curr->slot[0]; i++)
		curr->slot[i] = curr->slot[i + 1];

	curr->slot[0] = curr->slot[0] - 1;

	curr->bitmap = curr->bitmap + 1 - (0x1UL << (loc + 1));

	return;  	
}
