/*
--  Compile: gcc -Wall -ggdb -o epollc epoll_clnt.c
---------------------------------------------------------------------------------------*/
#include <stdio.h>
#include <netdb.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <errno.h>
#include <stdlib.h>
#include <strings.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <string.h>
#include <pthread.h>

#define SERVER_TCP_PORT		7000	// Default port
#define BUFLEN			    1024 	// Buffer length
#define CONNECTION_LIMIT    15000   // Max # of clients
#define ITER_LIMIT          10000   // Max # of iterations
pthread_cond_t cond1 = PTHREAD_COND_INITIALIZER;

// declaring mutex
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void SystemFatal(const char* message)
{
    perror (message);
    exit (EXIT_FAILURE);
}

int setup(int *num_connections, int *message_len, int *iterations)
{
    char ibuf[BUFLEN];
    printf("Num clients(Limit here): ");
    fgets(ibuf, BUFLEN, stdin);
    *num_connections = atoi(ibuf);
    if(1 > *num_connections||*num_connections > 15000)
    {
        SystemFatal("Improper number of connections");
        return 0;
    }

    printf("Length of message in bytes(1 - 1023): ");
    fgets(ibuf, BUFLEN, stdin);
    *message_len = atoi(ibuf);
    if(1 > *message_len||*message_len > 1024)
    {
        SystemFatal("Improper message length");
        return 0;
    }

    printf("Times to send message(Limit here): ");
    fgets(ibuf, BUFLEN, stdin);
    *iterations = atoi(ibuf);
    if(1 > *iterations||*iterations > 10000)
    {
        SystemFatal("Improper number of iterations");
        return 0;
    }
    return 1;
}

int main (int argc, char **argv)
{
    int n, bytes_to_read;
    int sd, port;
    int num_connections, message_len, iterations;
    struct hostent	*hp;
    struct sockaddr_in server;
    char  *host, *bp, rbuf[BUFLEN], sbuf[BUFLEN],  **pptr;
    char str[16];


    switch(argc)
    {
        case 2:
            host =	argv[1];	// Host name
            port =	SERVER_TCP_PORT;
            break;
        case 3:
            host =	argv[1];
            port =	atoi(argv[2]);	// User specified port
            break;
        default:
            fprintf(stderr, "Usage: %s host [port]\n", argv[0]);
            exit(1);
    }

    if(!setup(&num_connections,&message_len,&iterations))
    {
        SystemFatal("Setup issue");
    }


    memset(sbuf,'X',message_len);
    sbuf[message_len] = '\0';
    int all_conn=0;

    //fork or create threads here

    //NEED TO ADD VARIABLE TO CONTROL IF ALL CONNECT BEFORE SENDING
    pthread_mutex_lock(&lock);
    // Create the socket
    if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("Cannot create socket");
        exit(1);
    }

    bzero((char *)&server, sizeof(struct sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);

    if ((hp = gethostbyname(host)) == NULL)
    {
        fprintf(stderr, "Unknown server address\n");
        exit(1);
    }
    bcopy(hp->h_addr, (char *)&server.sin_addr, hp->h_length);

    // Connecting to the server
    if (connect (sd, (struct sockaddr *)&server, sizeof(server)) == -1)
    {
        fprintf(stderr, "Can't connect to server\n");
        perror("connect");
        exit(1);
    }

    printf("Connected:    Server Name: %s\n", hp->h_name);
    pptr = hp->h_addr_list;
    printf("\t\tIP Address: %s\n", inet_ntop(hp->h_addrtype, *pptr, str, sizeof(str)));
    if(all_conn < num_connections) {
        all_conn++;
        pthread_cond_wait(&cond1,&lock);
    } else {
        printf("all connected");
        pthread_cond_signal(&cond1);
    }
    pthread_mutex_unlock(&lock);
    printf("Transmit:\n");

    // get user's text
    for(int b = 0; b < iterations;b++) {
        send (sd, sbuf, BUFLEN, 0);

        printf("Receive:\n");
        bp = rbuf;
        bytes_to_read = BUFLEN;

        // client makes repeated calls to recv until no more data is expected to arrive.
        n = 0;
        while ((n = recv (sd, bp, bytes_to_read, 0)) < BUFLEN)
        {
            bp += n;
            bytes_to_read -= n;
        }
        printf ("%s\n", rbuf);
        fflush(stdout);
    }
    //fgets (sbuf, BUFLEN, stdin);

    // Transmit data through the socket

    close (sd);
    return (0);
}

