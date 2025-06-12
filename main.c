/* main.c - Event-driven MemoDB server implementation using epoll for Linux computers/servers */
#include "main.h" // Includes standard headers and server-specific definitions
#include "tree.h"   // Includes the tree data structure definitions (Node, Leaf, Tree, etc.)

// Global server context (declared here and defined in main.c)
struct server_context *g_server = NULL;

// Declare the global root tree from tree.c (it is defined there)
extern Tree root;

/**
 * @brief Helper function to ensure a node path exists, creating intermediate nodes if necessary.
 * This function traverses the tree based on the provided path. If any segment of the path
 * does not exist, it creates a new Node for that segment and links it into the tree.
 * The current tree implementation uses 'west' for sibling nodes and 'east' for the first leaf.
 * This function will create new 'west' children for the current_node if a path segment is not found.
 *
 * @param path The full path string (e.g., "users/data").
 * @return A pointer to the Node at the end of the specified path, or NULL on error.
 */
static Node *ensure_node_path(const char *path)
{
    // Start from the global root node. The root is a union, so access its node member.
    Node *current_node = &(root.node);
    // Make a mutable copy of the path string as strtok_r modifies it.
    char *path_copy = strdup(path);
    if (path_copy == NULL)
    {
        error_log("ensure_node_path: Failed to allocate memory for path copy.");
        return NULL;
    }

    char *token;
    char *saveptr; // Used by strtok_r to maintain state for re-entrant tokenizing.

    // Handle leading slash: if the path starts with '/', skip it.
    char *path_start = path_copy;
    if (path_start[0] == '/')
    {
        path_start++;
    }

    // If the path is empty or just "/", return the root node.
    if (strlen(path_start) == 0)
    {
        free(path_copy); // Free the duplicated path string.
        return current_node;
    }

    // Tokenize the path by '/' to process each segment.
    token = strtok_r(path_start, "/", &saveptr);

    // Loop through each token (path segment)
    while (token != NULL)
    {
        bool found = false;
        Node *child_candidate = current_node->west; // Start search from the current node's first child.

        // Iterate through siblings (nodes linked via 'west' pointer) to find the next path segment.
        // The tree structure here implies `west` links are for a list of child nodes from a single parent.
        while (child_candidate != NULL)
        {
            // Compare the current path segment (token) with the child's path.
            if (strcmp((char *)child_candidate->path, token) == 0)
            {
                current_node = child_candidate; // Found the next segment, move to this node.
                found = true;
                break;
            }
            child_candidate = child_candidate->west; // Move to the next sibling in the list.
        }

        if (!found)
        {
            // If the path segment was not found, create a new Node for it.
            // create_node links the new node as the 'west' child of the 'parent' (current_node).
            Node *new_node = create_node(current_node, (int8_t *)token);
            if (new_node == NULL)
            {
                error_log("ensure_node_path: Failed to create new node for path segment '%s'.", token);
                free(path_copy); // Clean up allocated memory.
                return NULL;
            }
            current_node = new_node; // Move to the newly created node for the next iteration.
        }

        // Move to the next path segment token.
        token = strtok_r(NULL, "/", &saveptr);
    }

    free(path_copy);     // Free the duplicated path string.
    return current_node; // Return the node at the end of the processed path.
}

/**
 * @brief Implements the SET command for the in-memory database.
 * Stores a key-value pair under a specified 'file' (node path).
 * If the path or key doesn't exist, it creates them. If the key exists,
 * its value is updated.
 *
 * @param filename The path (database name) where the key-value pair should be stored.
 * @param key The key to store.
 * @param value The value associated with the key.
 * @return 0 on success, -1 on error.
 */
int db_set(const char *filename, const char *key, const char *value)
{
    debug_log("DB_SET: file='%s', key='%s', value='%s'", filename, key, value);

    // 1. Ensure the node path (file) exists in the tree. Create it if it doesn't.
    Node *target_node = ensure_node_path(filename);
    if (target_node == NULL)
    {
        error_log("db_set: Failed to ensure node path '%s' exists.", filename);
        return -1;
    }

    // 2. Try to find if the key already exists as a Leaf under the target_node.
    // The find_leaf_linear function expects an int8_t* path and key.
    Leaf *existing_leaf = find_leaf_linear((int8_t *)filename, (int8_t *)key);

    if (existing_leaf)
    {
        // Key exists: Update the value.
        debug_log("db_set: Key '%s' found in '%s'. Updating value.", key, filename);
        // Free the old value before allocating new memory for the updated value.
        if (existing_leaf->value != NULL)
        {
            free(existing_leaf->value);
            existing_leaf->value = NULL; // Set to NULL defensively.
        }
        // Allocate new memory for the updated value, including space for null terminator.
        existing_leaf->value = (int8_t *)malloc(strlen(value) + 1);
        if (existing_leaf->value == NULL)
        {
            error_log("db_set: Failed to allocate memory for new value for key '%s'.", key);
            return -1;
        }
        // Copy the new value and update its size.
        strcpy((char *)existing_leaf->value, value);
        existing_leaf->size = strlen(value);
    }
    else
    {
        // Key does not exist: Create a new Leaf.
        debug_log("db_set: Key '%s' not found in '%s'. Creating new leaf.", key, filename);
        // Call create_leaf. The 'west' argument expects a Tree* which should be the target Node.
        // The Leaf will be linked to the Node's 'east' pointer (if first) or to the last existing leaf's 'east'.
        Leaf *new_leaf = create_leaf((Tree *)target_node, (uint8_t *)key, (uint8_t *)value, strlen(value));
        if (new_leaf == NULL)
        {
            error_log("db_set: Failed to create new leaf for key '%s' in '%s'.", key, filename);
            return -1;
        }
    }

    return 0; // Success
}

