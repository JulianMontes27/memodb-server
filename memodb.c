/* main.c - Event-driven MemoDB server implementation using epoll for Linux computers/servers */
#include "memodb.h"

// Global server context
struct server_context *g_server = NULL;

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
    int flags = fcntl(fd, F_GETFL, 0);
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
    // Make a mutable copy of the command string as strtok modifies it
    char *cmd_copy = strdup(command_str);
    if (!cmd_copy)
    {
        error_log("Failed to allocate memory for command copy.");
        return false;
    }

    // Initialize the parsed command structure
    memset(parsed_cmd, 0, sizeof(parsed_command_t));

    // Tokenize the command string by space
    char *token = strtok(cmd_copy, " ");

    // First token is always the command
    if (token)
    {
        strncpy(parsed_cmd->command, token, sizeof(parsed_cmd->command) - 1);
        parsed_cmd->command[sizeof(parsed_cmd->command) - 1] = '\0'; // Ensure null-termination
    }
    else
    {
        free(cmd_copy);
        return false; // Empty command
    }

    // Convert command to uppercase for case-insensitive comparison
    for (char *p = parsed_cmd->command; *p; ++p)
    {
        *p = toupper((unsigned char)*p);
    }

    // Handle GET command: GET <file> <key>
    if (strcmp(parsed_cmd->command, "GET") == 0)
    {
        // Get the file token
        token = strtok(NULL, " ");
        if (!token)
        {
            free(cmd_copy);
            return false; // Missing file
        }
        strncpy(parsed_cmd->file, token, sizeof(parsed_cmd->file) - 1);
        parsed_cmd->file[sizeof(parsed_cmd->file) - 1] = '\0';

        // Get the key token
        token = strtok(NULL, " ");
        if (!token)
        {
            free(cmd_copy);
            return false; // Missing key
        }
        strncpy(parsed_cmd->key, token, sizeof(parsed_cmd->key) - 1);
        parsed_cmd->key[sizeof(parsed_cmd->key) - 1] = '\0';

        // Ensure no extra arguments
        token = strtok(NULL, " ");
        if (token)
        {
            free(cmd_copy);
            return false; // Too many arguments for GET
        }
    }
    // Handle SET command: SET <file> <key> <value>
    else if (strcmp(parsed_cmd->command, "SET") == 0)
    {
        // Get the file token
        token = strtok(NULL, " ");
        if (!token)
        {
            free(cmd_copy);
            return false; // Missing file
        }
        strncpy(parsed_cmd->file, token, sizeof(parsed_cmd->file) - 1);
        parsed_cmd->file[sizeof(parsed_cmd->file) - 1] = '\0';

        // Get the key token
        token = strtok(NULL, " ");
        if (!token)
        {
            free(cmd_copy);
            return false; // Missing key
        }
        strncpy(parsed_cmd->key, token, sizeof(parsed_cmd->key) - 1);
        parsed_cmd->key[sizeof(parsed_cmd->key) - 1] = '\0';

        // The remaining part is the value, which can contain spaces.
        // Find the start of the value by advancing the pointer past the key.
        char *value_start = strstr(command_str, parsed_cmd->key);
        if (value_start)
        {
            value_start += strlen(parsed_cmd->key); // Move past the key
            // Skip any spaces after the key
            while (*value_start == ' ')
            {
                value_start++;
            }
            if (*value_start != '\0')
            {
                strncpy(parsed_cmd->value, value_start, sizeof(parsed_cmd->value) - 1);
                parsed_cmd->value[sizeof(parsed_cmd->value) - 1] = '\0';
            }
            else
            {
                free(cmd_copy);
                return false; // Missing value for SET
            }
        }
        else
        {
            free(cmd_copy);
            return false; // Should not happen if key was found
        }
    }
    // Handle DEL command: DEL <file> <key>
    else if (strcmp(parsed_cmd->command, "DEL") == 0)
    {
        // Get the file token
        token = strtok(NULL, " ");
        if (!token)
        {
            free(cmd_copy);
            return false; // Missing file
        }
        strncpy(parsed_cmd->file, token, sizeof(parsed_cmd->file) - 1);
        parsed_cmd->file[sizeof(parsed_cmd->file) - 1] = '\0';

        // Get the key token
        token = strtok(NULL, " ");
        if (!token)
        {
            free(cmd_copy);
            return false; // Missing key
        }
        strncpy(parsed_cmd->key, token, sizeof(parsed_cmd->key) - 1);
        parsed_cmd->key[sizeof(parsed_cmd->key) - 1] = '\0';

        // Ensure no extra arguments
        token = strtok(NULL, " ");
        if (token)
        {
            free(cmd_copy);
            return false; // Too many arguments for DEL
        }
    }
    else
    {
        free(cmd_copy);
        return false; // Unknown command
    }

    free(cmd_copy); // Free the duplicated string
    return true;
}

