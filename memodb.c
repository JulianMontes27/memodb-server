/* main.c - Main implementation file with fork() support */
#include "memodb.h"

// Global variable to control the main server loop
// When this becomes false, the server will shut down
bool scontinuation = true;

/**
 * SIGCHLD Signal Handler - Critical for Fork-based Servers
 *
 * WHY WE NEED THIS:
 * When a child process (forked to handle a client) terminates, it becomes a "zombie"
 * - Zombie processes consume system resources (process table entries)
 * - If not cleaned up, we'll eventually run out of process slots
 * - The parent must "reap" dead children using wait() or waitpid()
 *
 * HOW IT WORKS:
 * - When child dies, kernel sends SIGCHLD signal to parent
 * - This handler automatically reaps all available zombie children
 * - WNOHANG means "don't block if no zombies are ready"
 * - Loop continues until no more zombies exist
 *
 * @param sig - Signal number (SIGCHLD = 17 on most systems)
 */
void sigchld_handler(int sig)
{
    // Save errno because signal handlers can modify it
    int saved_errno = errno;

    // Reap ALL available zombie children in one go
    // This is crucial because signals can be "coalesced" - if 5 children die
    // simultaneously, we might only get 1 SIGCHLD signal, not 5
    while (waitpid(-1, NULL, WNOHANG) > 0)
    {
        // waitpid(-1, NULL, WNOHANG) explanation:
        // -1 = wait for ANY child process
        // NULL = we don't care about exit status
        // WNOHANG = return immediately if no zombie children available
        // Return value > 0 = PID of reaped child
        // Return value 0 = no zombies available right now
        // Return value -1 = error (usually ECHILD = no children exist)
    }

    // Restore errno to prevent interference with main program
    errno = saved_errno;
}

/**
 * SIGINT/SIGTERM Signal Handler - Graceful Shutdown
 *
 * WHY WE NEED THIS:
 * - When user presses Ctrl+C or system sends SIGTERM, we want clean shutdown
 * - Without this, accept() call blocks forever and server won't respond to signals
 * - Allows server to finish current operations and close resources properly
 *
 * @param sig - Signal number (SIGINT=2 for Ctrl+C, SIGTERM=15 for kill)
 */
void shutdown_handler(int sig)
{
    printf("\n[Parent] Received shutdown signal (%d). Initiating graceful shutdown...\n", sig);
    scontinuation = false; // This will break the main accept() loop
}

/**
 * Client Handler Function - Runs in Child Process
 *
 * PROCESS ISOLATION EXPLAINED:
 * - This function runs in a completely separate process (created by fork())
 * - Child has its own memory space - can't corrupt parent or other children
 * - If child crashes, parent and other clients are unaffected
 * - Child inherits open file descriptors from parent (including sockets)
 *
 * SOCKET MANAGEMENT:
 * - Child inherits both listening socket AND client socket from parent
 * - Child MUST close listening socket (doesn't need it, prevents resource waste)
 * - Parent MUST close client socket (child handles it, prevents resource waste)
 * - This is crucial - failing to close sockets leads to resource leaks
 *
 * @param client_fd - File descriptor for communication with THIS specific client
 * @param client_ip - IP address string of the client (for logging)
 * @param client_port - Port number of the client (for logging)
 * @param server_socket_fd - Listening socket (MUST be closed by child)
 */
void handle_client(int client_fd, const char *client_ip, uint16_t client_port, int server_socket_fd)
{
    // CRITICAL: Child process must close the listening socket
    // WHY: Child inherited it from parent but doesn't need it
    // - Prevents resource waste (each child would hold a reference)
    // - Allows parent to properly shut down server when needed
    // - Child only needs the client-specific socket for communication
    close(server_socket_fd);

    printf("[Child PID:%d] Started handling client %s:%d\n", getpid(), client_ip, client_port);

    // Buffer for receiving client data
    char buffer[1024] = {0};

    // Send welcome message to client
    const char *welcome_msg = "Welcome to MemoDB! Type 'quit' to disconnect.\n> ";
    ssize_t bytes_sent = send(client_fd, welcome_msg, strlen(welcome_msg), 0);
    if (bytes_sent < 0)
    {
        perror("[Child] Failed to send welcome message");
        goto cleanup_and_exit;
    }

    // Main client communication loop
    // Each child process runs this loop independently
    while (1)
    {
        // Clear buffer for new data
        memset(buffer, 0, sizeof(buffer));

        // Receive data from client (BLOCKING call)
        // This blocks until client sends data or disconnects
        ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);

        // Handle client disconnection or receive error
        if (bytes_received <= 0)
        {
            if (bytes_received == 0)
            {
                printf("[Child PID:%d] Client %s:%d disconnected normally\n",
                       getpid(), client_ip, client_port);
            }
            else
            {
                printf("[Child PID:%d] Receive error from %s:%d: %s\n",
                       getpid(), client_ip, client_port, strerror(errno));
            }
            break; // Exit client loop
        }

        // Remove trailing newline characters (telnet sends \r\n)
        buffer[strcspn(buffer, "\r\n")] = '\0';

        printf("[Child PID:%d] Received from %s:%d: '%s'\n",
               getpid(), client_ip, client_port, buffer);

        // Process client commands
        if (strcmp(buffer, "quit") == 0 || strcmp(buffer, "exit") == 0)
        {
            // Client wants to disconnect
            const char *goodbye_msg = "Goodbye!\n";
            send(client_fd, goodbye_msg, strlen(goodbye_msg), 0);
            printf("[Child PID:%d] Client %s:%d requested disconnect\n",
                   getpid(), client_ip, client_port);
            break; // Exit client loop
        }
        else if (strcmp(buffer, "info") == 0)
        {
            // Show process information - useful for debugging fork() behavior
            char info_msg[256];
            snprintf(info_msg, sizeof(info_msg),
                     "Process Info:\n"
                     "- Child PID: %d\n"
                     "- Parent PID: %d\n"
                     "- Your IP: %s:%d\n> ",
                     getpid(), getppid(), client_ip, client_port);
            send(client_fd, info_msg, strlen(info_msg), 0);
        }
        else if (strlen(buffer) > 0)
        {
            // Echo command back to client (placeholder for actual database operations)
            char response[1024];
            snprintf(response, sizeof(response),
                     "Echo from PID %d: %s\n> ", getpid(), buffer);
            send(client_fd, response, strlen(response), 0);
        }
        else
        {
            // Empty command - just send new prompt
            const char *prompt = "> ";
            send(client_fd, prompt, strlen(prompt), 0);
        }
    }