/**
 * @brief Implements the GET command for the in-memory database.
 * Retrieves the value associated with a key from a specified 'file' (node path).
 *
 * @param filename The path (database name) to search within.
 * @param key The key to retrieve.
 * @return A dynamically allocated string containing the value, or NULL if not found.
 * The caller is responsible for freeing the returned string.
 */
char *db_get(const char *filename, const char *key)
{
    debug_log("DB_GET: file='%s', key='%s'", filename, key);

    // Use the lookup_linear function from tree.c which finds the leaf and returns its value.
    // lookup_linear returns an `int8_t *`.
    int8_t *value_ptr = lookup_linear((int8_t *)filename, (int8_t *)key);

    if (value_ptr)
    {
        // Value found, return a dynamically allocated copy using strdup.
        // This ensures the caller gets a copy and is responsible for its memory.
        char *ret_value = strdup((char *)value_ptr);
        if (ret_value == NULL)
        {
            error_log("db_get: Failed to allocate memory for return value for key '%s'.", key);
            return NULL;
        }
        return ret_value;
    }
    else
    {
        // Key not found in the tree.
        return NULL;
    }
}

/**
 * @brief Implements the DEL command for the in-memory database.
 * Deletes a key-value pair from a specified 'file' (node path).
 *
 * @param filename The path (database name) from which to delete.
 * @param key The key to delete.
 * @return 0 on success, -1 on error (e.g., key not found or file not found).
 */
int db_del(const char *filename, const char *key)
{
    debug_log("DB_DEL: file='%s', key='%s'", filename, key);

    // 1. Find the target node (file/path) where the key should be.
    Node *target_node = find_node_linear((int8_t *)filename);
    if (target_node == NULL)
    {
        // Node (file/path) does not exist, so the key cannot be there.
        debug_log("db_del: File/node '%s' not found.", filename);
        return -1;
    }

    // 2. Traverse the list of leaves attached to the target_node to find the key.
    Leaf *current_leaf = target_node->east; // Start from the first leaf attached to this node.
    Leaf *prev_leaf = NULL;                 // Keep track of the previous leaf for relinking.

    while (current_leaf != NULL)
    {
        // Check if the current leaf's key matches the key to be deleted.
        if (strcmp((char *)current_leaf->key, key) == 0)
        {
            // Leaf found! Now, remove it from the linked list.
            if (prev_leaf == NULL)
            {
                // This is the first leaf in the list (attached directly to the node's east pointer).
                target_node->east = current_leaf->east; // Node now points to the next leaf.
            }
            else
            {
                // This leaf is in the middle or at the end of the list.
                prev_leaf->east = current_leaf->east; // Previous leaf now points to the current leaf's next.
            }
            // Free the found leaf and its dynamically allocated value.
            free_leaf(current_leaf);
            debug_log("db_del: Successfully deleted key '%s' from file '%s'.", key, filename);
            return 0; // Success: Key was found and deleted.
        }
        // Move to the next leaf in the list.
        prev_leaf = current_leaf;
        current_leaf = current_leaf->east;
    }

    // If the loop finishes, it means the key was not found in the specified file/node.
    debug_log("db_del: Key '%s' not found in file '%s'.", key, filename);
    return -1; // Error: Key not found.
}

/**
 * Signal handler for graceful shutdown
 * Sets the server running flag to false, causing main loop to exit
 */
void shutdown_handler(int sig)
{
    if (g_server)
    {
        info_log("Received signal %d, initiating graceful shutdown...", sig);
        g_server->running = false;
    }
}

/**
 * Set a file descriptor to non-blocking mode
 * This is crucial for event-driven I/O - prevents blocking on reads/writes
 *
 * @param fd - File descriptor to make non-blocking
 * @return 0 on success, -1 on error
 */
