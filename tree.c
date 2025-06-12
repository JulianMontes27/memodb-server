/* tree.c - Implementation of the MemoDB tree data structure */
#include "tree.h" // Include its own header for definitions and prototypes

// Global root of the MemoDB tree. This is defined here and declared extern in tree.h.
Tree root;

/**
 * @brief Generates an indentation string for pretty-printing the tree.
 * @param n The number of indentation levels (each level is two spaces).
 * @return A static buffer containing the indentation string.
 */
uint8_t *indent(uint8_t n)
{
    static uint8_t buf[256]; // Static buffer for indentation string.
    uint8_t *p;
    uint16_t i;

    if (n < 1)
    {
        return (uint8_t *)""; // No indentation needed.
    }
    // Assertion to prevent buffer overflow for indentation string.
    // 256 bytes / 2 spaces per indent = 128 max indents.
    // Ensure 'n' does not exceed the buffer capacity minus one for null terminator.
    assert(n < (sizeof(buf) / 2) && "Error: Indentation level too deep for buffer.");
    memset(buf, 0, sizeof(buf)); // Initialize buffer with zeros.

    // Populate the buffer with spaces.
    for (i = 0, p = buf; i < n; i++, p += 2)
    {
        *p = ' ';       // First space.
        *(p + 1) = ' '; // Second space.
    }

    return buf;
}

/**
 * @brief Prints the tree structure to a given file descriptor (e.g., standard output).
 * This function recursively traverses the tree and prints nodes and their associated leaves.
 *
 * @param fd The file descriptor to write the output to (e.g., 1 for stdout).
 * @param _root The root of the tree (or subtree) to print.
 */
void print_tree(uint8_t fd, Tree *_root)
{
    uint8_t indentation;
    Node *n;
    Leaf *l;

    if (_root == NULL)
    {
        Print(fd, "Tree is NULL\n"); // Use the Print macro.
        return;
    }

    indentation = 0;

    // Traverse nodes (nodes are linked via their 'west' pointer, forming a flat list here)
    for (n = (Node *)_root; n; n = n->west)
    {
        // Print node information.
        Print(fd, indent(indentation));
        Print(fd, "Node[");
        Print(fd, (char *)n->path);
        Print(fd, "] (tag: ");

        // Convert tag (integer) to string for printing.
        char tag_str[16];
        snprintf(tag_str, sizeof(tag_str), "%d", n->tag);
        Print(fd, tag_str);
        Print(fd, ")\n");

        // Print associated leaves (leaves are linked via 'east' pointer from this node).
        if (n->east) // Check if this node has any leaves.
        {
            for (l = n->east; l; l = l->east) // Iterate through the leaves.
            {
                Print(fd, indent(indentation + 1)); // Indent leaves more than their parent node.
                Print(fd, "Leaf[");
                Print(fd, (char *)l->key);
                Print(fd, "] = '");
                Print(fd, (char *)l->value);
                Print(fd, "' (size: ");

                // Convert size (integer) to string for printing.
                char size_str[16];
                snprintf(size_str, sizeof(size_str), "%d", l->size);
                Print(fd, size_str);
                Print(fd, ")\n");
            }
        }

        indentation++; // Increase indentation for child nodes (if recursion were used).
    }
}

/**
 * @brief Zeros out a block of memory.
 * @param ptr Pointer to the memory block.
 * @param size The number of bytes to zero out.
 */
void zero(uint8_t *ptr, uint16_t size)
{
    // Pre-condition check: Ensure the pointer is valid.
    assert(ptr != NULL && "Error: Attempted to zero NULL memory block.");

    // Use `memset` for efficient memory zeroing.
    memset(ptr, 0, size);
}

/**
 * @brief Creates a new Node and links it as a child (via 'west' link) to the parent.
 *
 * @param parent The parent Node to which the new node will be linked.
 * @param path The path segment for the new node.
 * @return Pointer to the newly created Node, or NULL on error.
 */
