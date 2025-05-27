/* main.c */
#include "main.h"

bool scontinuation; // Flag for the main while loop

void mainloop(uint16_t port)
{
    // Create a socket address struct that defines how clients will connect to the db server. Handles IPv4 addresses
    struct sockaddr_in sock;
    int s;

    sock.sin_family = AF_INET;              // IPv4 protocol
    sock.sin_port = htons(port);            // Crucial - it converts your port number from host byte order to network byte order. Different CPU architectures store multi-byte numbers differently (big-endian vs little-endian), but network protocols always use big-endian. So htons() ensures your port number is transmitted correctly regardless of your machine's architecture
    sock.sin_addr.s_addr = inet_addr(HOST); // converts a string IP address (like "127.0.0.1") into the 32-bit binary format that the socket system expects

    s = socket(AF_INET, SOCK_STREAM, 0);
    assert(s > 0);

    errno = 0;

    // bind() returns success with 0, so we check if bind() results in 1 or true for errors
    if (bind(s, (struct sockaddr *)&sock, sizeof(sock)))
    {
        assert_perror(errno);
    }

    errno = 0;
    if (listen(s, 20))
    {
        assert_perror(errno);
    }; // accept 20 connections at a time

    scontinuation = false;
};

int main(int argc, const char *argv[])
{
    char *sport;
    int port;

    // If the # of args passed to main is less than 2...
    if (argc < 2)
    {
        sport = PORT; // Specify a default PORT value
    }
    else
    {
        sport = argv[1];
    }

    port = (int)atoi(sport);

    scontinuation = true;
    while (scontinuation == true)
    {
        mainloop(port);
    }

    return 0;
}