int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0); // Sys call - manipulate file descriptor
    if (flags == -1)
    {
        error_log("fcntl F_GETFL failed: %s", strerror(errno));
        return -1;
    }

    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        error_log("fcntl F_SETFL failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * Create a new client structure
 * Allocates memory and initializes client state
 *
 * @param fd - Client socket file descriptor
 * @param addr - Client address structure
 * @return Pointer to client structure, or NULL on error
 */
struct client *create_client(int fd, struct sockaddr_in *addr)
{
    struct client *client = calloc(1, sizeof(struct client));
    if (!client)
    {
        error_log("Failed to allocate memory for client");
        return NULL;
    }

    client->fd = fd;
    client->state = CLIENT_CONNECTING;
    client->read_pos = 0;
    client->write_pos = 0;
    client->write_len = 0;
    client->write_pending = false;
    client->last_activity = time(NULL);
    client->port = ntohs(addr->sin_port);

    // Convert IP address to string
    if (!inet_ntop(AF_INET, &addr->sin_addr, client->ip, INET_ADDRSTRLEN))
    {
        error_log("inet_ntop failed: %s", strerror(errno));
        free(client);
        return NULL;
    }

    debug_log("Created client %s:%d (fd=%d)", client->ip, client->port, client->fd);
    return client;
}

/**
 * Destroy a client and free its resources
 * Removes client from epoll, closes socket, frees memory
 *
 * @param client - Client to destroy
 */
void destroy_client(struct client *client)
{
    if (!client)
        return;

    debug_log("Destroying client %s:%d (fd=%d)", client->ip, client->port, client->fd);

    // Remove from epoll
    if (epoll_ctl(g_server->epoll_fd, EPOLL_CTL_DEL, client->fd, NULL) == -1)
    {
        error_log("epoll_ctl DEL failed: %s", strerror(errno));
    }

    // Close socket
    close(client->fd);

    // Remove from clients array
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (g_server->clients[i] == client)
        {
            g_server->clients[i] = NULL;
            g_server->client_count--;
            break;
        }
    }

    free(client);
}

/**
 * Handle new incoming connection
 * Accepts connection, creates client structure, adds to epoll
 *
 * @return 0 on success, -1 on error
 */
int handle_new_connection(void)
{
    struct sockaddr_in client_addr;
    socklen_t addr_len = sizeof(client_addr);

    // Accept new connection
    int client_fd = accept(g_server->listen_fd, (struct sockaddr *)&client_addr, &addr_len);
    if (client_fd == -1)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            error_log("accept failed: %s", strerror(errno));
        }
        return -1;
    }

    // Check if we have room for more clients
    if (g_server->client_count >= MAX_CLIENTS)
    {
        error_log("Maximum clients reached, rejecting connection");
        close(client_fd);
        return -1;
    }

    // Set client socket to non-blocking
    if (set_nonblocking(client_fd) == -1)
    {
        error_log("Failed to set client socket non-blocking");
        close(client_fd);
        return -1;
    }

    // Create client structure
    struct client *client = create_client(client_fd, &client_addr);
    if (!client)
    {
        close(client_fd);
        return -1;
    }

    // Add client to epoll for read events
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET; // Edge-triggered for better performance
    ev.data.ptr = client;

    if (epoll_ctl(g_server->epoll_fd, EPOLL_CTL_ADD, client_fd, &ev) == -1)
    {
        error_log("epoll_ctl ADD failed: %s", strerror(errno));
        destroy_client(client);
        return -1;
    }

    // Add client to clients array
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (g_server->clients[i] == NULL)
        {
            g_server->clients[i] = client;
            g_server->client_count++;
            break;
        }
    }

    client->state = CLIENT_AUTHENTICATED;
    info_log("New client connected: %s:%d (fd=%d, total=%d)",
             client->ip, client->port, client->fd, g_server->client_count);

    // Send welcome message
    send_to_client(client, "Welcome to MemoDB! Type 'help' for commands.\n> ");

    return 0;
}

/**
 * Handle client read event
 * Reads data from client socket and processes commands
 *
 * @param client - Client to read from
 * @return 0 on success, -1 on error/disconnect
 */
