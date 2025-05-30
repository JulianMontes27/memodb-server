#ifndef MAIN_H
#define MAIN_H

// =============================================================================
// Compiler-Specific Features
// =============================================================================
// _GNU_SOURCE: Enables GNU extensions for various standard library functions.
//              Consider removing if strict portability to non-GNU systems is required.
#define _GNU_SOURCE

// =============================================================================
// Standard Library Includes
// =============================================================================
#include <stdio.h>    // For standard input/output functions (e.g., printf, perror)
#include <unistd.h>   // For POSIX operating system API (e.g., access, sleep)
#include <stdlib.h>   // For general utilities (e.g., malloc, free, NULL)
#include <string.h>   // For string manipulation functions (e.g., memset, snprintf)
#include <stdint.h>   // For fixed-width integer types (e.g., uint8_t, int16_t)
#include <assert.h>   // For the assert() macro, used for debugging and pre-condition checks
#include <errno.h>    // For error number definitions (e.g., EFAULT, ENOMEM)

// =============================================================================
// Database Node Tag Definitions
// =============================================================================
// These tags define the type of structure (Tree, Node, or Leaf) at runtime,
// allowing for flexible interpretation of the `Tree` union.
#define TagRoot 1 /* Represents the root of the database tree */
#define TagNode 2 /* Represents an internal node in the tree structure */
#define TagLeaf 3 /* Represents a leaf node, holding actual data entries */

// =============================================================================
// Macro Definitions
// =============================================================================

// find_last: A macro to abstract the underlying implementation of finding the last element.
// Currently maps to find_last_linear, but can be changed to a more efficient algorithm later.
#define find_last(x) find_last_linear(x)

// NoError: A custom error code indicating successful operation or no error.
// Typically used with the `errno` variable.
#define NoError 0

// reterr: A macro for returning NULL from a function and setting the global `errno` variable.
// This is designed for functions that return a pointer type.
// It uses a do-while(0) loop to ensure safe expansion in all contexts (e.g., if-else statements).
#define reterr(x) \
    do { \
        errno = (x); \
        return NULL; \
    } while(0)

// =============================================================================
// Type Definitions
// =============================================================================

// Tag: An unsigned character type used for runtime type identification of tree components.
typedef unsigned char Tag;

// Forward declarations for structs and union.
// These are necessary because `struct s_node`, `struct s_leaf`, and `union u_tree`
// refer to each other.
struct s_node;
struct s_leaf;
union u_tree;

/**
 * @brief Represents a Leaf node in the database tree.
 *
 * A Leaf node typically holds the actual key-value data. It forms a linked list
 * with other Leaf nodes via the `east` pointer, and can link back to a `Tree`
 * (Node or Leaf) via the `west` pointer, forming a double-linked structure
 * at the leaf level.
 */
struct s_leaf {
    union u_tree *west;  ///< Pointer to the preceding Tree (Node or Leaf) in the west direction.
    struct s_leaf *east; ///< Pointer to the next Leaf in the east (sibling) direction.
    int8_t key[128];     ///< Fixed-size array for the key data (128 bytes).
    int8_t *value;       ///< Pointer to the dynamically allocated value data.
    int16_t size;        ///< Size of the value data in bytes.
    Tag tag;             ///< Tag indicating this is a Leaf node (TagLeaf).
};
typedef struct s_leaf Leaf;

/**
 * @brief Represents an internal Node in the database tree.
 *
 * A Node typically organizes other Nodes and Leaves. It forms the hierarchical
 * structure of the database, allowing for path-based navigation.
 */
struct s_node {
    struct s_node *north; ///< Pointer to the parent Node.
    struct s_node *west;  ///< Pointer to a child Node (e.g., for sub-paths).
    struct s_leaf *east;  ///< Pointer to the first Leaf in the list associated with this Node.
    uint8_t path[256];    ///< Fixed-size array for the path segment represented by this Node.
    Tag tag;              ///< Tag indicating this is a Node (TagNode or TagRoot).
};
typedef struct s_node Node;

/**
 * @brief A union representing either a Node or a Leaf.
 *
 * This union allows a single `Tree` variable to hold either a `Node` or a `Leaf`
 * structure, sharing the same memory location. The `tag` member (within both
 * `Node` and `Leaf` structs) is used to determine which member is currently active.
 */
union u_tree {
    Node node; ///< The Node structure member.
    Leaf leaf; ///< The Leaf structure member.
};
typedef union u_tree Tree;

// =============================================================================
// Function Prototypes
// =============================================================================

/**
 * @brief Zeros out a specified block of memory.
 *
 * @param ptr   A pointer to the beginning of the memory block.
 * @param size  The number of bytes to set to zero.
 */
void zero(uint8_t *ptr, uint16_t size);

/**
 * @brief Creates and initializes a new Node in the tree structure.
 *
 * The new node is linked as a 'west' child of the provided parent node.
 *
 * @param parent A pointer to the parent Node under which the new node will be created.
 * @param path   A pointer to a character array (string) representing the path segment for the new node.
 * @return       A pointer to the newly created Node, or NULL if memory allocation fails.
 */
Node *create_node(Node *parent, int8_t *path);

/**
 * @brief Finds the last Leaf in the 'east' linked list starting from a parent Node.
 *
 * This function traverses the `east` pointers from the parent's first leaf
 * until it finds the last leaf in the sequence.
 *
 * @param parent A pointer to the Node from which to start the search.
 * @return       A pointer to the last Leaf found, or NULL if the parent has no 'east' leaf.
 */
Leaf *find_last_linear(Node *parent);

/**
 * @brief Creates and initializes a new Leaf node.
 *
 * This function allocates memory for a new Leaf, initializes its fields,
 * and links it to the provided 'west' tree element.
 *
 * @param west  A pointer to the `Tree` (Node or Leaf) that will be the 'west' link to this new leaf.
 * @param key   A pointer to the key data for this leaf.
 * @param value A pointer to the value data for this leaf.
 * @param size  The size of the value data in bytes.
 * @return      A pointer to the newly created Leaf, or NULL if memory allocation fails.
 */
Leaf *create_leaf(Tree *west, uint8_t *key, uint8_t *value, uint16_t size);


/**
 * @brief Main function - The entry point for the database server application.
 *
 * @param argc The number of command-line arguments.
 * @param argv An array of strings containing the command-line arguments.
 * @return     0 on successful execution, non-zero on error.
 */
int main(int argc, const char *argv[]);

#endif /* MAIN_H */