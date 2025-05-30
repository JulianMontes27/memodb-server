/* main.h - Header file containing all includes, defines, and function declarations */
#ifndef MEMODB_H
#define MEMODB_H

// This macro enables GNU-specific extensions in the C library
#define _GNU_SOURCE

// Standard C library headers
#include <stdio.h>   // For printf, perror, etc.
#include <unistd.h>  // For UNIX specific system calls like close()
#include <stdlib.h>  // For exit(), atoi(), memory allocation
#include <string.h>  // For string manipulation functions
#include <stdint.h>  // For fixed-width integer types like uint16_t
#include <assert.h>  // For debugging assertions
#include <errno.h>   // For error handling (errno variable)
#include <stdbool.h> // For boolean type (true/false)
#include <sys/wait.h> // For waitpid() and wait status macros
#include <signal.h>   // For signal handling

// Network programming headers (socket programming)
#include <arpa/inet.h>  // For inet_addr(), htons() - IP address conversion
#include <sys/socket.h> // For socket(), bind(), listen(), accept()
#include <netinet/in.h> // For sockaddr_in structure

// Configuration constants
#define HOST "127.0.0.1" // Localhost IP address (loopback)
#define PORT "12049"     // Default port number as string

#define NoError 0 // Success return code

// Macro for error handling - sets errno and returns NULL
#define reterr(x)    \
    do               \
    {                \
        errno = (x); \
        return NULL; \
    } while (0)

    
struct s_client
{
    int s; // file descriptor
    char ip[16];
    uint16_t port;
};

// Function declarations (prototypes)
void main_loop(int socket_fd);          // Main server loop
int init_server(uint16_t port);         // Initialize and setup server socket
int main(int argc, const char *argv[]); // Program entry point

#endif /* MEMODB_H */