int handle_client_read(struct client *client)
{
    client->last_activity = time(NULL);

    while (1)
    {
        ssize_t bytes_read = recv(client->fd,
                                  client->read_buffer + client->read_pos,
                                  BUFFER_SIZE - client->read_pos - 1, 0);

        if (bytes_read == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // No more data available right now
                break;
            }
            error_log("recv failed for client %s:%d: %s",
                      client->ip, client->port, strerror(errno));
            return -1;
        }

        if (bytes_read == 0)
        {
            // Client disconnected
            info_log("Client %s:%d disconnected", client->ip, client->port);
            return -1;
        }

        client->read_pos += bytes_read;
        client->read_buffer[client->read_pos] = '\0';

        // Process complete commands (lines ending with \n)
        char *line_start = client->read_buffer;
        char *line_end;

        while ((line_end = strchr(line_start, '\n')) != NULL)
        {
            *line_end = '\0'; // Null-terminate the command

            // Remove carriage return if present
            if (line_end > line_start && *(line_end - 1) == '\r')
            {
                *(line_end - 1) = '\0';
            }

            // Process the command
            if (strlen(line_start) > 0)
            {
                process_client_command(client, line_start);
            }

            line_start = line_end + 1;
        }

        // Move remaining data to beginning of buffer
        size_t remaining = client->read_buffer + client->read_pos - line_start;
        if (remaining > 0)
        {
            memmove(client->read_buffer, line_start, remaining);
        }
        client->read_pos = remaining;

        // Check for buffer overflow
        if (client->read_pos >= BUFFER_SIZE - 1)
        {
            error_log("Client %s:%d command too long, disconnecting",
                      client->ip, client->port);
            return -1;
        }
    }

    return 0;
}

/**
 * Handle client write event
 * Writes pending data to client socket
 *
 * @param client - Client to write to
 * @return 0 on success, -1 on error
 */
int handle_client_write(struct client *client)
{
    if (!client->write_pending)
    {
        return 0; // Nothing to write
    }

    client->last_activity = time(NULL);

    while (client->write_pos < client->write_len)
    {
        ssize_t bytes_written = send(client->fd,
                                     client->write_buffer + client->write_pos,
                                     client->write_len - client->write_pos, 0);

        if (bytes_written == -1)
        {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
            {
                // Socket buffer is full, try again later
                return 0;
            }
            error_log("send failed for client %s:%d: %s",
                      client->ip, client->port, strerror(errno));
            return -1;
        }

        client->write_pos += bytes_written;
    }

    // All data written, reset write state
    client->write_pending = false;
    client->write_pos = 0;
    client->write_len = 0;

    // Remove EPOLLOUT event since we're done writing
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLET;
    ev.data.ptr = client;

    if (epoll_ctl(g_server->epoll_fd, EPOLL_CTL_MOD, client->fd, &ev) == -1)
    {
        error_log("epoll_ctl MOD failed: %s", strerror(errno));
        return -1;
    }

    return 0;
}

/**
 * @brief Parses a raw command string into a structured command.
 *
 * This function tokenizes the input command string to extract the command
 * itself, the filename, key, and value (if applicable for SET). It handles
 * different command formats and reports parsing errors.
 *
 * Command formats:
 * GET <file> <key>
 * SET <file> <key> <value>
 * DEL <file> <key>
 *
 * @param command_str The raw command string received from the client.
 * @param parsed_cmd Pointer to a parsed_command_t structure to fill.
 * @return True on successful parsing, false on error.
 */