Node *create_node(Node *parent, int8_t *path)
{
    Node *node;         // Pointer to hold the address of the newly allocated Node.
    uint16_t node_size; // Stores the calculated size of the Node structure.

    // Pre-condition check: A parent node is required to create a new child node.
    assert(parent != NULL && "Error: Parent node cannot be NULL when creating a new node.");

    node_size = sizeof(struct s_node); // Determine the exact size required for a `Node` structure.
    node = (Node *)malloc(node_size);  // Allocate memory from the heap.

    // Error handling: Check if memory allocation was successful.
    if (node == NULL)
    {
        perror("ERROR: Failed to allocate memory for a new Node");
        reterr(ENOMEM); // Use reterr macro for NULL return and errno setting.
    }

    zero((uint8_t *)node, node_size); // Initialize the allocated memory to all zeros.

    // Link the newly created node into the tree structure.
    // In this tree design, 'west' is used as a sibling link for nodes,
    // so new nodes are typically added to a list branched from the parent.
    // The ensure_node_path function in main.c handles adding new children.
    node->west = parent->west; // Link new node to existing sibling list.
    parent->west = node;       // Parent now points to this new node as its first child.

    node->tag = TagNode;  // Assign the appropriate tag to identify this as a Node.
    node->north = parent; // Set the new node's 'north' (parent) pointer.
    node->east = NULL;    // Initialize 'east' (pointer to first Leaf) to NULL.

    // Safely copy the provided path segment into the new node's `path` field.
    // snprintf ensures null-termination and prevents buffer overflow.
    snprintf((char *)node->path, sizeof(node->path), "%s", (char *)path);

    return node;
}

/**
 * @brief Finds a specific Leaf by key within the leaves associated with a path.
 * This function effectively finds the Node for the given path and then
 * linearly searches its associated leaves for the specified key.
 *
 * @param path The path (Node) where the leaf is expected to be.
 * @param key The key of the leaf to find.
 * @return Pointer to the found Leaf, or NULL if not found.
 */
Leaf *find_leaf_linear(int8_t *path, int8_t *key)
{
    Node *n;
    Leaf *l, *ret;

    // Get the Node of the given path.
    n = find_node_linear(path);
    if (!n)
    {
        // Path does not exist, so leaf cannot exist.
        errno = ENOENT; // Set errno to indicate no such file or directory.
        return NULL;
    }

    // Linear Algorithm to traverse leaves associated with the found node.
    for (ret = NULL, l = n->east; l != NULL; l = l->east)
    {
        if (strcmp((char *)l->key, (char *)key) == 0)
        {
            ret = l; // Found the leaf.
            break;   // Exit loop.
        }
    }
    return ret; // Return the found leaf or NULL.
}

/**
 * @brief Finds a Node by traversing the tree linearly based on the provided path.
 * The path can contain multiple segments separated by '/'.
 *
 * @param path The full path string (e.g., "users/john/data").
 * @return Pointer to the found Node, or NULL if not found or on error.
 */
