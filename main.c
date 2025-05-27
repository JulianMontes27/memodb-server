/* main.c */
#include "main.h"

bool scontinuation;

void mainloop(void)
{
    struct sockaddr_in sock;
    int s;

    sock.sin_family = AF_INET;
    sock.sin_port = htons(PORT);
    sock.sin_addr.s_addr = inet_addr(HOST);

    s = socket(AF_INET, SOCK_STREAM, 0);
};

int main(int argc, const char *argv[])
{
}