bool parse_command(const char *command_str, parsed_command_t *parsed_cmd)
{
    // Make a mutable copy of the command string as strtok_r modifies it.
    char *cmd_copy = strdup(command_str);
    if (!cmd_copy)
    {
        error_log("Failed to allocate memory for command copy.");
        return false;
    }

    // Initialize the parsed command structure to all zeros.
    memset(parsed_cmd, 0, sizeof(parsed_command_t));

    char *token;
    char *saveptr; // Pointer for strtok_r to manage internal state.

    // Get the first token, which is expected to be the command itself.
    token = strtok_r(cmd_copy, " ", &saveptr);

    // Check if a command token was successfully extracted.
    if (token)
    {
        // Copy the command token into the parsed_cmd structure, ensuring null-termination.
        strncpy(parsed_cmd->command, token, sizeof(parsed_cmd->command) - 1);
        parsed_cmd->command[sizeof(parsed_cmd->command) - 1] = '\0';
    }
    else
    {
        // No command found (empty string or just spaces).
        free(cmd_copy); // Free the duplicated string.
        return false;   // Return false to indicate parsing failure.
    }

    // Convert the command to uppercase for case-insensitive comparison (e.g., "get" vs "GET").
    for (char *p = parsed_cmd->command; *p; ++p)
    {
        *p = toupper((unsigned char)*p);
    }

    // Handle GET command: GET <file> <key>
    if (strcmp(parsed_cmd->command, "GET") == 0)
    {
        // Get the 'file' token.
        token = strtok_r(NULL, " ", &saveptr);
        if (!token)
        {
            free(cmd_copy);
            return false; // Missing file argument.
        }
        strncpy(parsed_cmd->file, token, sizeof(parsed_cmd->file) - 1);
        parsed_cmd->file[sizeof(parsed_cmd->file) - 1] = '\0';

        // Get the 'key' token.
        token = strtok_r(NULL, " ", &saveptr);
        if (!token)
        {
            free(cmd_copy);
            return false; // Missing key argument.
        }
        strncpy(parsed_cmd->key, token, sizeof(parsed_cmd->key) - 1);
        parsed_cmd->key[sizeof(parsed_cmd->key) - 1] = '\0';

        // Ensure no extra arguments are present for GET.
        token = strtok_r(NULL, " ", &saveptr);
        if (token)
        {
            free(cmd_copy);
            return false; // Too many arguments for GET.
        }
    }
    // Handle SET command: SET <file> <key> <value>
    else if (strcmp(parsed_cmd->command, "SET") == 0)
    {
        // Get the 'file' token.
        token = strtok_r(NULL, " ", &saveptr);
        if (!token)
        {
            free(cmd_copy);
            return false; // Missing file argument.
        }
        strncpy(parsed_cmd->file, token, sizeof(parsed_cmd->file) - 1);
        parsed_cmd->file[sizeof(parsed_cmd->file) - 1] = '\0';

        // Get the 'key' token.
        token = strtok_r(NULL, " ", &saveptr);
        if (!token)
        {
            free(cmd_copy);
            return false; // Missing key argument.
        }
        strncpy(parsed_cmd->key, token, sizeof(parsed_cmd->key) - 1);
        parsed_cmd->key[sizeof(parsed_cmd->key) - 1] = '\0';

        // The remaining part of `saveptr` after extracting key should be the value.
        // It might start with a space, so skip any leading spaces.
        char *value_start = saveptr;
        while (*value_start == ' ')
        {
            value_start++;
        }

        // Check if a value was actually provided.
        if (*value_start != '\0')
        {
            // Copy the remaining string as the value, ensuring null-termination.
            strncpy(parsed_cmd->value, value_start, sizeof(parsed_cmd->value) - 1);
            parsed_cmd->value[sizeof(parsed_cmd->value) - 1] = '\0';
        }
        else
        {
            free(cmd_copy);
            return false; // Missing value for SET.
        }
    }
    // Handle DEL command: DEL <file> <key>
    else if (strcmp(parsed_cmd->command, "DEL") == 0)
    {
        // Get the 'file' token.
        token = strtok_r(NULL, " ", &saveptr);
        if (!token)
        {
            free(cmd_copy);
            return false; // Missing file argument.
        }
        strncpy(parsed_cmd->file, token, sizeof(parsed_cmd->file) - 1);
        parsed_cmd->file[sizeof(parsed_cmd->file) - 1] = '\0';

        // Get the 'key' token.
        token = strtok_r(NULL, " ", &saveptr);
        if (!token)
        {
            free(cmd_copy);
            return false; // Missing key argument.
        }
        strncpy(parsed_cmd->key, token, sizeof(parsed_cmd->key) - 1);
        parsed_cmd->key[sizeof(parsed_cmd->key) - 1] = '\0';

        // Ensure no extra arguments are present for DEL.
        token = strtok_r(NULL, " ", &saveptr);
        if (token)
        {
            free(cmd_copy);
            return false; // Too many arguments for DEL.
        }
    }
    else
    {
        // Command is not recognized as GET, SET, or DEL.
        free(cmd_copy);
        return false; // Unknown command.
    }

    free(cmd_copy); // Free the duplicated string after parsing is complete.
    return true;    // Parsing successful.
}

/**
 * @brief Processes a client command, handling built-in commands and CRUD operations.
 *
 * This function orchestrates the parsing and execution of client commands.
 * It first checks for simple built-in commands, then attempts to parse
 * more complex CRUD operations using the `parsed_command_t` structure.
 *
 * @param client Pointer to the client structure.
 * @param command The raw command string received from the client.
 */
