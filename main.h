/* memodb.h - Header file for event-driven MemoDB server */
#ifndef MAIN_H
#define MAIN_H

// Standard C library headers
#include <stdio.h>    // For printf, perror, etc.
#include <unistd.h>   // For UNIX specific system calls like close()
#include <stdlib.h>   // For exit(), atoi(), memory allocation
#include <string.h>   // For string manipulation functions
#include <stdint.h>   // For fixed-width integer types like uint16_t
#include <assert.h>   // For debugging assertions
#include <errno.h>    // For error handling (errno variable)
#include <stdbool.h>  // For boolean type (true/false)
#include <sys/wait.h> // For waitpid() and wait status macros
#include <signal.h>   // For signal handling
#include <fcntl.h>    // For fcntl() and file control
#include <time.h>     // For time functions
#include <ctype.h>    // For toupper() - used in command parsing

// Network programming headers
#include <arpa/inet.h>  // For inet_addr(), htons() - IP address conversion
#include <sys/socket.h> // For socket(), bind(), listen(), accept()
#include <netinet/in.h> // For sockaddr_in structure

// Event-driven I/O (Linux epoll)
#include <sys/epoll.h> // For epoll functionality

// Configuration constants
#define HOST "127.0.0.1"  // Localhost IP address
#define PORT "12049"      // Default port number as string
#define MAX_EVENTS 1024   // Maximum events to process per epoll_wait
#define MAX_CLIENTS 10000 // Maximum concurrent clients
#define BUFFER_SIZE 4096  // Buffer size for client data
#define BACKLOG 128       // Listen backlog queue size

#define NoError 0 // Success return code

// --- NEW ADDITIONS FOR COMMAND PARSING AND DB INTERACTION ---
#define MAX_KEY_LEN 128      // Maximum length for a database key (matching Leaf key size)
#define MAX_VALUE_LEN 1024   // Maximum length for a database value
#define MAX_FILENAME_LEN 256 // Maximum length for a 'file' (database) name (matching Node path size)

/**
 * @brief Represents a parsed client command.
 * This structure holds the components of a command like GET, SET, DEL.
 */
typedef struct
{
    char command[16];            // Stores the command (e.g., "GET", "SET", "DEL")
    char file[MAX_FILENAME_LEN]; // Stores the 'file' (database name)
    char key[MAX_KEY_LEN];       // Stores the key for GET/SET/DEL
    char value[MAX_VALUE_LEN];   // Stores the value for SET
} parsed_command_t;

// Function prototypes for command parsing and database operations
// (These functions are implemented in main.c and interact with the tree from tree.c)
bool parse_command(const char *command_str, parsed_command_t *parsed_cmd);
int db_set(const char *filename, const char *key, const char *value);
char *db_get(const char *filename, const char *key);
int db_del(const char *filename, const char *key);

// Client connection states
typedef enum
{
    CLIENT_CONNECTING,    // Just accepted, not fully initialized
    CLIENT_AUTHENTICATED, // Ready to process commands
    CLIENT_PROCESSING,    // Currently processing a command
    CLIENT_DISCONNECTING  // Being disconnected
} client_state_t;

// Client structure - represents each connected client
struct client
{
    int fd;                         // Socket file descriptor
    char ip[INET_ADDRSTRLEN];       // Client IP address string
    uint16_t port;                  // Client port number
    client_state_t state;           // Current client state
    char read_buffer[BUFFER_SIZE];  // Buffer for incoming data
    size_t read_pos;                // Current position in read buffer
    char write_buffer[BUFFER_SIZE]; // Buffer for outgoing data
    size_t write_len;               // Length of data to write
    size_t write_pos;               // Current position in write buffer
    time_t last_activity;           // Last activity timestamp (for timeouts)
    bool write_pending;             // True if we have data to write
};

// Server context structure
struct server_context
{
    int listen_fd;                       // Listening socket file descriptor
    int epoll_fd;                        // epoll file descriptor
    struct client *clients[MAX_CLIENTS]; // Array of client pointers
    int client_count;                    // Current number of connected clients
    bool running;                        // Server running flag
    uint16_t port;                       // Server port
};

// Global server context
extern struct server_context *g_server;

// Function declarations
int init_server(uint16_t port);
void main_loop(void);
void shutdown_handler(int sig);
int set_nonblocking(int fd);
struct client *create_client(int fd, struct sockaddr_in *addr);
void destroy_client(struct client *client);
int handle_new_connection(void);
int handle_client_read(struct client *client);
int handle_client_write(struct client *client);
void process_client_command(struct client *client, const char *command);
void send_to_client(struct client *client, const char *message);
void cleanup_server(void);

// Error logging macros (no changes needed for these, they are fine)
#ifdef DEBUG
#define debug_log(fmt, ...) printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
#else
#define debug_log(fmt, ...) // No-op in release mode
#endif

#define info_log(fmt, ...) printf("[INFO] " fmt "\n", ##__VA_ARGS__)
#define error_log(fmt, ...) fprintf(stderr, "[ERROR] " fmt "\n", ##__VA_ARGS__)

#endif /* MAIN_H */