Node *find_node_linear(int8_t *path)
{
    Node *current_node;
    char *path_copy;
    char *token;
    char *saveptr; // For strtok_r state.

    // Pre-condition check: path must be valid.
    if (path == NULL)
    {
        reterr(EINVAL); // Invalid argument.
    }

    // Start from the global root Node.
    current_node = &(root.node);

    // Make a copy of the path since strtok_r modifies the string.
    path_copy = strdup((char *)path);
    if (path_copy == NULL)
    {
        reterr(ENOMEM); // Out of memory.
    }

    // Skip leading slash if present.
    char *path_start = path_copy;
    if (path_start[0] == '/')
    {
        path_start++;
    }

    // If path is empty or just "/", return the root.
    if (strlen(path_start) == 0)
    {
        free(path_copy); // Free the duplicated string.
        return current_node;
    }

    // Tokenize the path by '/' and traverse the tree.
    token = strtok_r(path_start, "/", &saveptr);

    // Loop through each path segment.
    while (token != NULL)
    {
        bool found = false;
        Node *child = current_node->west; // Start searching among the current node's direct children.

        // Search through the linked list of children (linked via 'west' pointers).
        while (child != NULL)
        {
            if (strcmp((char *)child->path, token) == 0)
            {
                current_node = child; // Found the next segment, move to this child node.
                found = true;
                break;
            }
            child = child->west; // Move to the next sibling.
        }

        if (!found)
        {
            // Path segment not found.
            free(path_copy); // Clean up allocated memory.
            reterr(ENOENT);  // No such entry.
        }

        // Move to the next path segment token.
        token = strtok_r(NULL, "/", &saveptr);
    }

    free(path_copy);     // Free the duplicated string.
    return current_node; // Return the node corresponding to the full path.
}

/**
 * @brief Looks up a value given a path and a key.
 * This function is a wrapper around `find_leaf_linear` to directly
 * return the value's pointer.
 *
 * @param path The path (Node) where the key is located.
 * @param key The key to look up.
 * @return Pointer to the value (int8_t*), or NULL if not found.
 */
int8_t *lookup_linear(int8_t *path, int8_t *key)
{
    Leaf *p = find_leaf_linear(path, key); // Find the leaf.

    if (p)
    {
        return p->value; // Return the value pointer if leaf found.
    }
    else
    {
        return NULL; // Return NULL if leaf not found.
    }
}

/**
 * @brief Finds the last Leaf in the 'east' linked list of a given Node.
 * Used when appending a new leaf to a node's list of key-value pairs.
 *
 * @param parent The Node whose leaves are to be traversed.
 * @return Pointer to the last Leaf, or NULL if the node has no leaves.
 */
Leaf *find_last_linear(Node *parent)
{
    Leaf *current_leaf; // Iterator for traversing the leaf list.

    // Pre-condition check: Ensure the parent node is valid.
    assert(parent != NULL && "Error: Parent node cannot be NULL for find_last_linear.");

    // If the parent has no 'east' leaf, the list is empty.
    if (parent->east == NULL)
    {
        return NULL; // No leaves found.
    }

    current_leaf = parent->east; // Start traversal from the first leaf.

    // Iterate through the linked list of leaves until the last one is found.
    while (current_leaf->east != NULL)
    {
        assert(current_leaf != NULL && "Error: NULL leaf pointer encountered during linear search.");
        current_leaf = current_leaf->east;
    }

    return current_leaf; // Return the last leaf.
}

/**
 * @brief Creates a new Leaf node and links it to the appropriate place in the tree.
 * The new leaf is linked either directly to the 'east' of the parent Node
 * or to the 'east' of the last existing Leaf of that Node.
 *
 * @param west The 'west' link for the new leaf (typically the parent Node).
 * @param key The key for the new leaf.
 * @param value The value data for the new leaf.
 * @param size The size of the value data in bytes.
 * @return Pointer to the newly created Leaf, or NULL on error.
 */
