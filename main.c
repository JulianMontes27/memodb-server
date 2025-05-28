/* main.c - Main implementation file */
#include "main.h"

// Global variable to control the main server loop
// When this becomes false, the server will shut down
bool scontinuation;

/**
 * Main server loop - handles client connections and database operations
 *
 * @param socket_fd - The server socket file descriptor
 */
void main_loop(int socket_fd)
{
    struct sockaddr_in cli;      // Client address structure - filled by accept()
    socklen_t len = sizeof(cli); // FIXED: Initialize len properly
    int client_fd;               // Client socket file descriptor
    uint16_t client_port;
    char buffer[1024] = {0}; // Buffer for receiving data

    printf("Waiting for connections...\n");

    // Accept connections on the server socket
    // This call BLOCKS until a client connects
    client_fd = accept(socket_fd, (struct sockaddr *)&cli, &len);
    if (client_fd < 0)
    {
        perror("accept failed");
        return;
    }

    // FIXED: Convert port from network to host byte order
    client_port = ntohs(cli.sin_port); // ntohs, not htons!
    char *client_ip = inet_ntoa(cli.sin_addr);
    printf("Connection accepted from %s:%d\n", client_ip, client_port);

    // Send a welcome message to the client
    const char *welcome_msg = "Welcome to MemDB! Type 'quit' to exit.\n> ";
    send(client_fd, welcome_msg, strlen(welcome_msg), 0);

    // Simple command loop for this client
    while (1)
    {
        // Clear the buffer
        memset(buffer, 0, sizeof(buffer));

        // Read data from client
        ssize_t bytes_read = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        if (bytes_read <= 0)
        {
            // Client disconnected or error occurred
            printf("Client %s:%d disconnected\n", client_ip, client_port);
            break;
        }

        // Remove newline characters
        buffer[strcspn(buffer, "\r\n")] = 0;

        printf("Received from %s:%d: '%s'\n", client_ip, client_port, buffer);

        // Simple command processing
        if (strcmp(buffer, "quit") == 0 || strcmp(buffer, "exit") == 0)
        {
            const char *goodbye_msg = "Goodbye!\n";
            send(client_fd, goodbye_msg, strlen(goodbye_msg), 0);
            printf("Client %s:%d requested disconnect\n", client_ip, client_port);
            break;
        }
        else if (strcmp(buffer, "shutdown") == 0)
        {
            const char *shutdown_msg = "Server shutting down...\n";
            send(client_fd, shutdown_msg, strlen(shutdown_msg), 0);
            printf("Shutdown requested by %s:%d\n", client_ip, client_port);
            scontinuation = false; // Stop the server
            break;
        }
        else if (strlen(buffer) > 0)
        {
            // Echo the command back with a response
            char response[1024];
            snprintf(response, sizeof(response), "Echo: %s\n> ", buffer);
            send(client_fd, response, strlen(response), 0);
        }
        else
        {
            // Empty command, just send prompt
            const char *prompt = "> ";
            send(client_fd, prompt, strlen(prompt), 0);
        }
    }

    // Close the client connection
    close(client_fd);
    printf("Connection with %s:%d closed\n", client_ip, client_port);
}

/**
 * Initialize and configure the database server socket
 * This sets up the network listener that clients will connect to
 *
 * @param port - The port number to listen on (e.g., 12049)
 * @return socket_fd - The file descriptor for the server socket, or exits on error
 */
int init_server(uint16_t port)
{
    int socket_fd;              // File descriptor for our server socket
    struct sockaddr_in address; // Structure to hold server address info

    // Socket option for reusing address - FIXED: Actually use this option
    int opt = 1;

    // Configure the server address structure
    address.sin_family = AF_INET;              // Use IPv4 protocol
    address.sin_addr.s_addr = inet_addr(HOST); // Convert "127.0.0.1" to binary format
    address.sin_port = htons(port);            // FIXED: Use the port parameter

    // Step 1: Create a socket; a communication endpoint
    // AF_INET = IPv4, SOCK_STREAM = TCP (reliable), 0 = default protocol
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }

    // FIXED: Set socket options to reuse address (prevents "Address already in use" error)
    if (setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt failed");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    // Step 2: Bind socket to address and port
    // This tells the OS "when data comes to HOST:PORT, give it to this socket"
    if (bind(socket_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    // Step 3: Start listening for connections
    // 20 = maximum number of pending connections in the queue
    if (listen(socket_fd, 20) < 0)
    {
        perror("listen failed");
        close(socket_fd);
        exit(EXIT_FAILURE);
    }

    // FIXED: More informative output
    printf("MemDB Server initialized successfully!\n");
    printf("Listening on %s:%d (socket_fd: %d)\n", HOST, port, socket_fd);
    printf("Use 'telnet %s %d' to connect\n", HOST, port);

    return socket_fd;
}

/**
 * Program entry point - handles command line arguments and starts the server
 *
 * @param argc - Number of command line arguments
 * @param argv - Array of command line argument strings
 * @return 0 on success, non-zero on error
 */
int main(int argc, const char *argv[])
{
    char *s_port;  // String version of port number
    uint16_t port; // Numeric version of port number
    int socket_fd; // Server socket file descriptor

    printf("=== MemDB Server Starting ===\n");

    // Handle command line arguments for port selection
    if (argc < 2)
    {
        s_port = PORT; // Use default port
        printf("No port specified, using default port %s\n", PORT);
    }
    else
    {
        s_port = argv[1];
        printf("Using port from command line: %s\n", s_port);
    }

    // Convert port string to integer
    port = (uint16_t)atoi(s_port);

    // Validate port number
    if (port == 0)
    {
        fprintf(stderr, "Invalid port number: %s\n", s_port);
        exit(EXIT_FAILURE);
    }

    // Initialize the server socket (done once at startup)
    socket_fd = init_server(port);

    // Start the main server loop
    scontinuation = true;
    printf("\n=== Server Ready - Press Ctrl+C to stop ===\n");

    while (scontinuation)
    {
        main_loop(socket_fd); // Handle one client connection at a time
    }

    // Cleanup
    close(socket_fd);
    printf("\n=== Server shut down gracefully ===\n");

    return 0;
}