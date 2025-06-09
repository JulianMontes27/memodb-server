/* main.c */
#include "tree.h" // Custom header for database structures and 

void zero(uint8_t *ptr, uint16_t size)
{
    // Pre-condition check: Ensure the pointer is valid before attempting to dereference.
    // `assert` is used for debugging; it will terminate the program if the condition is false.
    assert(ptr != NULL && "Error: Attempted to zero NULL memory block.");

    // Use `memset` for efficient memory zeroing. `memset` is a standard library function
    // optimized for setting a block of memory to a specified byte value.
    memset(ptr, 0, size);
}

Node *create_node(Node *parent, int8_t *path)
{
    Node *node;         // Pointer to hold the address of the newly allocated Node.
    uint16_t node_size; // Stores the calculated size of the Node structure.

    // Pre-condition check: A parent node is required to create a new child node.
    assert(parent != NULL && "Error: Parent node cannot be NULL when creating a new node.");

    // Determine the exact size required for a `Node` structure.
    node_size = sizeof(struct s_node);
    // Allocate memory from the heap for the new Node.
    node = (Node *)malloc(node_size);

    // Error handling: Check if memory allocation was successful.
    if (node == NULL)
    {
        // Print a system-specific error message to standard error stream.
        perror("ERROR: Failed to allocate memory for a new Node");
        return NULL; // Return NULL to indicate allocation failure.
    }

    // Initialize the allocated memory to all zeros. This is crucial to prevent
    // undefined behavior from uninitialized pointers or data.
    zero((uint8_t *)node, node_size);

    // --- Debugging Output ---
    printf("--- Node Creation Details ---\n");
    printf("  Node structure size: %u bytes\n", node_size);
    printf("  Address of new Node in memory: %p\n", (void *)node);

    // Link the newly created node into the tree structure.
    parent->west = node;  // Link the parent's 'west' child pointer to this new node.
    node->tag = TagNode;  // Assign the appropriate tag to identify this as a Node.
    node->north = parent; // Set the new node's 'north' (parent) pointer.
    node->east = NULL;    // Initialize 'east' (pointer to first Leaf) to NULL.

    // Safely copy the provided path segment into the new node's `path` field.
    // `snprintf` is used to prevent buffer overflows by limiting the number of characters copied.
    snprintf((char *)node->path, sizeof(node->path), "%s", (char *)path);

    printf("  New Node path initialized to: '%s'\n", (char *)node->path);

    return node; // Return the pointer to the newly created node.
}

Leaf *find_last_linear(Node *parent)
{
    Leaf *current_leaf; // Iterator for traversing the leaf list.

    // Pre-condition check: Ensure the parent node is valid.
    assert(parent != NULL && "Error: Parent node cannot be NULL for find_last_linear.");

    // If the parent has no 'east' leaf, the list is empty.
    if (parent->east == NULL)
    {
        // Set errno to NoError (0) and return NULL, indicating no leaf was found but no error occurred.
        // Using the reterr macro to handle error setting and return.
        reterr(NoError);
    }

    // Start traversal from the first leaf linked to the parent.
    current_leaf = parent->east;

    // Iterate through the linked list of leaves until the last one is found.
    // The loop continues as long as `current_leaf->east` is not NULL, meaning there's a next leaf.
    while (current_leaf->east != NULL)
    {
        // Assert that the current_leaf pointer is valid before moving to the next.
        assert(current_leaf != NULL && "Error: NULL leaf pointer encountered during linear search.");
        current_leaf = current_leaf->east; // Move to the next leaf.
    }

    return current_leaf; // Return the last leaf found.
}

