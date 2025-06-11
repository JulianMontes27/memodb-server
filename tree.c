/* main.c */
#include "tree.h"

Tree root; // Global root tree

uint8_t *indent(uint8_t n)
{
    static uint8_t buf[256]; // Static buffer for indentation
    uint8_t *p;
    uint16_t i;

    if (n < 1)
    {
        return (uint8_t *)"";
    }
    // 256 -> 2 spaces per indent -> 256 / 2 = 128 -> minus the null terminator -> 128 -1 = 127 free bytes
    assert(n < 127);
    memset(buf, 0, sizeof(buf));

    for (i = 0, p = buf; i < n; i++, p += 2)
    {
        *p = ' '; // More efficient than strncpy
        *(p + 1) = ' ';
    }

    return buf;
}

void print_tree(uint8_t fd, Tree *_root)
{
    uint8_t indentation;
    Node *n;
    Leaf *l;
    uint16_t size;
    uint8_t buf[256];

    if (_root == NULL)
    {
        Print("Tree is NULL\n");
        return;
    }

    indentation = 0;

    // Traverse nodes (west-linked)
    for (n = (Node *)_root; n; n = n->west)
    {
        // Print node information
        Print(indent(indentation));
        Print("Node[");
        Print((char *)n->path);
        Print("] (tag: ");

        // Convert tag to string for printing
        char tag_str[16];
        snprintf(tag_str, sizeof(tag_str), "%d", n->tag);
        Print(tag_str);
        Print(")\n");

        // Print associated leaves (east-linked from this node)
        if (n->east)
        {
            for (l = n->east; l; l = l->east)
            {
                Print(indent(indentation + 1));
                Print("Leaf[");
                Print((char *)l->key);
                Print("] = '");
                Print((char *)l->value);
                Print("' (size: ");

                // Convert size to string for printing
                char size_str[16];
                snprintf(size_str, sizeof(size_str), "%d", l->size);
                Print(size_str);
                Print(")\n");
            }
        }

        indentation++;
    }
}

void zero(uint8_t *ptr, uint16_t size)
{
    // Pre-condition check: Ensure the pointer is valid before attempting to dereference.
    assert(ptr != NULL && "Error: Attempted to zero NULL memory block.");

    // Use `memset` for efficient memory zeroing.
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
        perror("ERROR: Failed to allocate memory for a new Node");
        return NULL;
    }

    // Initialize the allocated memory to all zeros.
    zero((uint8_t *)node, node_size);

    // Link the newly created node into the tree structure.
    parent->west = node;  // Link the parent's 'west' child pointer to this new node.
    node->tag = TagNode;  // Assign the appropriate tag to identify this as a Node.
    node->north = parent; // Set the new node's 'north' (parent) pointer.
    node->east = NULL;    // Initialize 'east' (pointer to first Leaf) to NULL.
    node->west = NULL;    // Initialize 'west' (pointer to child nodes) to NULL.

    // Safely copy the provided path segment into the new node's `path` field.
    snprintf((char *)node->path, sizeof(node->path), "%s", (char *)path);

    return node;
}

Leaf *lookup_linear(int8_t *path, int8_t *key) {};