Leaf *create_leaf(Tree *west, uint8_t *key, uint8_t *value, uint16_t size)
{
    Leaf *leaf_list_last;      // The last leaf in the current parent's east list.
    Leaf *new_leaf;            // Pointer to the newly allocated Leaf.
    uint16_t leaf_struct_size; // Size of the Leaf structure.

    // Pre-condition check: The 'west' link (parent Node) must be valid.
    assert(west != NULL && "Error: 'west' link (parent Node) cannot be NULL when creating a new leaf.");

    // Find the last leaf in the current list to link the new leaf.
    // Cast 'west' to Node* as find_last_linear expects a Node.
    leaf_list_last = find_last_linear((Node *)west);

    leaf_struct_size = sizeof(struct s_leaf);    // Determine the size required for a `Leaf` structure.
    new_leaf = (Leaf *)malloc(leaf_struct_size); // Allocate memory for the new Leaf structure.

    // Error handling: Check if `malloc` for the leaf structure failed.
    if (new_leaf == NULL)
    {
        perror("ERROR: Failed to allocate memory for new Leaf structure");
        reterr(ENOMEM); // Use reterr macro.
    }

    zero((uint8_t *)new_leaf, leaf_struct_size); // Initialize the new leaf structure to zeros.

    // Link the new leaf into the existing list or directly to the parent Node.
    if (leaf_list_last == NULL)
    {
        // If no existing leaves for this node, link directly to the 'east' of the parent Node.
        west->node.east = new_leaf;
    }
    else
    {
        // If there are existing leaves, link the new leaf after the last one found.
        leaf_list_last->east = new_leaf;
    }

    new_leaf->tag = TagLeaf; // Set the tag for the new leaf.
    new_leaf->west = west;   // Set the 'west' pointer for the new leaf (back-link to parent/sibling).
    new_leaf->east = NULL;   // New leaf is always the last in its chain.

    // Safely copy the provided key into the new leaf's `key` field.
    snprintf((char *)new_leaf->key, sizeof(new_leaf->key), "%s", (char *)key);
    // Explicitly null-terminate, though snprintf should handle this if buffer is large enough.
    new_leaf->key[sizeof(new_leaf->key) - 1] = '\0';

    // Allocate memory for the value data. (+1 for null terminator).
    new_leaf->value = (int8_t *)malloc(size + 1);
    // Error handling: Check if `malloc` for value data failed.
    if (new_leaf->value == NULL)
    {
        perror("ERROR: Failed to allocate memory for Leaf value");
        free(new_leaf); // Free the leaf structure if value allocation fails.
        reterr(ENOMEM); // Use reterr macro.
    }
    zero((uint8_t *)new_leaf->value, size + 1); // Initialize the allocated value memory to zeros.

    memcpy(new_leaf->value, value, size); // Copy the provided value data.
    new_leaf->size = size;                // Store the actual size of the value.

    return new_leaf; // Return the newly created leaf.
}

/**
 * @brief Frees a single Leaf and its associated dynamically allocated value.
 * @param leaf Pointer to the Leaf to free.
 */
void free_leaf(Leaf *leaf)
{
    if (leaf != NULL)
    {
        if (leaf->value != NULL)
        {
            free(leaf->value); // Free the dynamically allocated value.
        }
        free(leaf); // Free the Leaf structure itself.
    }
}

/**
 * @brief Recursively frees a Node and all leaves directly attached to it.
 * This function does NOT traverse to child nodes (west-linked branches),
 * only the leaves linked via 'east' from the current node.
 * This is meant to be called for individual nodes after they've been
 * removed from their parent's 'west' list.
 *
 * @param node Pointer to the Node to free.
 */
void free_node_and_leaves(Node *node)
{
    if (node == NULL)
        return;

    // Free all leaves attached to this node (linked via 'east' pointer).
    Leaf *current_leaf = node->east;
    while (current_leaf != NULL)
    {
        Leaf *next_leaf = current_leaf->east; // Store next leaf before freeing current.
        free_leaf(current_leaf);              // Free the current leaf.
        current_leaf = next_leaf;             // Move to the next leaf.
    }

    free(node); // Free the Node structure itself.
}

/**
 * @brief Frees the entire tree structure starting from the given root.
 * This function traverses the tree (both 'west' child nodes and 'east' leaves)
 * and deallocates all dynamically allocated memory.
 * This is a depth-first traversal for nodes, then cleans up leaves.
 *
 * @param root Pointer to the root of the tree to free.
 */
