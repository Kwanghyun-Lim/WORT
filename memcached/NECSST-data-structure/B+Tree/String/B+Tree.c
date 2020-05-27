#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <x86intrin.h>
#include <malloc.h>
#include <stdint.h>
#include <time.h>
#include "B+Tree.h"

#define mfence() asm volatile("mfence":::"memory")

unsigned long IN_count = 0;
unsigned long LN_count = 0;

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

	return new_key;
}

LN *allocLNode()
{
	LN *node = malloc(sizeof(LN));
	node->type = THIS_LN;
	node->nKeys = 0;
	LN_count++;
	return node;
}

IN *allocINode()
{
	IN *node = malloc(sizeof(IN));
	node->type = THIS_IN;
	node->nKeys = 0;
	IN_count++;
	return node;
}

tree *initTree()
{
	tree *t =malloc(sizeof(tree)); 
	t->root = allocLNode();
	((LN *)t->root)->pNext = NULL;
	return t;
}

void *Lookup(tree *t, unsigned char *key, int key_len)
{
	int loc = 0;
	void *value = NULL;
	LN *curr = t->root;
	curr = find_leaf_node(curr, key, key_len);

	loc = Search((IN *)curr, key, key_len);

	if (memcmp((curr->keys[loc])->key, key, max((curr->keys[loc])->key_len, key_len)) == 0)
		value = curr->ptr[loc];
	return value;
}
/*
void Range_Lookup(tree *t, unsigned long start_key, unsigned int num, 
		unsigned long buf[])
{
	unsigned long i, search_count = 0;
	LN *curr = t->root;
	curr = find_leaf_node(curr, start_key);

	while (curr != NULL) {
		for (i = 0; i < curr->nKeys; i++) {
			buf[search_count] = *(unsigned long *)curr->ptr[i];
			search_count++;
			if (search_count == num)
				return ;
		}
		curr = curr->pNext;
	}
}
*/
int Search(IN *curr, unsigned char *key, int key_len)
{
	int low = 0, mid = 0;
	int high = curr->nKeys - 1;
	int len, decision;

	while (low <= high){
		mid = (low + high) / 2;
		len = max((curr->keys[mid])->key_len, key_len);
		decision = memcmp((curr->keys[mid])->key, key, len);
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

void *find_leaf_node(void *curr, unsigned char *key, int key_len) 
{
	unsigned long loc;

	if (((LN *)curr)->type == THIS_LN) 
		return curr;
	loc = Search(curr, key, key_len);

	if (loc > ((IN *)curr)->nKeys - 1) 
		return find_leaf_node(((IN *)curr)->ptr[loc - 1], key, key_len);
	else if (memcmp((((IN *)curr)->keys[loc])->key, key, max((((IN *)curr)->keys[loc])->key_len, key_len)) <= 0) 
		return find_leaf_node(((IN *)curr)->ptr[loc], key, key_len);
	else if (loc == 0) 
		return find_leaf_node(((IN *)curr)->leftmostPtr, key, key_len);
	else 
		return find_leaf_node(((IN *)curr)->ptr[loc - 1], key, key_len);
}

void Insert(tree *t, unsigned char *key, int key_len, void *value)
{
	LN *curr = t->root;
	/* Find proper leaf */
	curr = find_leaf_node(curr, key, key_len);

	/* Make new key item */
	key_item *new_item = make_key_item(key, key_len);

	/* Check overflow & split */
	if (curr->nKeys < NUM_LN_ENTRY) {
		insert_in_leaf(curr, new_item, value);
	} else {
		int i, j, loc, curr_nKeys;
		LN *split_LNode = allocLNode();
		curr_nKeys = curr->nKeys;

		for (j = MIN_LN_ENTRIES, i = 0; j < curr_nKeys; j++, i++) {
			split_LNode->keys[i] = curr->keys[j];
			split_LNode->ptr[i] = curr->ptr[j];
			split_LNode->nKeys++;
			curr->nKeys--;
		}

		if (memcmp((split_LNode->keys[0])->key, key, max((split_LNode->keys[0])->key_len, key_len)) > 0)
			insert_in_leaf(curr, new_item, value);
		else
			insert_in_leaf(split_LNode, new_item, value);

		insert_in_parent(t, curr, split_LNode->keys[0], split_LNode);

		split_LNode->pNext = curr->pNext;
		curr->pNext = split_LNode;
	}
}

void insert_in_leaf(LN *curr, key_item *new_item, void *value)
{
	int loc, mid, j;

	mid = Search((IN *)curr, new_item->key, new_item->key_len);

	for (j = (curr->nKeys - 1); j >= mid; j--) {
		curr->keys[j + 1] = curr->keys[j];
		curr->ptr[j + 1] = curr->ptr[j];
	}

	curr->keys[mid] = new_item;
	curr->ptr[mid] = value;

	curr->nKeys++;
}

void insert_in_inner(IN *curr, key_item *inserted_item, void *child)
{
	int loc, mid, j;

	mid = Search(curr, inserted_item->key, inserted_item->key_len);

	for (j = (curr->nKeys - 1); j >= mid; j--) {
		curr->keys[j + 1] = curr->keys[j];
		curr->ptr[j + 1] = curr->ptr[j];
	}

	curr->keys[mid] = inserted_item;
	curr->ptr[mid] = child;

	curr->nKeys++;
}

void insert_in_parent(tree *t, void *curr, key_item *inserted_item, void *splitNode) {
	if (curr == t->root) {
		IN *root = allocINode();
		root->leftmostPtr = curr;
		root->keys[0] = inserted_item;
		root->ptr[0] = splitNode;
		root->nKeys++;

		((IN *)splitNode)->parent = root;
		((IN *)curr)->parent = root;
		t->root = root;
		return ;
	}

	IN *parent;

	if (((IN *)curr)->type == THIS_IN)
		parent = ((IN *)curr)->parent;
	else
		parent = ((LN *)curr)->parent;

	if (parent->nKeys < NUM_IN_ENTRY) {
		insert_in_inner(parent, inserted_item, splitNode);
		((IN *)splitNode)->parent = parent;
	} else {
		int i, j, loc, parent_nKeys;
		IN *split_INode = allocINode();
		parent_nKeys = parent->nKeys;

		for (j = MIN_IN_ENTRIES, i = 0; j < parent_nKeys; j++, i++) {
			split_INode->keys[i] = parent->keys[j];
			split_INode->ptr[i] = parent->ptr[j];
			((IN *)split_INode->ptr[i])->parent = split_INode;
			split_INode->nKeys++;
			parent->nKeys--;
		}

		if (memcmp((split_INode->keys[0])->key, inserted_item->key,
					max((split_INode->keys[0])->key_len, inserted_item->key_len)) > 0) {
			insert_in_inner(parent, inserted_item, splitNode);
			((IN *)splitNode)->parent = parent;
		}
		else {
			((IN *)splitNode)->parent = split_INode;
			insert_in_inner(split_INode, inserted_item, splitNode);
		}

		insert_in_parent(t, parent, split_INode->keys[0], split_INode);
	}
}

/*
void *Update(tree *t, unsigned long key, void *value)
{
	unsigned long loc = 0;
	LN *curr = t->root;
	curr = find_leaf_node(curr, key);

	while (loc < NUM_LN_ENTRY) {
		loc = find_next_bit(&curr->bitmap, BITMAP_SIZE, loc);
		if (loc == BITMAP_SIZE)
			break;
		
		if (curr->fingerprints[loc] == hash(key) &&
				curr->entries[loc].key == key) {
			curr->entries[loc].ptr = value;
			flush_buffer(&curr->entries[loc].ptr, 8, true);
			return curr->entries[loc].ptr;
		}
		loc++;
	}

	return NULL;
}
*/

/*
int delete_in_leaf(node *curr, unsigned long key)
{
	int mid, j;

	curr->bitmap = curr->bitmap - 1;
	flush_buffer(&curr->bitmap, sizeof(unsigned long), true);

	mid = Search(curr, curr->slot, key);

	for (j = curr->slot[0]; j > mid; j--)
		curr->slot[j - 1] = curr->slot[j];

	curr->slot[0] = curr->slot[0] - 1;

	flush_buffer(curr->slot, sizeof(curr->slot), true);
	
	curr->bitmap = curr->bitmap + 1 - (0x1UL << (mid + 1));
	flush_buffer(&curr->bitmap, sizeof(unsigned long), true);
	return 0;
}

int Delete(tree *t, unsigned long key)
{
	int numEntries, errval = 0;
	node *curr = t->root;
	
	curr = find_leaf_node(curr, key);

	numEntries = curr->slot[0];
	if (numEntries <= 1)
		errval = -1;
	else
		errval = delete_in_leaf(curr, key);

	return errval;
}
*/