Leaf *create_leaf(Tree *west, uint8_t *key, uint8_t *value, uint16_t size)
{
    Leaf *leaf_list_last;      // The last leaf in the current parent's east list.
    Leaf *new_leaf;            // Pointer to the newly allocated Leaf.
    uint16_t leaf_struct_size; // Size of the Leaf structure.

    // Pre-condition check: The 'west' link (parent/sibling) must be valid.
    assert(west != NULL && "Error: 'west' link cannot be NULL when creating a new leaf.");

    // Find the last leaf in the current list to link the new leaf.
    // Cast 'west' to Node* if it's a Node, to use find_last.
    // This assumes `west` is a Node when creating the first leaf for a path.
    // A more robust solution might involve checking `west->tag`.
    leaf_list_last = find_last((Node *)west);

    // Determine the size required for a `Leaf` structure.
    leaf_struct_size = sizeof(struct s_leaf);
    // Allocate memory for the new Leaf structure.
    new_leaf = (Leaf *)malloc(leaf_struct_size);

    // Error handling: Check if `malloc` for the leaf structure failed.
    if (new_leaf == NULL)
    {
        perror("ERROR: Failed to allocate memory for new Leaf structure");
        return NULL;
    }

    // Initialize the newly allocated leaf structure to all zeros.
    zero((uint8_t *)new_leaf, leaf_struct_size);

    // Link the new leaf into the existing list or directly to the parent.
    if (leaf_list_last == NULL)
    {
        // If no existing leaves, link directly to the 'east' of the 'west' (Node) element.
        // This assumes 'west' is a Node when leaf_list_last is NULL.
        west->node.east = new_leaf;
    }
    else
    {
        // If there are existing leaves, link the new leaf after the last one found.
        leaf_list_last->east = new_leaf;
    }

    // Set the tag for the new leaf.
    new_leaf->tag = TagLeaf;

    // Set the 'west' pointer for the new leaf.
    // It points back to the element that linked to it (either a Node or the previous Leaf).
    new_leaf->west = west;

    // Safely copy the provided key into the new leaf's `key` field.
    // `snprintf` ensures null-termination and prevents buffer overflows.
    snprintf((char *)new_leaf->key, sizeof(new_leaf->key), "%s", (char *)key);

    // Allocate memory for the value data.
    new_leaf->value = (uint8_t *)malloc(size + 1); // +1 for null terminator if it's a string
    // Error handling: Check if `malloc` for value data failed.
    if (new_leaf->value == NULL)
    {
        perror("ERROR: Failed to allocate memory for Leaf value");
        free(new_leaf); // Free the leaf structure if value allocation fails.
        return NULL;
    }
    // Initialize the allocated value memory to zeros.
    zero(new_leaf->value, size + 1);

    // Safely copy the provided value data.
    // `snprintf` is used here assuming the value is a string. If it's raw binary data, `memcpy` would be used.
    snprintf((char *)new_leaf->value, size + 1, "%s", (char *)value);
    new_leaf->size = size; // Store the size of the value.

    // // --- Debugging Output ---
    // printf("--- Leaf Creation Details ---\n");
    // printf("  Leaf structure size: %u bytes\n", leaf_struct_size);
    // printf("  Address of new Leaf in memory: %p\n", (void *)new_leaf);
    // printf("  New Leaf key initialized to: '%s'\n", (char *)new_leaf->key);
    // printf("  New Leaf value initialized to: '%s' (size: %u)\n", (char *)new_leaf->value, new_leaf->size);
    // printf("-----------------------------\n");

    return new_leaf; // Return the pointer to the newly created leaf.
}

Tree root;

