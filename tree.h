#ifndef TREE_H
#define TREE_H

// Enable GNU extensions (should be defined before includes)
#define _GNU_SOURCE

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

// Abstracts last element finding (can swap implementations)
#define find_last(x) find_last_linear(x)
#define find_leaf(x, y) find_leaf_linear(x, y)
#define find_node(x) find_node_linear(x)
#define lookup(x, y) lookup_linear(x, y)

#define NoError 0

// Return NULL and set errno safely in any context
#define reterr(x)    \
    do               \
    {                \
        errno = (x); \
        return NULL; \
    } while (0)

// Fixed Print macro - added missing semicolon and proper block structure
#define Print(x)                                \
    do                                          \
    {                                           \
        memset(buf, 0, 256);                    \
        strncpy((char *)buf, (char *)(x), 255); \
        size = (uint16_t)strlen((char *)buf);   \
        if (size)                               \
        {                                       \
            write(fd, (char *)buf, size);       \
        }                                       \
    } while (0)

typedef unsigned char Tag;

// Forward declarations for circular references
struct s_node;
struct s_leaf;
union u_tree;

/**
 * Leaf node holding key-value data in east-linked list
 */
struct s_leaf
{
    union u_tree *west;  // Link to preceding Tree element
    struct s_leaf *east; // Next leaf in chain
    int8_t key[128];     // Fixed 128-byte key
    int8_t *value;       // Dynamic value data
    int16_t size;        // Value size in bytes
    Tag tag;             // Type discriminator (TagLeaf)
};
typedef struct s_leaf Leaf;

/**
 * Internal node organizing tree hierarchy
 */
struct s_node
{
    struct s_node *north; // Parent node
    struct s_node *west;  // Child node for sub-paths
    struct s_leaf *east;  // First associated leaf
    uint8_t path[256];    // Path segment (256 bytes)
    Tag tag;              // Type discriminator (TagNode/TagRoot)
};
typedef struct s_node Node;

/**
 * Union for Node/Leaf polymorphism - use tag field to determine active member
 */
union u_tree
{
    Node node;
    Leaf leaf;
};
typedef union u_tree Tree;

// Function prototypes

// Indenting function
uint8_t *indent(uint8_t);

// Zero out memory block
void zero(uint8_t *ptr, uint16_t size);

// Create new node as west child of parent
Node *create_node(Node *parent, int8_t *path);

// Create new leaf linked to west tree element
Leaf *create_leaf(Tree *west, uint8_t *key, uint8_t *value, uint16_t size);

Leaf *find_leaf_linear(int8_t *path, int8_t *key);

Node *find_node_linear(int8_t *path);

Leaf *find_last_linear(Node *parent);

int8_t *lookup_linear(int8_t *path, int8_t *key);

// Print the in-memory Tree structure for a given file descriptor
void print_tree(uint8_t fd, Tree *root);

// Database server entry point
int main(int argc, const char *argv[]);

#endif /* TREE_H */
