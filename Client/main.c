#pragma clang diagnostic push
#pragma ide diagnostic ignored "openmp-use-default-none"
/*
--  Compile: gcc -Wall -ggdb -o epollc epoll_clnt.c -fopenmp
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
#include <omp.h>

#define SERVER_TCP_PORT		7000	// Default port
#define BUFLEN			    1024 	// Buffer length
#define CONNECTION_LIMIT    15000   // Max # of clients
#define ITER_LIMIT          10000   // Max # of iterations
long delay (struct timeval t1, struct timeval t2);
pthread_cond_t cond1 = PTHREAD_COND_INITIALIZER;

// declaring mutex
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void SystemFatal(const char* message)
{
    perror (message);
    exit (EXIT_FAILURE);
}

int setup(int *num_connections, int *message_len, int *iterations, int *connect_all)
{
    char ibuf[BUFLEN];
    printf("Num clients(Limit here): ");
    fgets(ibuf, BUFLEN, stdin);
    *num_connections = atoi(ibuf);
    if(1 > *num_connections||*num_connections > CONNECTION_LIMIT)
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
    if(1 > *iterations||*iterations > ITER_LIMIT)
    {
        SystemFatal("Improper number of iterations");
        return 0;
    }

    printf("Wait for all clients to connect before sending?(1 for no, 2 for yes): ");
    fgets(ibuf, BUFLEN, stdin);
    *connect_all = atoi(ibuf);
    if(1 == *connect_all || *connect_all == 2)
    {
        return 1;

    } else{
        SystemFatal("Improper set for wait for connection.");
        return 0;
    }

}

int main (int argc, char **argv)
{

    int port;
    int num_connections, message_len, iterations, connect_all;
    struct hostent	*hp;
    struct sockaddr_in server;
    char  *host, sbuf[BUFLEN];
    char str[16];
    int thread_num=0,num_core =1;


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

    if(!setup(&num_connections,&message_len,&iterations,&connect_all))
    {
        SystemFatal("Setup issue");
    }


    memset(sbuf,'X',message_len);
    sbuf[message_len] = '\0';
    int all_conn=1;
    omp_set_num_threads(num_connections);
    bzero((char *)&server, sizeof(struct sockaddr_in));
    server.sin_family = AF_INET;
    server.sin_port = htons(port);

    if ((hp = gethostbyname(host)) == NULL)
    {
        fprintf(stderr, "Unknown server address\n");
        exit(1);
    }
    bcopy(hp->h_addr, (char *)&server.sin_addr, hp->h_length);
    //fork or create threads here
    #pragma omp parallel private(thread_num,num_core)
    {
        int n, bytes_to_read,sd;
        char *bp, rbuf[BUFLEN],  **pptr;
        num_core = omp_get_num_threads();
        thread_num = omp_get_thread_num();
        //NEED TO ADD VARIABLE TO CONTROL IF ALL CONNECT BEFORE SENDING
        if(connect_all == 2) {
            pthread_mutex_lock(&lock);
        }
        // Create the socket
        if ((sd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
        {
            perror("Cannot create socket");
            exit(1);
        }
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(sd,SOL_SOCKET,SO_RCVTIMEO, (const char*)&tv, sizeof tv);

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
        if(connect_all == 2) {
            if(all_conn < num_connections) {
                all_conn++;
                pthread_cond_wait(&cond1,&lock);
            } else {
                printf("all connected\n");
                pthread_cond_signal(&cond1);
            }
            pthread_mutex_unlock(&lock);
        }

        //printf("Transmit:\n");

        // get user's text
        for(int b = 0; b < iterations;b++) {
            send (sd, sbuf, BUFLEN, 0);

            //printf("Receive:\n");
            bp = rbuf;
            bytes_to_read = BUFLEN;

            // client makes repeated calls to recv until no more data is expected to arrive.
            n = 0;
            while ((n = recv (sd, bp, bytes_to_read, 0)) < BUFLEN)
            {
                bp += n;
                bytes_to_read -= n;
            }
            //printf ("%s %d\n", rbuf,thread_num);
            fflush(stdout);
        }
        //fgets (sbuf, BUFLEN, stdin);

        // Transmit data through the socket
        printf ("%d done\n", thread_num);
        close (sd);
    }

    return (0);
}

long delay (struct timeval t1, struct timeval t2)
{
    long d;

    d = (t2.tv_sec - t1.tv_sec) * 1000;
    d += ((t2.tv_usec - t1.tv_usec + 500) / 1000);
    return(d);
}


#pragma clang diagnostic pop