Node *find_node_linear(int8_t *path)
{
    Node *current_node;
    char *path_copy;
    char *token;
    char *saveptr;

    // Pre-condition check: path must be valid
    if (path == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

    // Start from the global root
    current_node = &(root.node);

    // Make a copy of the path since strtok_r modifies it
    path_copy = strdup((char *)path);
    if (path_copy == NULL)
    {
        errno = ENOMEM;
        return NULL;
    }

    // Skip leading slash if present
    char *path_start = path_copy;
    if (path_start[0] == '/')
    {
        path_start++;
    }

    // If path is empty or just "/", return root
    if (strlen(path_start) == 0)
    {
        free(path_copy);
        return current_node;
    }

    // Tokenize the path by '/' and traverse
    token = strtok_r(path_start, "/", &saveptr);

    while (token != NULL)
    {
        // Look for matching node path
        bool found = false;

        // Check if current node matches the token
        if (strcmp((char *)current_node->path, token) == 0)
        {
            found = true;
        }
        else
        {
            // Search through west-linked child nodes
            Node *child = current_node->west;
            while (child != NULL)
            {
                if (strcmp((char *)child->path, token) == 0)
                {
                    current_node = child;
                    found = true;
                    break;
                }
                child = child->west;
            }
        }

        if (!found)
        {
            // Path segment not found
            free(path_copy);
            errno = ENOENT;
            return NULL;
        }

        // Move to next path segment
        token = strtok_r(NULL, "/", &saveptr);
    }

    free(path_copy);
    return current_node;
}

Leaf *find_last_linear(Node *parent)
{
    Leaf *current_leaf; // Iterator for traversing the leaf list.

    // Pre-condition check: Ensure the parent node is valid.
    assert(parent != NULL && "Error: Parent node cannot be NULL for find_last_linear.");

    // If the parent has no 'east' leaf, the list is empty.
    if (parent->east == NULL)
    {
        return (Leaf *)0;
    }

    // Start traversal from the first leaf linked to the parent.
    current_leaf = parent->east;

    // Iterate through the linked list of leaves until the last one is found.
    while (current_leaf->east != NULL)
    {
        assert(current_leaf != NULL && "Error: NULL leaf pointer encountered during linear search.");
        current_leaf = current_leaf->east;
    }

    return current_leaf;
}

Leaf *create_leaf(Tree *west, uint8_t *key, uint8_t *value, uint16_t size)
{
    Leaf *leaf_list_last;      // The last leaf in the current parent's east list.
    Leaf *new_leaf;            // Pointer to the newly allocated Leaf.
    uint16_t leaf_struct_size; // Size of the Leaf structure.

    // Pre-condition check: The 'west' link (parent/sibling) must be valid.
    assert(west != NULL && "Error: 'west' link cannot be NULL when creating a new leaf.");

    // Find the last leaf in the current list to link the new leaf.
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
    new_leaf->west = west;

    // Initialize east pointer to NULL
    new_leaf->east = NULL;

    // Safely copy the provided key into the new leaf's `key` field.
    snprintf((char *)new_leaf->key, sizeof(new_leaf->key), "%s", (char *)key);

    // Allocate memory for the value data.
    new_leaf->value = (int8_t *)malloc(size + 1); // +1 for null terminator
    // Error handling: Check if `malloc` for value data failed.
    if (new_leaf->value == NULL)
    {
        perror("ERROR: Failed to allocate memory for Leaf value");
        free(new_leaf);
        return NULL;
    }
    // Initialize the allocated value memory to zeros.
    zero((uint8_t *)new_leaf->value, size + 1);

    // Copy the provided value data.
    memcpy(new_leaf->value, value, size);
    new_leaf->size = size;

    return new_leaf;
}

// Helper function to free a leaf and its value
void free_leaf(Leaf *leaf)
{
    if (leaf != NULL)
    {
        if (leaf->value != NULL)
        {
            free(leaf->value);
        }
        free(leaf);
    }
}

// Helper function to free a node and all its leaves
void free_node_and_leaves(Node *node)
{
    if (node == NULL)
        return;

    // Free all leaves attached to this node
    Leaf *current_leaf = node->east;
    while (current_leaf != NULL)
    {
        Leaf *next_leaf = current_leaf->east;
        free_leaf(current_leaf);
        current_leaf = next_leaf;
    }

    // Free the node itself
    free(node);
}

// Helper function to free entire tree recursively
void free_tree(Tree *root)
{
    if (root == NULL)
        return;

    Node *current_node = &(root->node);

    // Free all child nodes (west-linked)
    while (current_node->west != NULL)
    {
        Node *next_node = current_node->west->west;
        free_node_and_leaves(current_node->west);
        current_node->west = next_node;
    }

    // Free leaves directly under root
    Leaf *current_leaf = current_node->east;
    while (current_leaf != NULL)
    {
        Leaf *next_leaf = current_leaf->east;
        free_leaf(current_leaf);
        current_leaf = next_leaf;
    }
}

int main(int argc, const char *argv[])
{
    // Suppress unused parameter warnings
    (void)argc;
    (void)argv;

    Node *newNode1 = NULL;
    Node *newNode2 = NULL;
    Leaf *newLeaf1 = NULL;
    Leaf *newLeaf2 = NULL;
    Leaf *newLeaf3 = NULL;
    Node *rootNodeAddress;
    uint8_t *key_data;
    uint8_t *value_data;
    uint16_t value_size;

    Node *test;

    printf("=== Tree Database Test Program ===\n\n");

    // Initialize the root
    rootNodeAddress = &(root.node);
    root.node.north = NULL;
    root.node.west = NULL;
    root.node.east = NULL;
    root.node.tag = TagRoot;
    strcpy((char *)root.node.path, "root");

    printf("Root initialized with tag: %d\n", root.node.tag);
    printf("Root address: %p\n\n", (void *)&root);

    // Create first child node
    // printf("Creating first child node...\n");
    newNode1 = create_node(rootNodeAddress, (int8_t *)"users");
    if (newNode1 == NULL)
    {
        fprintf(stderr, "Failed to create first node\n");
        return 1;
    }
    // printf("Created node with path: '%s'\n\n", (char *)newNode1->path);

    // Create second child node
    // printf("Creating second child node...\n");
    newNode2 = create_node(newNode1, (int8_t *)"profiles");
    if (newNode2 == NULL)
    {
        fprintf(stderr, "Failed to create second node\n");
        return 1;
    }
    // printf("Created node with path: '%s'\n\n", (char *)newNode2->path);

    // Create first leaf under first node
    // printf("Creating first leaf...\n");
    key_data = (uint8_t *)"julian";
    value_data = (uint8_t *)"123456789";
    value_size = (uint16_t)strlen((char *)value_data);

    newLeaf1 = create_leaf((Tree *)newNode1, key_data, value_data, value_size);
    if (newLeaf1 == NULL)
    {
        fprintf(stderr, "Failed to create first leaf\n");
        return 1;
    }
    // printf("Created leaf: key='%s', value='%s', size=%u\n\n",
    //        (char *)newLeaf1->key, (char *)newLeaf1->value, newLeaf1->size);

    // Create second leaf under first node
    // printf("Creating second leaf...\n");
    key_data = (uint8_t *)"juandi";
    value_data = (uint8_t *)"987654321";
    value_size = (uint16_t)strlen((char *)value_data);

    newLeaf2 = create_leaf((Tree *)newNode1, key_data, value_data, value_size);
    if (newLeaf2 == NULL)
    {
        fprintf(stderr, "Failed to create second leaf\n");
        return 1;
    }
    // printf("Created leaf: key='%s', value='%s', size=%u\n\n",
    //        (char *)newLeaf2->key, (char *)newLeaf2->value, newLeaf2->size);

    // Create third leaf under second node
    // printf("Creating third leaf...\n");
    key_data = (uint8_t *)"admin";
    value_data = (uint8_t *)"password123";
    value_size = (uint16_t)strlen((char *)value_data);

    newLeaf3 = create_leaf((Tree *)newNode2, key_data, value_data, value_size);
    if (newLeaf3 == NULL)
    {
        fprintf(stderr, "Failed to create third leaf\n");
        return 1;
    }
    // printf("Created leaf: key='%s', value='%s', size=%u\n\n",
    //        (char *)newLeaf3->key, (char *)newLeaf3->value, newLeaf3->size);

    // Print the entire tree structure to stdout (fd = 1)
    printf("=== Tree Structure ===\n");
    print_tree(1, &root);
    printf("\n");

    // Test finding last leaf
    printf("=== Testing find_last_linear ===\n");
    Leaf *last_leaf = find_last_linear(newNode1);
    if (last_leaf != NULL)
    {
        printf("Last leaf under 'users' node: key='%s', value='%s'\n",
               (char *)last_leaf->key, (char *)last_leaf->value);
    }
    else
    {
        printf("No leaves found under 'users' node\n");
    }

    last_leaf = find_last_linear(newNode2);
    if (last_leaf != NULL)
    {
        printf("Last leaf under 'profiles' node: key='%s', value='%s'\n",
               (char *)last_leaf->key, (char *)last_leaf->value);
    }
    else
    {
        printf("No leaves found under 'profiles' node\n");
    }

    // Test find_node_linear function
    printf("\n=== Testing find_node_linear ===\n");

    Node *found_node = find_node_linear((int8_t *)"/root");
    printf("find_node_linear(\"/root\"): %p (expected: %p)\n",
           (void *)found_node, (void *)&root.node);

    found_node = find_node_linear((int8_t *)"/root/users");
    printf("find_node_linear(\"/root/users\"): %p (expected: %p)\n",
           (void *)found_node, (void *)newNode1);

    found_node = find_node_linear((int8_t *)"/root/users/profiles");
    printf("find_node_linear(\"/root/users/profiles\"): %p (expected: %p)\n",
           (void *)found_node, (void *)newNode2);

    found_node = find_node_linear((int8_t *)"/root/nonexistent");
    printf("find_node_linear(\"/root/nonexistent\"): %p (should be NULL)\n",
           (void *)found_node);
    // Cleanup
    printf("\n=== Cleaning up memory ===\n");
    free_tree(&root);
    printf("Memory cleanup completed.\n");

    return 0;
}