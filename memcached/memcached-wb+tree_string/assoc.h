/* associative array */
void assoc_init(const int hashpower_init);
item *assoc_find(const char *key, const size_t nkey);
int assoc_insert(item *item);
void assoc_delete(const char *key, const size_t nkey);

extern unsigned int hashpower;
extern unsigned int item_lock_hashpower;


/* wB+_TREE_H */
#include <stdbool.h>
#include <stdint.h>


#define CACHE_LINE_SIZE 64
#define NODE_SIZE 			63
#define SLOT_SIZE 			NODE_SIZE + 1
#define MIN_LIVE_ENTRIES 	NODE_SIZE / 2

#define LOG_DATA_SIZE		48
#define LOG_AREA_SIZE		4194304
#define LE_DATA			0
#define LE_COMMIT		1

#define BITS_PER_LONG	64
#define BITMAP_SIZE		NODE_SIZE + 1

typedef struct entry entry;
typedef struct node node;
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

struct node{
	char slot[SLOT_SIZE];
	unsigned long bitmap;
	struct entry entries[NODE_SIZE];
	struct node *leftmostPtr;
	struct node *parent;
	int isleaf;		//[kh] need to check
//	char dummy[32];		//15
//	char dummy[16];		//31
	char dummy[48];		//63
};

struct tree{
	node *root;
	log_area *start_log;
};


void flush_buffer_nocount(void *buf, unsigned long len, bool fence);
void flush_buffer(void *buf, unsigned long len, bool fence);
void add_log_entry(tree *t, void *addr, unsigned int size, unsigned char type);
void initTree(void);
node *allocNode(void);
int Search(node *curr, char *temp, unsigned char *key, int key_len);
node *find_leaf_node(node *curr, unsigned char *key, int key_len);
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
		unsigned long offset);
int Append(node *n, key_item *new_item, void *value);
int Append_in_inner(node *n, key_item *inserted_item, void *value);
int insert_in_leaf_noflush(node *curr, key_item *new_item, void *value);
void insert_in_leaf(node *curr, key_item *new_item, void *value);
int insert_in_inner_noflush(node *curr, key_item *inserted_item, void *value);
void insert_in_parent(tree *t, node *curr, key_item *inserted_item, node *splitNode);
key_item *make_key_item(unsigned char *key, int key_len);

static inline unsigned long ffz(unsigned long word)
{
	asm("rep; bsf %1,%0"
		: "=r" (word)
		: "r" (~word));
	return word;
}

