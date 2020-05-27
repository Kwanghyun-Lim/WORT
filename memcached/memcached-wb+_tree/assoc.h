/* associative array */
void assoc_init(const int hashpower_init);
item *assoc_find(const char *key, const size_t nkey, const uint32_t hv);
int assoc_insert(item *item, const uint32_t hv);
void assoc_delete(const char *key, const size_t nkey, const uint32_t hv);
void do_assoc_move_next_bucket(void);
void stop_assoc_maintenance_thread(void);
extern unsigned int hashpower;
extern unsigned int item_lock_hashpower;


/* W_B+_TREE_H */

#define NODE_SIZE 7
#define min_live_entries 3
#define CACHE_LINE_SIZE 64

#define LE_DATA		0
#define LE_COMMIT	1

typedef struct entry entry;
typedef struct node node;
typedef struct tree tree;
typedef struct redo_log_entry redo_log_entry;
typedef struct commit_entry commit_entry;

struct redo_log_entry {
	unsigned long addr;
	unsigned long new_value;
	unsigned char type;
};

struct commit_entry {
	unsigned char type;
};

struct entry{
	unsigned long key;
	void *ptr;
};

struct node{
	char slot[NODE_SIZE+1];
	struct entry entries[NODE_SIZE];
	struct node *leftmostPtr;
	struct node* parent;
	int isleaf;
	char dummy[52];
};

struct tree{
	node *root;
	int height;
};


/* flush functions*/
void flush_buffer(void *buf, unsigned long len, bool fence);

/* structure allocation functions */
tree *initTree(void); 
node *allocNode(void);
// [REDO, COMMIT] logging functions 
void add_redo_logentry(void);
void add_commit_entry(void);


/* [INSERT] linking functions */
//int Insert(tree **t, unsigned long key, item *value);

void insert_in_parent(tree *t, node *curr, unsigned long key, node *splitNode);

void insert_in_inner(node *curr, unsigned long key, void *value);

int insert_in_inner_noflush(node *curr, unsigned long key, void *value);

int insert_in_leaf_noflush(node *curr, unsigned long key, void *value); /*[KH] why return types are diff? */
void insert_in_leaf(node *curr, unsigned long key, void *value);



/* [SEARCH] searching functions */
//void *Lookup(tree *t, unsigned long key);
int Search_link(node *curr, char *temp, unsigned long key, char *valid);
int Search(node *curr, char *temp, unsigned long key);
node *find_leaf_node(node *curr, unsigned long key);

/* [PRINT] printing function */
void printNode(node *n);


/* I don't know yet */
/* allocation functions? */
int Append(node *n, unsigned long key, void *value); 
int Append_in_inner(node *n, unsigned long key, void *value);
int Append_split(node *n, unsigned long key, void *value);