int main(int argc, const char *argv[])
{
    // Suppress unused parameter warnings for `argc` and `argv`.
    // This is a common practice when these parameters are not used in the function body.
    (void)argc;
    (void)argv;

    Node *newNode = NULL; // Pointer to hold a newly created node.
    Leaf *newLeaf = NULL; // Pointer to hold a newly created leaf.
    Leaf *leaf_2 = NULL;
    Node *rootNodeAddress; // Pointer to the Node part of the global `root` Tree union.
    uint8_t *key_data;     // Pointer for key string.
    uint8_t *value_data;   // Pointer for value string.
    uint16_t value_size;   // Size of the value data.

    // Get the address of the `node` member within the `root` union.
    rootNodeAddress = (Node *)&root;
    root.node.north = NULL;   // The root has no parent.
    root.node.west = NULL;    // Initially, no child nodes or sub-paths.
    root.node.east = NULL;    // Initially, no child leaves directly under root.
    root.node.tag = TagRoot;  // Set the tag to explicitly identify this as the root.
    root.node.path[0] = '\0'; // Initialize the root's path to an empty string.

    printf("  Root tag: %d\n", root.node.tag);
    printf("  Root tree address: %p\n", (void *)&root);

    // --- Demonstrate Node Creation ---
    printf("Attempting to create a new Node under the root...\n");
    // Create a new node under the root.
    // The path for the new node is currently an empty string (copied from root.node.path).
    newNode = create_node(rootNodeAddress, (int8_t *)root.node.path);

    // Check if the node creation was successful.
    if (newNode == NULL)
    {
        fprintf(stderr, "FATAL ERROR: Failed to create initial node. Exiting.\n");
        return 1; // Exit with an error code if critical allocation fails.
    }

    // Assign string literals to `uint8_t *` pointers, casting to suppress signedness warnings.
    key_data = (uint8_t *)"julian";
    value_data = (uint8_t *)"123456789";
    // Calculate the size of the value string. `strlen` does not include the null terminator.
    value_size = (uint16_t)strlen((char *)value_data);

    // Create a new leaf and link it to the newly created node.
    // The 'west' parameter for create_leaf expects a Tree*, so we pass the newNode cast to Tree*.
    newLeaf = create_leaf((Tree *)newNode, key_data, value_data, value_size);

    // Check if the leaf creation was successful.
    if (newLeaf == NULL)
    {
        fprintf(stderr, "FATAL ERROR: Failed to create initial leaf. Exiting.\n");
        // Remember to free previously allocated memory if an error occurs.
        if (newNode != NULL)
        {
            free(newNode);
            newNode = NULL;
        }
        return 1;
    }
    printf("  Leaf1 is located at: %p\n", (void *)newLeaf);
    printf("  Leaf1's key: '%s'\n", (char *)newLeaf->key);
    printf("  Leaf1's value: '%s'\n", (char *)newLeaf->value);
    printf("  Leaf1's value size: %u\n\n", newLeaf->size);

    // Assign string literals to `uint8_t *` pointers, casting to suppress signedness warnings.
    key_data = (uint8_t *)"juandi";
    value_data = (uint8_t *)"987654321";
    // Calculate the size of the value string. `strlen` does not include the null terminator.
    value_size = (uint16_t)strlen((char *)value_data);

    leaf_2 = create_leaf((Tree *)newLeaf, key_data, value_data, value_size);

    // Check if the leaf creation was successful.
    if (leaf_2 == NULL)
    {
        fprintf(stderr, "FATAL ERROR: Failed to create initial leaf. Exiting.\n");
        // Remember to free previously allocated memory if an error occurs.
        if (newNode != NULL)
        {
            free(newNode);
            newNode = NULL;
        }
        return 1;
    }
    printf("  Leaf2 is located at: %p\n", (void *)leaf_2);
    printf("  Leaf2's key: '%s'\n", (char *)leaf_2->key);
    printf("  Leaf2's value: '%s'\n", (char *)leaf_2->value);
    printf("  Leaf2's value size: %u\n\n", leaf_2->size);

    // --- Cleanup: Free Allocated Memory ---
    // In a full database server, memory management would be more sophisticated
    // (e.g., a garbage collector or a dedicated memory pool).
    // For this simple example, we explicitly free the allocated nodes/leaves.
    printf("--- Cleaning up allocated memory ---\n");
    if (newLeaf != NULL)
    {
        // Free the value data first, then the leaf structure itself.
        if (newLeaf->value != NULL)
        {
            printf("  Freeing newLeaf->value at %p\n", (void *)newLeaf->value);
            free(newLeaf->value);
            newLeaf->value = NULL;
        }
        printf("  Freeing newLeaf at %p\n", (void *)newLeaf);
        free(newLeaf);
        newLeaf = NULL;
    }
    if (newNode != NULL)
    {
        printf("  Freeing newNode at %p\n", (void *)newNode);
        free(newNode);
        newNode = NULL;
    }

    return 0; // Program executed successfully.
}