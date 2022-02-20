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
#include <sys/time.h>

#define SERVER_TCP_PORT		7000	// Default port
#define BUFLEN			    1024 	// Buffer length
#define CONNECTION_LIMIT    15000   // Max # of clients
#define ITER_LIMIT          10000   // Max # of iterations
typedef struct
{
    int  port, message_len,num_connections, iterations;
    char  *host;
    long response_times[ITER_LIMIT];
    int response_bytes[ITER_LIMIT];
    long total_time;
    int total_bytes;
    int total_requests;
}args;
pthread_cond_t cond1 = PTHREAD_COND_INITIALIZER;
int all_conn=1;
// declaring mutex
pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static void SystemFatal(const char* message)
{
    perror (message);
    exit (EXIT_FAILURE);
}

// Compute the delay between tl and t2 in milliseconds
long delay (struct timeval t1, struct timeval t2)
{
    long d;

    d = (t2.tv_sec - t1.tv_sec) * 1000;
    d += ((t2.tv_usec - t1.tv_usec + 500) / 1000);
    return(d);
}

void *client_thread(void *info_ptr) {
    int n, bytes_to_read,sd;
    args *a = (args *)info_ptr;
    int  port = a->port, message_len=a->message_len, num_connections = a->num_connections,iterations=a->iterations;
    char  *host = a->host;
    char *bp, rbuf[message_len+1], sbuf[message_len+1],  **pptr;
    char str[16];
    struct hostent	*hp;
    struct sockaddr_in server;
    static char *end = "\n";


    struct  timeval start_echo, end_echo;


    a->total_time = 0;
    a->total_bytes = 0;


    //NEED TO ADD VARIABLE TO CONTROL IF ALL CONNECT BEFORE SENDING
    memset(sbuf,'X',message_len);
    sbuf[message_len] = '\n';
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
        //printf("a\n");
        pthread_cond_wait(&cond1,&lock);
    } else {
        printf("all connected\n");
        pthread_cond_broadcast(&cond1);
    }
    pthread_mutex_unlock(&lock);
    //printf("Transmit:\n");

    // get user's text
    for(a->total_requests = 0; a->total_requests < iterations; a->total_requests++) {
        gettimeofday(&start_echo, NULL);
        a->total_bytes+=(message_len+1);
        send (sd, sbuf, message_len+1, 0);

        //printf("Receive:\n");
        bp = rbuf;
        bytes_to_read = message_len+1;

        // client makes repeated calls to recv until no more data is expected to arrive.
        n = 0;
        while ((n = recv (sd, bp, bytes_to_read, 0)) < (message_len+1))
        {
            bp += n;
            bytes_to_read -= n;

        }
        gettimeofday (&end_echo, NULL); // end delay measure
        a->response_times[a->total_requests] = delay(start_echo, end_echo);
        a->total_time += a->response_times[a->total_requests];
        //printf ("%s\n", rbuf);
        fflush(stdout);
    }

    printf("done\n");
    close (sd);
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

    int  port, num_connections, message_len, iterations;
    char  *host;


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
    args *thread_info[num_connections];
    pthread_t threads[num_connections];
    for(int i = 0; i < num_connections;i++) {
        thread_info[i] = malloc(sizeof(args));
        thread_info[i]->host=host;
        thread_info[i]->port = port;
        thread_info[i]->iterations=iterations;
        thread_info[i]->message_len = message_len;
        thread_info[i]->num_connections = num_connections;
        if(pthread_create(&threads[i], NULL, client_thread,(void * )thread_info[i])!= 0)
        {
            printf("error in pthread");
        }
    }
    for(int i =0; i<num_connections;i++) {
        pthread_join(threads[i], NULL);

    }
    for(int i =0; i<num_connections;i++) {
        free(thread_info[i]);

    }


    //fork or create threads here


    return (0);
}