void process_client_command(struct client *client, const char *command)
{
    // Log the received command for debugging and monitoring.
    info_log("Processing Client %s:%d command: '%s'", client->ip, client->port, command);

    // Handle built-in commands first as they don't require complex parsing.
    if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0)
    {
        send_to_client(client, "Goodbye!\n");
        client->state = CLIENT_DISCONNECTING; // Set client state to disconnect.
        return;
    }

    // Display help message with available commands.
    if (strcmp(command, "help") == 0)
    {
        send_to_client(client,
                       "Available commands:\n"
                       "  help        - Show this help message\n"
                       "  info        - Show server information\n"
                       "  quit        - Disconnect from server\n"
                       "  GET <file> <key> - Retrieve a value from a file\n"
                       "  SET <file> <key> <value> - Set a value in a file\n"
                       "  DEL <file> <key> - Delete a key-value pair from a file\n"
                       "> ");
        return;
    }

    // Display server and client information.
    if (strcmp(command, "info") == 0)
    {
        char info_msg[512]; // Buffer to compose the info message.
        snprintf(info_msg, sizeof(info_msg),
                 "Server Information:\n"
                 "  Host: %s:%d\n"
                 "  Connected clients: %d/%d\n"
                 "  Your IP: %s:%d\n"
                 "> ",
                 HOST, g_server->port, g_server->client_count, MAX_CLIENTS,
                 client->ip, client->port);
        send_to_client(client, info_msg);
        return;
    }

    // Attempt to parse the command as a CRUD operation.
    parsed_command_t parsed_cmd;
    if (!parse_command(command, &parsed_cmd))
    {
        // If parsing fails, it's an invalid or malformed command.
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response),
                 "Error: Malformed command or invalid arguments for '%s'. Type 'help' for syntax.\n> ",
                 command); // Echo back the problematic command.
        send_to_client(client, response);
        return;
    }

    // Dispatch to the appropriate database function based on the parsed command.
    if (strcmp(parsed_cmd.command, "GET") == 0)
    {
        // Call the database GET function to retrieve the value.
        char *value = db_get(parsed_cmd.file, parsed_cmd.key);
        char response[BUFFER_SIZE];
        if (value)
        {
            // Value found, send it back to the client.
            snprintf(response, sizeof(response), "OK: %s\n> ", value);
            free(value); // Important: Free the dynamically allocated value returned by db_get.
        }
        else
        {
            // Key not found in the specified file.
            snprintf(response, sizeof(response), "ERR: Key '%s' not found in file '%s'.\n> ",
                     parsed_cmd.key, parsed_cmd.file);
        }
        send_to_client(client, response);
    }
    else if (strcmp(parsed_cmd.command, "SET") == 0)
    {
        // Call the database SET function to store or update the key-value pair.
        // db_set now takes (const char *file, const char *key, const char *value)
        // Ensure the db_set signature matches this call here.
        if (db_set(parsed_cmd.file, parsed_cmd.key, parsed_cmd.value) == 0)
        {
            // Set operation successful.
            send_to_client(client, "OK\n> ");
        }
        else
        {
            // Set operation failed (e.g., out of memory, internal tree error).
            send_to_client(client, "ERR: Failed to set value. Check server logs.\n> ");
        }
    }
    else if (strcmp(parsed_cmd.command, "DEL") == 0)
    {
        // Attempt to delete the key-value pair from the specified file.
        if (db_del(parsed_cmd.file, parsed_cmd.key) == 0)
        {
            // Delete operation successful.
            send_to_client(client, "OK\n> ");
        }
        else
        {
            // Delete operation failed (e.g., key not found, file not found).
            send_to_client(client, "ERR: Failed to delete key. Check server logs.\n> ");
        }
    }
    else
    {
        // This case should ideally be caught by `parse_command`, but acts as a final fallback.
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response),
                 "Unknown command: '%s'. Type 'help' for available commands.\n> ",
                 command);
        send_to_client(client, response);
    }
}

/**
 * Send a message to a client
 * Buffers the message for asynchronous sending
 *
 * @param client - Client to send message to
 * @param message - Message to send
 */
void send_to_client(struct client *client, const char *message)
{
    size_t msg_len = strlen(message);

    // Check if message fits in buffer
    if (msg_len >= BUFFER_SIZE)
    {
        error_log("Message too long for client %s:%d", client->ip, client->port);
        return;
    }

    // If we already have pending data, try to send it first
    // This ensures that the write buffer is cleared before adding new data if possible.
    if (client->write_pending)
    {
        handle_client_write(client);
        if (client->write_pending)
        {
            // Still have pending data, can't send new message right now.
            // This might happen if the socket write buffer is full.
            error_log("Write buffer full for client %s:%d", client->ip, client->port);
            return;
        }
    }

    // Copy message to write buffer
    memcpy(client->write_buffer, message, msg_len);
    client->write_len = msg_len;
    client->write_pos = 0; // Reset write position for the new message.
    client->write_pending = true;

    // Add EPOLLOUT event to trigger handle_client_write when the socket is ready for writing.
    // We modify the existing epoll interest list for this client.
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET; // Keep EPOLLIN, add EPOLLOUT, retain EPOLLET.
    ev.data.ptr = client;                     // Re-associate the client pointer with the event.

    if (epoll_ctl(g_server->epoll_fd, EPOLL_CTL_MOD, client->fd, &ev) == -1)
    {
        error_log("epoll_ctl MOD failed: %s", strerror(errno));
    }
}

/**
 * Initialize the server
 * Creates socket, binds to port, starts listening
 *
 * @param port - Port to listen on
 * @return 0 on success, -1 on error
 */