/**
 * @brief Processes a client command, handling CRUD operations.
 *
 * This function orchestrates the parsing and execution of client commands.
 * It now uses a structured approach to parse commands and dispatch to
 * appropriate database functions.
 *
 * @param client Pointer to the client structure.
 * @param command The raw command string received from the client.
 */
void process_client_command(struct client *client, const char *command)
{
    debug_log("Client %s:%d command: '%s'", client->ip, client->port, command);

    // Handle built-in commands first
    if (strcmp(command, "quit") == 0 || strcmp(command, "exit") == 0)
    {
        send_to_client(client, "Goodbye!\n");
        client->state = CLIENT_DISCONNECTING;
        return;
    }

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

    if (strcmp(command, "info") == 0)
    {
        char info_msg[512];
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

    // Parse the command for CRUD operations
    parsed_command_t parsed_cmd;
    if (!parse_command(command, &parsed_cmd))
    {
        // If parsing fails, it's an invalid or malformed command
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response),
                 "Error: Malformed command or invalid arguments for '%s'. Type 'help' for syntax.\n> ",
                 command); // Echo back the problematic command
        send_to_client(client, response);
        return;
    }

    // Dispatch to appropriate database function based on the parsed command
    if (strcmp(parsed_cmd.command, "GET") == 0)
    {
        // Attempt to retrieve the value from the specified file and key
        char *value = db_get(parsed_cmd.file, parsed_cmd.key);
        char response[BUFFER_SIZE];
        if (value)
        {
            // Value found, send it back to the client
            snprintf(response, sizeof(response), "OK: %s\n> ", value);
            free(value); // Free the dynamically allocated value from db_get
        }
        else
        {
            // Key not found
            snprintf(response, sizeof(response), "ERR: Key '%s' not found in file '%s'.\n> ",
                     parsed_cmd.key, parsed_cmd.file);
        }
        send_to_client(client, response);
    }
    else if (strcmp(parsed_cmd.command, "SET") == 0)
    {
        // // Attempt to set the value in the specified file and key
        // if (db_set(parsed_cmd.file, parsed_cmd.key, parsed_cmd.value) == 0) {
        //     // Set operation successful
        //     send_to_client(client, "OK\n> ");
        // } else {
        //     // Set operation failed (e.g., out of memory, file limit)
        //     send_to_client(client, "ERR: Failed to set value. Check server logs.\n> ");
        // }

        // Instead of calling db_set, we'll construct a response
        // showing the parsed components.
        char response[BUFFER_SIZE]; // Use BUFFER_SIZE for a larger response buffer

        // Construct the response string with the parsed data
        // Format: "Parsed SET: File='%s', Key='%s', Value='%s'\n> "
        snprintf(response, sizeof(response),
                 "Parsed SET: File='%s', Key='%s', Value='%s'\n> ",
                 parsed_cmd.file, parsed_cmd.key, parsed_cmd.value);

        // Send the diagnostic message back to the client
        send_to_client(client, response);

        // NOTE: Comment out or remove the actual db_set call for this test
        /*
        if (db_set(parsed_cmd.file, parsed_cmd.key, parsed_cmd.value) == 0) {
            send_to_client(client, "OK\n> ");
        } else {
            send_to_client(client, "ERR: Failed to set value. Check server logs.\n> ");
        }
        */
    }
    else if (strcmp(parsed_cmd.command, "DEL") == 0)
    {
        // Attempt to delete the key-value pair from the specified file
        if (db_del(parsed_cmd.file, parsed_cmd.key) == 0)
        {
            // Delete operation successful
            send_to_client(client, "OK\n> ");
        }
        else
        {
            // Delete operation failed (e.g., key not found, file not found)
            send_to_client(client, "ERR: Failed to delete key. Check server logs.\n> ");
        }
    }
    else
    {
        // This case should ideally be caught by parse_command, but as a fallback
        char response[BUFFER_SIZE];
        snprintf(response, sizeof(response),
                 "Unknown command: '%s'. Type 'help' for available commands.\n> ",
                 command);
        send_to_client(client, response);
    }
}