void free_tree(Tree *root_union)
{
    if (root_union == NULL)
        return;

    // Start with the main root node.
    Node *current_node = &(root_union->node);

    // Recursively free all child nodes (west-linked) first in a depth-first manner.
    // This part is crucial for correctly freeing the nested node structure.
    Node *node_to_free = current_node->west; // Get the first child node.
    current_node->west = NULL;               // Sever the link to children from the root before freeing them, to prevent double freeing.

    while (node_to_free != NULL)
    {
        Node *next_node_sibling = node_to_free->west; // Store the next sibling.
        // Recursively call free_tree on the child. Note: this will free its own children, and its leaves.
        // The current tree structure uses `west` as a sibling list, not a direct child pointer.
        // To free entire branches, you'd recursively call on the children nodes.
        // Given the 'west' link creates a "linear" branch from the root, we'll free them iteratively.
        // For a true "tree," you'd need a different linking mechanism (e.g., 'children' array or a list of children).
        // For *this* specific `west` as sibling chain:
        free_node_and_leaves(node_to_free); // Free the current node in this linear chain and its leaves.
        node_to_free = next_node_sibling;   // Move to the next sibling in the chain.
    }

    // After all child nodes (west-linked) are freed, free leaves directly attached to the root.
    Leaf *current_leaf = current_node->east;
    current_node->east = NULL; // Sever the link to leaves from the root.
    while (current_leaf != NULL)
    {
        Leaf *next_leaf = current_leaf->east; // Store next leaf before freeing current.
        free_leaf(current_leaf);              // Free the current leaf.
        current_leaf = next_leaf;             // Move to the next leaf.
    }

    // IMPORTANT: The `root` itself is a global static variable and is not dynamically allocated
    // with malloc. Therefore, it should NOT be `free`d.
    // The `free_node_and_leaves` and `free_leaf` functions are designed for dynamically allocated
    // Node and Leaf structures.
    // The initial global `root` variable's memory is managed by the program's static data segment.
    // We only need to free its dynamically allocated children (nodes and leaves).
}

// int main(int argc, const char *argv[])
// {
//     // Suppress unused parameter warnings
//     (void)argc;
//     (void)argv;

//     Node *newNode1 = NULL;
//     Node *newNode2 = NULL;
//     Leaf *newLeaf1 = NULL;
//     Leaf *newLeaf2 = NULL;
//     Leaf *newLeaf3 = NULL;
//     Node *rootNodeAddress;
//     uint8_t *key_data;
//     uint8_t *value_data;
//     uint16_t value_size;

//     int8_t *test_lookup;

//     printf("=== Tree Database Test Program ===\n\n");

//     // Initialize the root
//     rootNodeAddress = &(root.node);
//     root.node.north = NULL;
//     root.node.west = NULL;
//     root.node.east = NULL;
//     root.node.tag = TagRoot;
//     strcpy((char *)root.node.path, "root");

//     printf("Root initialized with tag: %d\n", root.node.tag);
//     printf("Root address: %p\n\n", (void *)&root);

//     // Create first child node
//     // printf("Creating first child node...\n");
//     newNode1 = create_node(rootNodeAddress, (int8_t *)"users");
//     if (newNode1 == NULL)
//     {
//         fprintf(stderr, "Failed to create first node\n");
//         return 1;
//     }
//     // printf("Created node with path: '%s'\n\n", (char *)newNode1->path);

//     // Create second child node
//     // printf("Creating second child node...\n");
//     newNode2 = create_node(newNode1, (int8_t *)"profiles");
//     if (newNode2 == NULL)
//     {
//         fprintf(stderr, "Failed to create second node\n");
//         return 1;
//     }
//     // printf("Created node with path: '%s'\n\n", (char *)newNode2->path);

//     // Create first leaf under first node
//     // printf("Creating first leaf...\n");
//     key_data = (uint8_t *)"julian";
//     value_data = (uint8_t *)"123456789";
//     value_size = (uint16_t)strlen((char *)value_data);