int init_server(uint16_t port)
{
    struct sockaddr_in server_addr;
    int opt = 1; // For setsockopt SO_REUSEADDR.

    // Create a TCP socket (AF_INET for IPv4, SOCK_STREAM for TCP, 0 for default protocol).
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0); // Sys call
    if (listen_fd == -1)
    {
        error_log("socket creation failed: %s", strerror(errno));
        return -1;
    }

    // Set socket option SO_REUSEADDR to allow reusing the address immediately after closing.
    // This prevents "Address already in use" errors during quick server restarts.
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        error_log("setsockopt SO_REUSEADDR failed: %s", strerror(errno));
        close(listen_fd); // Close the socket on error.
        return -1;
    }

    // Set the listening socket to non-blocking mode.
    // This is crucial for event-driven I/O with epoll, preventing blocking calls.
    if (set_nonblocking(listen_fd) == -1)
    {
        close(listen_fd); // Close the socket on error.
        return -1;
    }

    // Configure server address structure.
    memset(&server_addr, 0, sizeof(server_addr));  // Zero out the structure.
    server_addr.sin_family = AF_INET;              // IPv4.
    server_addr.sin_addr.s_addr = inet_addr(HOST); // Bind to the specified host IP (e.g., 127.0.0.1).
    server_addr.sin_port = htons(port);            // Convert port number to network byte order.

    // Bind the socket to the configured IP address and port.
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        error_log("bind failed: %s", strerror(errno));
        close(listen_fd); // Close the socket on error.
        return -1;
    }

    // Start listening for incoming connections.
    // BACKLOG defines the maximum length of the queue of pending connections.
    if (listen(listen_fd, BACKLOG) == -1)
    {
        error_log("listen failed: %s", strerror(errno));
        close(listen_fd); // Close the socket on error.
        return -1;
    }

    info_log("Server listening on %s:%d (fd=%d)", HOST, port, listen_fd);
    return listen_fd; // Return the listening socket file descriptor.
}

/**
 * Main event loop
 * Uses epoll to handle multiple clients efficiently
 * It continuously listens for events on sockets using epoll, allowing it to handle multiple client
 * connections efficiently and concurrently in a single-threaded, non-blocking fashion.
 */
void main_loop(void)
{
    // Array to store ready events reported by epoll_wait.
    struct epoll_event events[MAX_EVENTS];

    info_log("Starting main event loop...");
    info_log("Server ready to accept connections");

    // The main server loop runs as long as the 'running' flag in g_server is true.
    while (g_server->running)
    {
        // Wait for events on the epoll file descriptor.
        // It waits up to 1 second (1000 ms) for any file descriptor in the epoll interest list to become ready.
        // This timeout prevents the loop from blocking indefinitely, allowing periodic tasks to be performed.
        int nfds = epoll_wait(g_server->epoll_fd, events, MAX_EVENTS, 1000);

        if (nfds == -1)
        {
            // If epoll_wait was interrupted by a signal (e.g., SIGINT, SIGTERM), continue the loop.
            if (errno == EINTR)
            {
                continue;
            }
            // Log other fatal errors and break the loop.
            error_log("epoll_wait failed: %s", strerror(errno));
            break;
        }

        // Process all events that are ready (if there are any).
        for (int i = 0; i < nfds; i++)
        {
            // Check if the current event is from the server's listening socket.
            if (events[i].data.fd == g_server->listen_fd)
            {
                // New connection event: Handle incoming client connection.
                handle_new_connection();
            }
            else
            {
                // This is a client-specific event. Retrieve the client structure from event data.
                struct client *client = (struct client *)events[i].data.ptr;

                // Check for error or hangup events (client disconnected unexpectedly or socket error).
                if (events[i].events & (EPOLLERR | EPOLLHUP))
                {
                    debug_log("Client %s:%d error/hangup", client->ip, client->port);
                    destroy_client(client); // Clean up client resources.
                    continue;               // Move to the next event.
                }

                // Check for data available to read (EPOLLIN).
                if (events[i].events & EPOLLIN)
                {
                    // Attempt to read data from the client. If handle_client_read returns -1,
                    // it indicates a disconnect or error, so destroy the client.
                    if (handle_client_read(client) == -1)
                    {
                        destroy_client(client);
                        continue;
                    }
                }

                // Check if the socket is ready for writing (EPOLLOUT).
                if (events[i].events & EPOLLOUT)
                {
                    // Attempt to write pending data to the client. If handle_client_write returns -1,
                    // destroy the client.
                    if (handle_client_write(client) == -1)
                    {
                        destroy_client(client);
                        continue;
                    }
                }

                // After processing read/write events, check if the client is in a disconnecting state.
                // This allows graceful shutdown initiated by client commands (e.g., 'quit').
                if (client->state == CLIENT_DISCONNECTING)
                {
                    destroy_client(client); // Clean up client resources.
                }
            }
        }

        // TODO: Add periodic maintenance tasks here
        // - Check for client timeouts (e.g., based on client->last_activity).
        // - More sophisticated database cleanup if needed (e.g., periodic tree optimization).
        // - Update server statistics.
    }

    info_log("Main event loop exited");
}