// --- Placeholder for Database Implementation (To be implemented in memodb.c or a dedicated db.c) ---
// You will need to implement the actual in-memory data storage and retrieval
// This could be a hash map, a linked list, or any other suitable data structure
// that can handle multiple 'files' (databases) and key-value pairs within each.

/**
 * @brief Placeholder for setting a key-value pair in a specified 'file'.
 *
 * @param filename The name of the 'database' or 'file'.
 * @param key The key to set.
 * @param value The value to associate with the key.
 * @return 0 on success, -1 on error.
 */
int db_set(const char *filename, const char *key, const char *value)
{
    // In a real implementation, you would:
    // 1. Find or create the data structure for 'filename'.
    // 2. Store (key, value) in that data structure.
    // Return 0 for success, -1 for error (e.g., out of memory, max file size reached).
    debug_log("DB_SET: file='%s', key='%s', value='%s'", filename, key, value);
    // Example: For now, always succeed
    return 0;
}

/**
 * @brief Placeholder for getting a value from a specified 'file' by key.
 *
 * @param filename The name of the 'database' or 'file'.
 * @param key The key to retrieve.
 * @return A dynamically allocated string containing the value, or NULL if not found.
 * The caller is responsible for freeing the returned string.
 */
char *db_get(const char *filename, const char *key)
{
    // In a real implementation, you would:
    // 1. Find the data structure for 'filename'.
    // 2. Look up 'key' in that data structure.
    // 3. If found, return a dynamically allocated copy of the value.
    // Return NULL if not found.
    debug_log("DB_GET: file='%s', key='%s'", filename, key);

    // Example: Simulate data retrieval
    if (strcmp(filename, "users") == 0 && strcmp(key, "john") == 0)
    {
        return strdup("doe123");
    }
    if (strcmp(filename, "products") == 0 && strcmp(key, "item1") == 0)
    {
        return strdup("keyboard");
    }
    return NULL; // Key not found
}

/**
 * @brief Placeholder for deleting a key-value pair from a specified 'file'.
 *
 * @param filename The name of the 'database' or 'file'.
 * @param key The key to delete.
 * @return 0 on success, -1 on error (e.g., key not found).
 */
int db_del(const char *filename, const char *key)
{
    // In a real implementation, you would:
    // 1. Find the data structure for 'filename'.
    // 2. Delete 'key' from that data structure.
    // Return 0 for success, -1 for error (e.g., key not found).
    debug_log("DB_DEL: file='%s', key='%s'", filename, key);
    // Example: For now, always succeed
    return 0;
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
    if (client->write_pending)
    {
        handle_client_write(client);
        if (client->write_pending)
        {
            // Still have pending data, can't send new message
            error_log("Write buffer full for client %s:%d", client->ip, client->port);
            return;
        }
    }

    // Copy message to write buffer
    memcpy(client->write_buffer, message, msg_len);
    client->write_len = msg_len;
    client->write_pos = 0;
    client->write_pending = true;

    // Add EPOLLOUT event to send the data
    struct epoll_event ev;
    ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
    ev.data.ptr = client;

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
    int opt = 1;

    // Create socket
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd == -1)
    {
        error_log("socket creation failed: %s", strerror(errno));
        return -1;
    }

    // Set socket options
    if (setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1)
    {
        error_log("setsockopt SO_REUSEADDR failed: %s", strerror(errno));
        close(listen_fd);
        return -1;
    }

    // Set socket to non-blocking
    if (set_nonblocking(listen_fd) == -1)
    {
        close(listen_fd);
        return -1;
    }

    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr(HOST);
    server_addr.sin_port = htons(port);

    // Bind socket
    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1)
    {
        error_log("bind failed: %s", strerror(errno));
        close(listen_fd);
        return -1;
    }

    // Start listening
    if (listen(listen_fd, BACKLOG) == -1)
    {
        error_log("listen failed: %s", strerror(errno));
        close(listen_fd);
        return -1;
    }

    info_log("Server listening on %s:%d (fd=%d)", HOST, port, listen_fd);
    return listen_fd;
}

/**
 * Main event loop
 * Uses epoll to handle multiple clients efficiently
 */
