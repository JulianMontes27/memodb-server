#ifndef MAIN_H
#define MAIN_H

#define _GNU_SOURCE

#include <stdio.h>    // For standard input/output functions (e.g., printf, perror)
#include <unistd.h>   // For POSIX operating system API (e.g., access, sleep)
#include <stdlib.h>   // For general utilities (e.g., malloc, free, NULL)
#include <string.h>   // For string manipulation functions (e.g., memset, snprintf)
#include <stdint.h>   // For fixed-width integer types (e.g., uint8_t, int16_t)
#include <assert.h>   // For the assert() macro, used for debugging and pre-condition checks
#include <errno.h>    // For error number definitions (e.g., EFAULT, ENOMEM)
#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>


#define HOST    "127.0.0.1"
#define PORT    12000

#define NoError 0


// reterr: A macro for returning NULL from a function and setting the global `errno` variable.
// This is designed for functions that return a pointer type.
// It uses a do-while(0) loop to ensure safe expansion in all contexts (e.g., if-else statements).
#define reterr(x) \
    do { \
        errno = (x); \
        return NULL; \
    } while(0)

void mainloop(void);
  
#endif /* MAIN_H */