/**
 * Cleanup server resources
 */
void cleanup_server(void)
{
    if (!g_server)
        return;

    info_log("Cleaning up server resources...");

    // Free the entire in-memory database tree.
    info_log("Freeing MemoDB in-memory tree...");
    free_tree(&root); // Call the tree cleanup function from tree.c.
    info_log("MemoDB in-memory tree freed.");

    // Close all client connections.
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (g_server->clients[i])
        {
            destroy_client(g_server->clients[i]); // Calls destroy_client for each active client.
        }
    }

    // Close the listening socket if it's open.
    if (g_server->listen_fd >= 0)
    {
        close(g_server->listen_fd);
    }

    // Close the epoll file descriptor if it's open.
    if (g_server->epoll_fd >= 0)
    {
        close(g_server->epoll_fd);
    }

    free(g_server); // Free the global server context structure.
    g_server = NULL;

    info_log("Server cleanup complete");
}

/**
 * Main entry point of the MemoDB server application.
 */
int main(int argc, const char *argv[])
{
    uint16_t port; // Variable to store the server port.

    // Parse command line arguments for the port number.
    if (argc < 2)
    {
        // If no port is provided, use the default from configuration.
        port = (uint16_t)atoi(PORT); // Convert the string PORT to an integer.
        info_log("Using default port: %d", port);
    }
    else
    {
        // Use the port number provided as a command-line argument.
        port = (uint16_t)atoi(argv[1]);
        if (port == 0) // atoi returns 0 if the string is not a valid number.
        {
            error_log("Invalid port number: %s", argv[1]);
            exit(EXIT_FAILURE); // Exit if the port is invalid.
        }
        info_log("Using port from command line: %d", port);
    }

    // Allocate memory for the global server context structure.
    g_server = (struct server_context *)calloc(1, sizeof(struct server_context));
    if (!g_server)
    {
        error_log("Failed to dynamically allocate server context in memory.");
        exit(EXIT_FAILURE); // Exit if memory allocation fails.
    }

    // Initialize the server's listening socket.
    int server_fd = init_server(port);
    g_server->listen_fd = server_fd; // Store the listening file descriptor.
    if (g_server->listen_fd == -1)
    {
        cleanup_server(); // Clean up resources on failure.
        exit(EXIT_FAILURE);
    }
    g_server->port = port;      // Store the port.
    g_server->running = true;   // Set the server running flag to true.
    g_server->client_count = 0; // Initialize client count.

    // Create an epoll instance. EPOLL_CLOEXEC ensures the FD is closed on exec.
    g_server->epoll_fd = epoll_create1(EPOLL_CLOEXEC);
    if (g_server->epoll_fd == -1)
    {
        error_log("epoll_create1 failed: %s", strerror(errno));
        cleanup_server(); // Clean up resources on failure.
        exit(EXIT_FAILURE);
    }

    // Add the listening socket to the epoll interest list.
    // We are interested in EPOLLIN (readability) events.
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = g_server->listen_fd;

    // `epoll_ctl` modifies the epoll interest list. Here, it adds the listening socket.
    if (epoll_ctl(g_server->epoll_fd, EPOLL_CTL_ADD, g_server->listen_fd, &ev) == -1)
    {
        error_log("epoll_ctl ADD failed: %s", strerror(errno));
        cleanup_server(); // Clean up resources on failure.
        exit(EXIT_FAILURE);
    }

    // Install signal handlers for graceful shutdown (SIGINT, SIGTERM) and ignore broken pipes (SIGPIPE).
    signal(SIGINT, shutdown_handler);
    signal(SIGTERM, shutdown_handler);
    signal(SIGPIPE, SIG_IGN);

    info_log("MemoDB server started successfully");
    info_log("Press Ctrl+C to shutdown gracefully");
    info_log("Test with: telnet %s %d", HOST, port);

    // Initialize the Root node of the in-memory database tree.
    // The `root` variable is global (extern in tree.h, defined in tree.c).
    // Access its `node` member directly to initialize it.
    root.node.north = NULL;  // The root node has no parent.
    root.node.west = NULL;   // The root node initially has no child nodes.
    root.node.east = NULL;   // The root node initially has no leaves.
    root.node.tag = TagRoot; // Set the tag to identify it as the root node.
    // Set the root's path. "root" serves as an identifier for the base of the tree.
    strncpy((char *)root.node.path, "root", sizeof(root.node.path) - 1);
    root.node.path[sizeof(root.node.path) - 1] = '\0'; // Ensure null-termination.

    info_log("MemoDB in-memory tree initialized.");

    // Start the main event loop to handle client connections and commands.
    main_loop();

    // Perform server cleanup before exiting.
    cleanup_server();
    info_log("Server shutdown complete");

    return 0; // Indicate successful program execution.
}
