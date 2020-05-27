#include <stdbool.h>
#define NUM_LN_ENTRY	254
#define NUM_IN_ENTRY	254
#define MIN_IN_ENTRIES (NUM_IN_ENTRY / 2)
#define MIN_LN_ENTRIES (NUM_LN_ENTRY / 2)
#define CACHE_LINE_SIZE 64
#define THIS_IN		1
#define THIS_LN		2

typedef struct Internal_Node IN;
typedef struct Leaf_Node LN;
typedef struct tree tree;

unsigned long IN_count;
unsigned long LN_count;

typedef struct {
	int key_len;
	unsigned char key[];
} key_item;

struct Internal_Node {
	unsigned char type;
	IN *parent;
	unsigned int nKeys;
	key_item *keys[NUM_IN_ENTRY];
	void *leftmostPtr;
	void *ptr[NUM_IN_ENTRY];
};

struct Leaf_Node {
	unsigned char type;
	IN *parent;
	unsigned int nKeys;
	key_item *keys[NUM_LN_ENTRY];
	LN *pNext;
	void *ptr[NUM_LN_ENTRY];
};

struct tree {
	void *root;
};

void flush_buffer_nocount(void *buf, unsigned long len, bool fence);
tree *initTree();
void Range_Lookup(tree *t, unsigned long start_key, unsigned int num, 
		unsigned long buf[]);
void *Lookup(tree *t, unsigned char *key, int key_len);
int Search(IN *curr, unsigned char *key, int key_len);
void *find_leaf_node(void *curr, unsigned char *key, int key_len);
void Insert(tree *t, unsigned char *key, int key_len, void *value);
void *Update(tree *t, unsigned long key, void *value);
void insert_in_leaf(LN *curr, key_item *new_item, void *value);
void insert_in_inner(IN *curr, key_item *inserted_item, void *value);
void insert_in_parent(tree *t, void *curr, key_item *inserted_item, void *splitNode);
int Delete(tree *t, unsigned long key);