cleanup_and_exit:
    // Close client socket - very important for resource management
    close(client_fd);
    printf("[Child PID:%d] Connection with %s:%d closed, child process exiting\n",
           getpid(), client_ip, client_port);

    // Child process MUST exit here
    // If child doesn't exit, it would return to main() and start accepting connections
    // This would create chaos - multiple processes accepting on same socket
    exit(0); // Successful termination
}

/**
 * Main server loop - handles client connections using fork() for concurrency
 *
 * FORK() PROCESS MODEL EXPLAINED:
 *
 * 1. Parent Process (Original):
 *    - Runs accept() loop continuously
 *    - Accepts new client connections
 *    - Forks child for each client
 *    - Immediately returns to accept() for next client
 *    - Handles SIGCHLD to reap dead children
 *
 * 2. Child Process (Forked):
 *    - Created by fork() for each client
 *    - Handles ONE specific client exclusively
 *    - Runs until client disconnects
 *    - Exits when done (never returns to parent code)
 *
 * CONCURRENCY ACHIEVED:
 * - Multiple clients can connect simultaneously
 * - Each client gets dedicated child process
 * - Children run in parallel (OS handles scheduling)
 * - No blocking between clients
 *
 * @param socket_fd - The server socket file descriptor
 */
void main_loop(int socket_fd)
{
    struct sockaddr_in cli; // Client address structure - filled by accept()
    socklen_t len = sizeof(cli);
    int client_fd;   // Client socket file descriptor
    pid_t child_pid; // Process ID of forked child

    // Install signal handlers BEFORE starting main loop
    // This ensures we can handle child termination and graceful shutdown

    // Handle child process termination (prevents zombie processes)
    if (signal(SIGCHLD, sigchld_handler) == SIG_ERR)
    {
        perror("Failed to install SIGCHLD handler");
        return;
    }
    // Handle shutdown signals (Ctrl+C, kill command)
    if (signal(SIGINT, shutdown_handler) == SIG_ERR)
    {
        perror("Failed to install SIGINT handler");
        return;
    }
    if (signal(SIGTERM, shutdown_handler) == SIG_ERR)
    {
        perror("Failed to install SIGTERM handler");
        return;
    }

    printf("[Parent PID:%d] Starting main loop. Waiting for connections...\n", getpid());

    // ACCEPT LOOP -> Server stays in this loop, forking children for each new client
    while (scontinuation)
    {
        // Accept connections on the server socket
        // THIS CALL BLOCKS until a client connects OR a signal interrupts it
        client_fd = accept(socket_fd, (struct sockaddr *)&cli, &len);

        // Check if we're shutting down (signal handler set scontinuation = false)
        if (!scontinuation)
        {
            printf("[Parent] Shutdown initiated, stopping accept loop\n");
            if (client_fd >= 0)
            {
                close(client_fd); // Close the socket we just accepted
            }
            break;
        }

        // Handle accept() errors
        if (client_fd < 0)
        {
            // accept() can fail due to:
            // - Interrupted by signal (EINTR) - this is normal
            // - System resource exhaustion
            // - Socket errors
            if (errno == EINTR)
            {
                // Signal interrupted accept() - this is normal, continue loop
                printf("[Parent] accept() interrupted by signal, continuing...\n");
                continue;
            }
            else
            {
                // Real error occurred
                perror("[Parent] accept failed");
                continue; // Try to continue accepting other connections
            }
        }

        // Extract client information for logging and passing to child
        uint16_t client_port = ntohs(cli.sin_port); // Network to host byte order
        char *client_ip = inet_ntoa(cli.sin_addr);  // Convert binary IP to string

        printf("[Parent PID:%d] New connection from %s:%d (client_fd: %d)\n",
               getpid(), client_ip, client_port, client_fd);

        // FORK A CHILD PROCESS - This is the concurrency magic!
        // fork() creates an exact copy of the current process
        // Both parent and child continue from this point, but with different PIDs
        child_pid = fork();

        if (child_pid < 0)
        {
            // FORK FAILED - This is serious but not fatal
            // Possible reasons: system out of memory, process limit reached
            perror("[Parent] fork() failed");

            // Send error to client and close connection
            const char *error_msg = "Server temporarily unavailable. Please try again.\n";
            send(client_fd, error_msg, strlen(error_msg), 0);
            close(client_fd);

            // Continue accepting other connections (don't give up entirely)
            continue;
        }
        else if (child_pid == 0)
        {
            // CHILD PROCESS CODE PATH
            // We are now in the child process (fork() returned 0 to child)

            printf("[Child PID:%d] Forked to handle client %s:%d\n",
                   getpid(), client_ip, client_port);

            // Handle this specific client (function never returns)
            // Child will exit() when client disconnects
            handle_client(client_fd, client_ip, client_port, socket_fd);

            // THIS LINE SHOULD NEVER BE REACHED
            // If we get here, something went wrong in handle_client()
            fprintf(stderr, "[Child] ERROR: handle_client() returned unexpectedly\n");
            exit(1);
        }
        else
        {
            // PARENT PROCESS CODE PATH
            // We are still in the parent process (fork() returned child PID to parent)

            printf("[Parent PID:%d] Created child process %d for client %s:%d\n",
                   getpid(), child_pid, client_ip, client_port);

            // CRITICAL: Parent must close the client socket
            // WHY: Child inherited a copy of this socket and will handle it
            // If parent doesn't close it:
            // - Resource leak (socket stays open unnecessarily)
            // - Client won't disconnect properly when child closes its copy
            // - Eventually run out of file descriptors
            close(client_fd);

            // Parent immediately continues to accept() for next client
            // This is what enables concurrency - parent doesn't wait for child to finish
            printf("[Parent] Closed client socket, ready for next connection\n");
        }

        // Parent continues the while loop and blocks on accept() again
        // Child never reaches this point (it called exit() in handle_client)
    }

    printf("[Parent PID:%d] Exiting main loop\n", getpid());

    // GRACEFUL SHUTDOWN PROCESS
    // Wait for all child processes to finish before parent exits
    // This ensures clean shutdown and prevents orphaned processes

    printf("[Parent] Waiting for all child processes to terminate...\n");

    int status;
    pid_t terminated_child;
    int active_children = 0;

    // Count and wait for all remaining child processes
    while ((terminated_child = wait(&status)) > 0)
    {
        active_children++;
        printf("[Parent] Child process %d terminated (status: %d)\n",
               terminated_child, status);
    }

    if (active_children > 0)
    {
        printf("[Parent] Successfully waited for %d child processes\n", active_children);
    }
    else
    {
        printf("[Parent] No child processes were running\n");
    }

    printf("[Parent] All children terminated. Main loop cleanup complete.\n");
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
 * CHANGES FOR FORK-BASED SERVER:
 * - No major changes needed - init_server() works exactly the same
 * - main_loop() now handles multiple concurrent clients via fork()
 * - Signal handlers are set up inside main_loop() for proper scope
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

    printf("=== MemDB Multi-Client Server Starting ===\n");

    // Handle command line arguments for port selection
    // This logic remains exactly the same
    if (argc < 2)
    {
        s_port = PORT; // Use default port from memodb.h
        printf("No port specified, using default port %s\n", PORT);
    }
    else
    {
        s_port = argv[1];
        printf("Using port from command line: %s\n", s_port);
    }

    // Convert the port (string) to integer
    port = (uint16_t)atoi(s_port);
    // Validate port number
    if (port == 0)
    {
        fprintf(stderr, "Invalid port number: %s\n", s_port);
        exit(EXIT_FAILURE);
    }

    // Initialize the server socket (done once at startup)
    socket_fd = init_server(port);

    // Initialize global continuation flag (explicitly set to true)
    scontinuation = true;

    printf("\n=== Multi-Client Server Ready ===\n");
    printf("Server can now handle multiple concurrent connections!\n");
    printf("Press Ctrl+C to shutdown gracefully\n");
    printf("Test with: telnet %s %d (open multiple terminals)\n\n", HOST, port);

    // Start the main server loop
    while (scontinuation)
    {
        main_loop(socket_fd);

        // If we reach here, it means scontinuation was set to false
        // (either by signal handler or some other shutdown condition)
        printf("[Main] Main loop exited, server shutting down...\n");
    }

    // Cleanup - close the server socket
    close(socket_fd);
    printf("\n=== Server shut down gracefully ===\n");

    return 0;
}