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
//#include "/home/sekwon/study/quartz/src/lib/pmalloc.h"

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
#include <sys/time.h>
#include <stdint.h>

#define mfence() asm volatile("mfence":::"memory")

unsigned long IN_count = 0;
unsigned long LN_count = 0;

static inline int max(int a, int b) {
	return (a > b) ? a : b;
}

static tree *T;

key_item *make_key_item(unsigned char *key, int key_len)
{
	key_item *new_key = malloc(sizeof(key_item) + key_len);
	new_key->key_len = key_len;
	memcpy(new_key->key, key, key_len);

	return new_key;
}

LN *allocLNode(void)
{
	LN *node = malloc(sizeof(LN));
	node->type = THIS_LN;
	node->nKeys = 0;
	LN_count++;
	return node;
}

IN *allocINode(void)
{
	IN *node = malloc(sizeof(IN));
	node->type = THIS_IN;
	node->nKeys = 0;
	IN_count++;
	return node;
}

void initTree(void)
{
	T =malloc(sizeof(tree)); 
	T->root = allocLNode();
	((LN *)T->root)->pNext = NULL;
	return;
}

void assoc_init(const int hashtable_init) {  /*[KH] The variable "hashtable_init" is not used as real */
   printf("[assoc_init] B+ tree\n");
	initTree();
   printf("[assoc_init] B+ tree \n");
}

item *assoc_find(const char *_key, const size_t nkey) {
//	printf("[assoc_find]\n");
	unsigned char *key = (unsigned char *)_key;
	int key_len =(int)nkey;
	int loc = 0;
	LN *curr = T->root;
	curr = find_leaf_node(curr, key, key_len);
	if (curr == NULL || curr->nKeys == 0)
		return NULL;

	//[kh] In this part, NULL handling is required
	loc = Search((IN *)curr, key, key_len);

	if (loc >= 0 && loc < curr->nKeys) {
		if (memcmp((curr->keys[loc])->key, key, max((curr->keys[loc])->key_len, key_len)) == 0)
			return (item *)curr->ptr[loc];
	}
    
    return NULL;
}

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
	if (curr == NULL)
		return NULL;
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

int assoc_insert(item *it) {	//ok	//ok
	unsigned char *key = (unsigned char *)ITEM_key(it);
	int key_len = it->nkey;
	void *value = (void *)it;

	LN *curr = T->root;
	/* Find proper leaf */
	curr = find_leaf_node(curr, key, key_len);

	/* Make new key item */
	key_item *new_item = make_key_item(key, key_len);

	/* Check overflow & split */
	if (curr->nKeys < NUM_LN_ENTRY) {
		insert_in_leaf(curr, new_item, value);
	} else {
		int i, j, curr_nKeys;
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

		insert_in_parent(T, curr, split_LNode->keys[0], split_LNode);

		split_LNode->pNext = curr->pNext;
		curr->pNext = split_LNode;
	}

	return 1;
}

void insert_in_leaf(LN *curr, key_item *new_item, void *value)
{
	int mid, j;

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
	int mid, j;

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
		int i, j, parent_nKeys;
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

void assoc_delete(const char *_key, const size_t nkey) {
	unsigned char *key = (unsigned char *)_key;
	int key_len =(int)nkey;
	unsigned long loc = 0;
	LN *curr = T->root;
	curr = find_leaf_node(curr, key, key_len);
	if (curr == NULL || curr->nKeys == 0)
		return ;

	loc = Search((IN *)curr, key, key_len);

	if (loc >= 0 && loc < curr->nKeys) {
		if (memcmp((curr->keys[loc])->key, key, max((curr->keys[loc])->key_len, key_len)) == 0) {
			int i;
			for (i = loc; i < curr->nKeys - 1; i++) {
				curr->keys[i] = curr->keys[i + 1];
				curr->ptr[i] = curr->ptr[i + 1];
			}
			curr->nKeys--;
			return ;
		}
	}
	return ;
}
