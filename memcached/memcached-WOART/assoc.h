/* associative array */
void assoc_init(const int hashpower_init);
item *assoc_find(const char *key, const size_t nkey);
int assoc_insert(item *item);
void assoc_delete(const char *key, const size_t nkey);

extern unsigned int hashpower;
extern unsigned int item_lock_hashpower;


/* W_RADIX_TREE_H */
#include <stdbool.h>
#include <stdint.h>


#define NODE4   1
#define NODE16  2
#define NODE48  3
#define NODE256 4

#define MAX_PREFIX_LEN	    6
#define BITS_PER_LONG		64
#define CACHE_LINE_SIZE 	64


#if defined(__GNUC__) && !defined(__clang__)
# if __STDC_VERSION__ >= 199901L && 402 == (__GNUC__ * 100 + __GNUC_MINOR__)
/*
 * GCC 4.2.2's C99 inline keyword support is pretty broken; avoid. Introduced in
 * GCC 4.2.something, fixed in 4.3.0. So checking for specific major.minor of
 * 4.2 is fine.
 */
#  define BROKEN_GCC_C99_INLINE
# endif
#endif


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

typedef struct {
	unsigned char depth;
	unsigned char partial_len;
	unsigned char partial[MAX_PREFIX_LEN];
} path_comp;

typedef struct {
    uint8_t type;
	path_comp path;
} art_node;

typedef struct {
	unsigned char key;
	char i_ptr;
} slot_array;

typedef struct {
	unsigned long k_bits : 16;
	unsigned long p_bits : 48;
} node48_bitmap;

/**
 * Small node with only 4 children, but
 * 8byte slot array field.
 */
typedef struct {
    art_node n;
	slot_array slot[4];
    art_node *children[4];
} art_node4;

/**
 * Node with 16 keys and 16 children, and
 * a 8byte bitmap field
 */
typedef struct {
    art_node n;
	unsigned long bitmap;
    unsigned char keys[16];
    art_node *children[16];
} art_node16;

/**
 * Node with 48 children and a full 256 byte field,
 * but a 128 byte bitmap field 
 * (4 bitmap group of 16 keys, 48 children bitmap)
 */
typedef struct {
    art_node n;
	node48_bitmap bits_arr[16];
    unsigned char keys[256];
    art_node *children[48];
} art_node48;

/**
 * Full node with 256 children
 */
typedef struct {
    art_node n;
    art_node *children[256];
} art_node256;

/*
typedef struct {
    void *value;
    uint32_t key_len;	
	unsigned char key[];
} art_leaf;
*/

/**
 * Main struct, points to root.
 */
typedef struct {
    art_node *root;
    uint64_t size;
} art_tree;



int art_tree_init(art_tree *t);

#define init_art_tree(...) art_tree_init(__VA_ARGS__)


item* art_minimum(art_tree *t);
item* art_maximum(art_tree *t);

/* flush functions*/
void flush_buffer(void *buf, unsigned long len, bool fence);
void flush_buffer_nocount(void *buf, unsigned long len, bool fence);
unsigned long find_next_bit(const unsigned long *addr, unsigned long size,
			    unsigned long offset);
unsigned long find_next_zero_bit(const unsigned long *addr, unsigned long size,
		unsigned long offset);