//     newLeaf1 = create_leaf((Tree *)newNode1, key_data, value_data, value_size);
//     if (newLeaf1 == NULL)
//     {
//         fprintf(stderr, "Failed to create first leaf\n");
//         return 1;
//     }
//     // printf("Created leaf: key='%s', value='%s', size=%u\n\n",
//     //        (char *)newLeaf1->key, (char *)newLeaf1->value, newLeaf1->size);

//     // Create second leaf under first node
//     // printf("Creating second leaf...\n");
//     key_data = (uint8_t *)"juandi";
//     value_data = (uint8_t *)"987654321";
//     value_size = (uint16_t)strlen((char *)value_data);

//     newLeaf2 = create_leaf((Tree *)newNode1, key_data, value_data, value_size);
//     if (newLeaf2 == NULL)
//     {
//         fprintf(stderr, "Failed to create second leaf\n");
//         return 1;
//     }
//     // printf("Created leaf: key='%s', value='%s', size=%u\n\n",
//     //        (char *)newLeaf2->key, (char *)newLeaf2->value, newLeaf2->size);

//     // Create third leaf under second node
//     // printf("Creating third leaf...\n");
//     key_data = (uint8_t *)"admin";
//     value_data = (uint8_t *)"password123";
//     value_size = (uint16_t)strlen((char *)value_data);

//     newLeaf3 = create_leaf((Tree *)newNode2, key_data, value_data, value_size);
//     if (newLeaf3 == NULL)
//     {
//         fprintf(stderr, "Failed to create third leaf\n");
//         return 1;
//     }
//     // printf("Created leaf: key='%s', value='%s', size=%u\n\n",
//     //        (char *)newLeaf3->key, (char *)newLeaf3->value, newLeaf3->size);

//     // Print the entire tree structure to stdout (fd = 1)
//     printf("=== Tree Structure ===\n");
//     print_tree(1, &root);
//     printf("\n");

//     // Test finding last leaf
//     printf("=== Testing find_last_linear ===\n");
//     Leaf *last_leaf = find_last_linear(newNode1);
//     if (last_leaf != NULL)
//     {
//         printf("Last leaf under 'users' node: key='%s', value='%s'\n",
//                (char *)last_leaf->key, (char *)last_leaf->value);
//     }
//     else
//     {
//         printf("No leaves found under 'users' node\n");
//     }

//     last_leaf = find_last_linear(newNode2);
//     if (last_leaf != NULL)
//     {
//         printf("Last leaf under 'profiles' node: key='%s', value='%s'\n",
//                (char *)last_leaf->key, (char *)last_leaf->value);
//     }
//     else
//     {
//         printf("No leaves found under 'profiles' node\n");
//     }

//     // Test find_node_linear function
//     printf("\n=== Testing find_node_linear ===\n");

//     Node *found_node = find_node_linear((int8_t *)"/root");
//     printf("find_node_linear(\"/root\"): %p (expected: %p)\n",
//            (void *)found_node, (void *)&root.node);

//     found_node = find_node_linear((int8_t *)"/root/users");
//     printf("find_node_linear(\"/root/users\"): %p (expected: %p)\n",
//            (void *)found_node, (void *)newNode1);

//     found_node = find_node_linear((int8_t *)"/root/users/profiles");
//     printf("find_node_linear(\"/root/users/profiles\"): %p (expected: %p)\n",
//            (void *)found_node, (void *)newNode2);

//     found_node = find_node_linear((int8_t *)"/root/nonexistent");
//     printf("find_node_linear(\"/root/nonexistent\"): %p (should be NULL)\n",
//            (void *)found_node);

//     test_lookup = lookup((int8_t *)"/root/users/", (int8_t *)"julian");
//     if (test_lookup)
//     {
//         printf("%s\n", test_lookup);
//     }
//     else
//     {
//         printf("No\n");
//     }

//     // Cleanup
//     printf("\n=== Cleaning up memory ===\n");
//     free_tree(&root);
//     printf("Memory cleanup completed.\n");

//     return 0;
// }