void main_loop(void)
{
    struct epoll_event events[MAX_EVENTS];

    info_log("Starting main event loop...");
    info_log("Server ready to accept connections");

    while (g_server->running)
    {
        // Wait for events
        int nfds = epoll_wait(g_server->epoll_fd, events, MAX_EVENTS, 1000); // 1 second timeout

        if (nfds == -1)
        {
            if (errno == EINTR)
            {
                // Interrupted by signal, continue
                continue;
            }
            error_log("epoll_wait failed: %s", strerror(errno));
            break;
        }

        // Process events
        for (int i = 0; i < nfds; i++)
        {
            if (events[i].data.fd == g_server->listen_fd)
            {
                // New connection
                handle_new_connection();
            }
            else
            {
                // Client event
                struct client *client = (struct client *)events[i].data.ptr;

                if (events[i].events & (EPOLLERR | EPOLLHUP))
                {
                    // Error or hangup
                    debug_log("Client %s:%d error/hangup", client->ip, client->port);
                    destroy_client(client);
                    continue;
                }

                if (events[i].events & EPOLLIN)
                {
                    // Data available to read
                    if (handle_client_read(client) == -1)
                    {
                        destroy_client(client);
                        continue;
                    }
                }

                if (events[i].events & EPOLLOUT)
                {
                    // Socket ready for writing
                    if (handle_client_write(client) == -1)
                    {
                        destroy_client(client);
                        continue;
                    }
                }

                // Check if client wants to disconnect
                if (client->state == CLIENT_DISCONNECTING)
                {
                    destroy_client(client);
                }
            }
        }

        // TODO: Add periodic maintenance tasks here
        // - Check for client timeouts
        // - Database cleanup
        // - Statistics updates
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

    // Close all client connections
    for (int i = 0; i < MAX_CLIENTS; i++)
    {
        if (g_server->clients[i])
        {
            destroy_client(g_server->clients[i]);
        }
    }

    // Close listening socket
    if (g_server->listen_fd >= 0)
    {
        close(g_server->listen_fd);
    }

    // Close epoll file descriptor
    if (g_server->epoll_fd >= 0)
    {
        close(g_server->epoll_fd);
    }

    free(g_server);
    g_server = NULL;

    info_log("Server cleanup complete");
}

/**
 * Main entry point
 */
int main(int argc, const char *argv[])
{
    uint16_t port;

    // Parse command line arguments
    if (argc < 2)
    {
        port = (uint16_t)atoi(PORT);
        info_log("Using default port: %d", port);
    }
    else
    {
        port = (uint16_t)atoi(argv[1]);
        if (port == 0)
        {
            error_log("Invalid port number: %s", argv[1]);
            exit(EXIT_FAILURE);
        }
        info_log("Using port from command line: %d", port);
    }

    // Allocate server context
    g_server = (struct server_context *)calloc(1, sizeof(struct server_context));
    if (!g_server)
    {
        error_log("Failed to dynamically allocate server context in memory.");
        exit(EXIT_FAILURE);
    }

    // Initialize server
    int server_fd = init_server(port);
    g_server->listen_fd = server_fd;
    if (g_server->listen_fd == -1)
    {
        cleanup_server();
        exit(EXIT_FAILURE);
    }
    g_server->port = port;
    g_server->running = true;
    g_server->client_count = 0;

    // Create epoll instance
    g_server->epoll_fd = epoll_create1(EPOLL_CLOEXEC); // EPOLL_CLOEXEC flag ensures the file descriptor is automatically closed when the process calls exec() - this prevents file descriptor leaks when spawning child processes.
    if (g_server->epoll_fd == -1)
    {
        error_log("epoll_create1 failed: %s", strerror(errno));
        cleanup_server();
        exit(EXIT_FAILURE);
    }

    // Add listening socket to epoll
    struct epoll_event ev;
    ev.events = EPOLLIN;
    ev.data.fd = g_server->listen_fd;

    if (epoll_ctl(g_server->epoll_fd, EPOLL_CTL_ADD, g_server->listen_fd, &ev) == -1)
    {
        error_log("epoll_ctl ADD failed: %s", strerror(errno));
        cleanup_server();
        exit(EXIT_FAILURE);
    }

    // Install signal handlers
    signal(SIGINT, shutdown_handler);
    signal(SIGTERM, shutdown_handler);
    signal(SIGPIPE, SIG_IGN); // Ignore broken pipe signals

    info_log("MemoDB server started successfully");
    info_log("Press Ctrl+C to shutdown gracefully");
    info_log("Test with: telnet %s %d", HOST, port);

    // Start main event loop
    main_loop();

    // Cleanup and exit
    cleanup_server();
    info_log("Server shutdown complete");

    return 0;
}