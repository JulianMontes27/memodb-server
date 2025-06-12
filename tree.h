#ifndef TREE_H
#define TREE_H

// Standard includes
#include <stdio.h>   // printf, perror
#include <unistd.h>  // access, sleep, write
#include <stdlib.h>  // malloc, free, NULL
#include <string.h>  // memset, snprintf, strlen, strncpy
#include <stdint.h>  // uint8_t, int16_t
#include <assert.h>  // assert macro
#include <errno.h>   // error definitions
#include <stdbool.h> // boolean
#include <time.h>    // timing functions

// Runtime type tags for Tree union discrimination
#define TagRoot 1 // Root of database tree
#define TagNode 2 // Internal tree node
#define TagLeaf 3 // Data-holding leaf node

// Convenience macros
// These currently use linear search; consider optimizing with more complex tree logic later.
#define find_node(path) find_node_linear(path)
#define find_leaf(path, key) find_leaf_linear(path, key)
#define find_last(parent) find_last_linear(parent)
#define lookup(path, key) lookup_linear(path, key)

#define NoError 0 // Generic success return code.

// Return NULL and set errno safely in any context. This macro is
// used by the tree functions (e.g., create_node, create_leaf) when they return a pointer.
#define reterr(x)    \
    do               \
    {                \
        errno = (x); \
        return NULL; \
    } while (0)

// Fixed Print macro - now suppresses unused result warning for write() and ensures null-termination.
//     * **Fix**: In the `Print` macro, changed `strncpy((char *)buf, (char *)(x), 255);` to `snprintf((char *)print_buf, sizeof(print_buf), "%s", (char *)(x));`. `snprintf` is safer as it guarantees null-termination if the buffer is large enough, and truncates safely otherwise. This is applied to all uses of `Print` in `tree.c` by default now. Your existing `create_node` and `create_leaf` functions already used `snprintf` correctly for `node->path` and `leaf->key` fields, which is great.

#define Print(fd, x)                                                       \
    do                                                                     \
    {                                                                      \
        uint8_t print_buf[256]; /* Buffer for printing */                  \
        uint16_t print_size;                                               \
        /* Use snprintf to safely copy and null-terminate, then write */   \
        snprintf((char *)print_buf, sizeof(print_buf), "%s", (char *)(x)); \
        print_size = (uint16_t)strlen((char *)print_buf);                  \
        if (print_size > 0)                                                \
        {                                                                  \
            /* Cast to void to suppress "ignoring return value" warning */ \
            (void)write((fd), (char *)print_buf, print_size);              \
        }                                                                  \
    } while (0)

typedef unsigned char Tag;

// Forward declarations for circular references
struct s_node;
struct s_leaf;
union u_tree;

struct s_leaf
{
    union u_tree *west;  // Link to preceding Tree element (usually its parent Node)
    struct s_leaf *east; // Next leaf in chain (forms a singly linked list of leaves)
    int8_t key[128];     // Fixed 128-byte key
    int8_t *value;       // Dynamic value data (allocated on heap)
    int16_t size;        // Value size in bytes
    Tag tag;             // Type discriminator (TagLeaf)
};
typedef struct s_leaf Leaf;

struct s_node
{
    struct s_node *north; // Parent node (if not root)
    struct s_node *west;  // Child node for sub-paths (forms a linked list of sibling nodes)
    struct s_leaf *east;  // First associated leaf (head of a linked list of leaves for this node)
    uint8_t path[256];    // Path segment (256 bytes)
    Tag tag;              // Type discriminator (TagNode/TagRoot)
};
typedef struct s_node Node;

union u_tree
{
    Node node; // Represents an internal node or the root of the tree
    Leaf leaf; // Represents a data-holding leaf node
};
typedef union u_tree Tree;

// Global declarations
extern Tree root; // The global root of the in-memory database tree

// Function prototypes for tree operations (implemented in tree.c)
uint8_t *indent(uint8_t);
void zero(uint8_t *ptr, uint16_t size);
Node *create_node(Node *parent, int8_t *path);
Leaf *create_leaf(Tree *west, uint8_t *key, uint8_t *value, uint16_t size);
Leaf *find_leaf_linear(int8_t *path, int8_t *key);
Node *find_node_linear(int8_t *path);
Leaf *find_last_linear(Node *parent);
int8_t *lookup_linear(int8_t *path, int8_t *key);
void print_tree(uint8_t fd, Tree *root);

// --- NEW: Prototypes for memory management functions ---
void free_leaf(Leaf *leaf);
void free_node_and_leaves(Node *node);
void free_tree(Tree *root);

#endif /* TREE_H */
