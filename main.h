#ifndef MAIN_H
#define MAIN_H

#define _GNU_SOURCE

#include <stdio.h>
#include <unistd.h>   
#include <stdlib.h>   
#include <string.h>   
#include <stdint.h>   
#include <assert.h>   
#include <errno.h>    
#include <stdbool.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>

#define HOST    "127.0.0.1"
#define PORT    "25000"

#define NoError 0

#define reterr(x) \
    do { \
        errno = (x); \
        return NULL; \
    } while(0)

void mainloop(uint16_t port);
  
#endif /* MAIN_H */