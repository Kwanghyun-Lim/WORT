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

#define NUM_LN_ENTRY	32
#define NUM_IN_ENTRY	128
#define MIN_IN_ENTRIES (NUM_IN_ENTRY / 2)
#define MIN_LN_ENTRIES (NUM_LN_ENTRY / 2)
#define CACHE_LINE_SIZE 64
#define IS_FULL		((0x1UL << NUM_LN_ENTRY) - 1)
#define THIS_IN		1
#define THIS_LN		2

#define LOG_DATA_SIZE		48
#define LOG_AREA_SIZE		4194304
#define LE_DATA			0
#define LE_COMMIT		1

#define BITS_PER_LONG	64
#define BITMAP_SIZE		NUM_LN_ENTRY

typedef struct entry entry;
typedef struct Internal_Node IN;
typedef struct Leaf_Node LN;
typedef struct tree tree;


typedef struct {
	unsigned int size;
	unsigned char type;
	void *addr;
	char data[LOG_DATA_SIZE];
} log_entry;

typedef struct {
	log_entry *next_offset;
	char log_data[LOG_AREA_SIZE];
} log_area;

typedef struct {
	int key_len;
	unsigned char key[];
} key_item;

struct entry{
	key_item *key;
	void *ptr;
};

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
	unsigned long bitmap;
	LN *pNext;
	unsigned char fingerprints[NUM_LN_ENTRY];
	entry entries[NUM_LN_ENTRY];
};

struct tree {
	void *root;
	log_area *start_log;
};


/* flush functions*/
void flush_buffer(void *buf, unsigned long len, bool fence);
void flush_buffer_nocount(void *buf, unsigned long len, bool fence);
void add_log_entry(tree *t, void *addr, unsigned int size, unsigned char type);
key_item *make_key_item(unsigned char *key, int key_len);
LN *allocLNode(void);
IN *allocINode(void);
void initTree(void);
unsigned char FP_hash(unsigned char *key, int key_len);
void insertion_sort(entry *base, int num);
unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
			    unsigned long offset);
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
		unsigned long offset);
int Search(IN *curr, unsigned char *key, int key_len);
void *find_leaf_node(void *curr, unsigned char *key, int key_len);
int insert_in_leaf_noflush(LN *curr, key_item *new_item, void *value);
void insert_in_leaf(LN *curr, key_item *new_item, void *value);
void insert_in_inner(IN *curr, key_item *inserted_item, void *child);
void insert_in_parent(tree *t, void *curr, key_item *inserted_item, void *splitNode);

static inline unsigned long __ffs(unsigned long word)
{
	asm("rep; bsf %1,%0"
		: "=r" (word)
		: "rm" (word));
	return word;
}

static inline unsigned long ffz(unsigned long word)
{
	asm("rep; bsf %1,%0"
		: "=r" (word)
		: "r" (~word));
	return word;
}



