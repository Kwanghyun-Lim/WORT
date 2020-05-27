/* associative array */
void assoc_init(const int hashpower_init);
item *assoc_find(const char *key, const size_t nkey);
int assoc_insert(item *item);
void assoc_delete(const char *key, const size_t nkey);

extern unsigned int hashpower;
extern unsigned int item_lock_hashpower;


/* FP_TREE_H */
#include <stdbool.h>
#include <stdint.h>

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

key_item *make_key_item(unsigned char *key, int key_len);
LN *allocLNode(void);
IN *allocINode(void);
void initTree(void);
int Search(IN *curr, unsigned char *key, int key_len);
void *find_leaf_node(void *curr, unsigned char *key, int key_len);
void insert_in_leaf(LN *curr, key_item *new_item, void *value);
void insert_in_inner(IN *curr, key_item *inserted_item, void *child);
void insert_in_parent(tree *t, void *curr, key_item *inserted_item, void *splitNode);
