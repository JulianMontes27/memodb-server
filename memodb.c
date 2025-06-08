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
 * Process a client command
 * Parses and executes client commands
 *
 * @param client - Client that sent the command
 * @param command - Command string to process
 */
void process_client_command(struct client *client, const char *command)
{
    debug_log("Client %s:%d command: '%s'", client->ip, client->port, command);

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
                       "  help  - Show this help message\n"
                       "  info  - Show server information\n"
                       "  quit  - Disconnect from server\n"
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

    // TODO: Add your database commands here (INSERT, GET, DELETE, etc.)

    // Unknown command
    char response[256];
    snprintf(response, sizeof(response),
             "Unknown command: '%s'. Type 'help' for available commands.\n> ",
             command);
    send_to_client(client, response);
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
    g_server = (struct server_context*)calloc(1, sizeof(struct server_context));
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