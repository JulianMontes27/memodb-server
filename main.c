/* main.c - Main implementation file */
#include "main.h"

// Global variable to control the main server loop
// When this becomes false, the server will shut down
bool scontinuation;

/**
 * Main server loop - handles client connections and database operations
 * Currently just a placeholder that immediately stops the server
 *
 * @param s - This should be the server socket file descriptor, but currently unused
 */
void main_loop(uint16_t s)
{
    // BUG: This immediately stops the server loop!
    // In a real database, this would:
    // 1. Accept incoming client connections
    // 2. Process database queries
    // 3. Send responses back to clients
    // 4. Continue running until told to stop

    scontinuation = false; // Stop the server immediately
    return;
}

/**
 * Initialize and configure the database server socket
 * This sets up the network listener that clients will connect to
 *
 * @param port - The port number to listen on (e.g., 12049)
 * @return server_fd - The file descriptor for the server socket, or exits on error
 */
int init_server(uint16_t port)
{
    int server_fd;                       // File descriptor for our server socket
    struct sockaddr_in address;          // Structure to hold server address info
    socklen_t addrlen = sizeof(address); // Size of address structure (currently unused)

    // Socket option for reusing address (currently unused but declared)
    int opt = 1;

    // Configure the server address structure
    address.sin_family = AF_INET;              // Use IPv4 protocol
    address.sin_addr.s_addr = inet_addr(HOST); // Convert "127.0.0.1" to binary format

    // BUG: Using PORT constant instead of the port parameter!
    address.sin_port = htons(port); // Convert port to network byte order

    // Step 1: Create a socket
    // AF_INET = IPv4, SOCK_STREAM = TCP (reliable), 0 = default protocol
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        perror("socket failed"); // Print error message
        exit(EXIT_FAILURE);      // Terminate program with error code
    }

    // Step 2: Bind socket to address and port
    // This tells the OS "when data comes to HOST:PORT, give it to this socket"
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }

    // Step 3: Start listening for connections
    // 20 = maximum number of pending connections in the queue
    if (listen(server_fd, 20) < 0)
    {
        perror("listen");
        exit(EXIT_FAILURE);
    }

    // Debug output - show the socket file descriptor number
    printf("server_fd: %i ", server_fd);

    return server_fd; // Return the socket so main() can use it
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
    int s;         // Server socket file descriptor

    // Handle command line arguments for port selection
    if (argc < 2)
    {
        // No port specified on command line, use default
        s_port = PORT; // "12049"
    }
    else
    {
        // Use port from command line argument
        s_port = argv[1]; // e.g., "./program 8080"
    }

    // Convert port string to integer
    // atoi("12049") returns 12049
    // uint16_t can hold values 0-65535 (valid port range)
    port = (uint16_t)atoi(s_port);

    // Initialize the server socket (done once at startup)
    s = init_server(port);

    // Start the main server loop
    scontinuation = true; // Enable the loop
    while (scontinuation)
    {
        // BUG: Passing port instead of socket file descriptor!
        // Should be: main_loop(s);
        main_loop(port);

        // Since main_loop() immediately sets scontinuation = false,
        // this loop only runs once and the server exits
    }

    // Program ends successfully
    return 0;
}

/*
 * CURRENT BUGS TO FIX:
 *
 * 1. In init_server(): Using PORT constant instead of port parameter
 *    Fix: address.sin_port = htons(port);
 *
 * 2. In main(): Passing port number instead of socket file descriptor to main_loop()
 *    Fix: main_loop(s);
 *
 * 3. main_loop() immediately stops the server
 *    Fix: Implement actual client handling with accept(), recv(), send()
 *
 * 4. No socket cleanup - should call close(server_fd) before exiting
 *
 * 5. Unused variables: opt, addrlen
 */