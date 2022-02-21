/*
-- Compile: gcc -Wall -ggdb -o epolls main.c -fopenmp
---------------------------------------------------------------------------------------*/

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <strings.h>
#include <arpa/inet.h>
#include <signal.h>
#include <omp.h>

#define TRUE 		        1
#define FALSE 		        0
#define EPOLL_QUEUE_LEN    50000
#define BUFLEN              1024
#define SERVER_PORT         7000
#define BACKLOG             10000
#define FLOC "server_log.txt" //client log
int fd_server;
int total_requests = 0;
int total_data =0;
int connections=0;
void process_client_data(struct epoll_event *event);
typedef struct {
    int sock;
    char *host;
    int requests;
    int data;
}client_data;
client_data *client_i[EPOLL_QUEUE_LEN];
static void SystemFatal(const char* message) {
    perror(message);
    exit(EXIT_FAILURE);
}

void close_fd(int signo)
{
    FILE *fptr = fopen(FLOC, "a");
    if(connections!=0 && total_requests !=0)
    {
        fprintf(fptr, "------------------\nServer results\n%d total connections\n%d total requests\n%d total data sent\n"
                      "Average of %d requests per connection\nAverage of %d bytes sent to each client\nAverage of %d bytes per message\n"
                ,connections, total_requests,total_data,total_requests/connections,total_data/connections,total_data/total_requests);
    }

    fclose(fptr);
    close(fd_server);
    exit(EXIT_SUCCESS);
}

void open_fd(int port)
{
    if (listen (fd_server, BACKLOG) == -1)
        SystemFatal("listen");
    printf("Server listening on port %d...\n", port);
}

int set_not_block(int sockfd)
{
    int flags, s;
    flags = fcntl(sockfd, F_GETFL, 0);
    if(flags == -1)
    {
        perror("fcntl");
        return -1;
    }
    flags |= O_NONBLOCK;
    s = fcntl(sockfd, F_SETFL, flags);
    if(s == -1)
    {
        perror("fcntl");
        return -1;
    }
    return 0;
}



// set up the signal handler to close the server socket when CTRL-c is received
void sig_handler()
{
    struct sigaction act;
    act.sa_handler = close_fd;
    act.sa_flags = 0;
    if ((sigemptyset (&act.sa_mask) == -1 || sigaction (SIGINT, &act, NULL) == -1))
    {
        perror ("Failed to set SIGINT handler");
        exit (EXIT_FAILURE);
    }
    if ((sigemptyset (&act.sa_mask) == -1 || sigaction (SIGTERM, &act, NULL) == -1))
    {
        perror ("Failed to set SIGTERM handler");
        exit (EXIT_FAILURE);
    }
}

void bind_sock(int port)
{
    struct sockaddr_in addr;
    memset (&addr, 0, sizeof (struct sockaddr_in));
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons(port);
    if (bind (fd_server, (struct sockaddr*) &addr, sizeof(addr)) == -1)
        SystemFatal("bind");
}

void listen_sock_setup()
{
    int arg;

    fd_server = socket (AF_INET, SOCK_STREAM, 0);
    if (fd_server == -1)
        SystemFatal("socket");

    arg = 1;
    if (setsockopt (fd_server, SOL_SOCKET, SO_REUSEADDR, &arg, sizeof(arg)) == -1)
        SystemFatal("setsockopt");

    set_not_block(fd_server);

}



static int read_sock(int fd, struct epoll_event *event)
{
    int	n = -1, bytes_to_read;
    char	*bp, buf[BUFLEN];

    char *end = "\n";
    bp = buf;
    bytes_to_read = BUFLEN;
    while (TRUE)
    {
        n = recv (fd, bp, bytes_to_read, MSG_DONTWAIT);
        if (n > 0)
        {
            bp += n;
            bytes_to_read -= n;
            //CHECK IF DATA IS DONE SENDING BEFORE CLOSING TO ADD
            if(strstr(buf,end) != NULL){
                //printf ("sending:%s\n", buf);
                int bytes_rec = BUFLEN - bytes_to_read;
                client_i[event->data.fd]->data+=bytes_rec;
                client_i[event->data.fd]->requests++;
                send(fd,buf,bytes_rec,0);
                return TRUE;
            }
            continue;
        }
        if(n == 0){
            process_client_data(event);
            close(fd);
            return FALSE;
        }
        if (errno == EINTR)
        {
            continue;
        }
        if (errno ==EAGAIN)
        {
            return FALSE;
        } else {
            process_client_data(event);
            close(fd);
            return FALSE;
        }

    }
    //close(fd);
    return FALSE;
}
void epoll_descriptor(int *epoll_fd)
{
    *epoll_fd = epoll_create(EPOLL_QUEUE_LEN);
    if (*epoll_fd == -1)
        SystemFatal("epoll_create");
}


void epoll_connect(int *epoll)
{
    int fd_new;
    static struct epoll_event event;
    struct sockaddr_in remote_addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    fd_new = accept (fd_server, (struct sockaddr*) &remote_addr, &addr_size);
    if (fd_new == -1)
    {
        if (errno != EAGAIN && errno != EWOULDBLOCK)
        {
            perror("accept");
        }
        return;
    }

    // Make the fd_new non-blocking
    //set_not_block(fd_new);
    if (fcntl (fd_new, F_SETFL, O_NONBLOCK | fcntl(fd_new, F_GETFL, 0)) == -1)
        SystemFatal("fcntl");
    //event->events = EPOLLIN | EPOLLET;
    // Add the new socket descriptor to the epoll loop
    event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET| EPOLLRDHUP | EAGAIN;
    event.data.fd = fd_new;
        if(!client_i[fd_new]) {
            client_i[fd_new] = malloc(sizeof(client_data));
            client_i[fd_new]->sock = fd_new;
            client_i[fd_new]->host = inet_ntoa(remote_addr.sin_addr);
            client_i[fd_new]->data = 0;
            client_i[fd_new]->requests = 0;
            //event.data.u64 = i;

        }else{
            SystemFatal("Error making client array");
        }


    if (epoll_ctl (*epoll, EPOLL_CTL_ADD, fd_new, &event) == -1)
        SystemFatal ("epoll_ctl");
    connections++;
    printf(" Remote Address:  %s\n", inet_ntoa(remote_addr.sin_addr));
}

void process_client_data(struct epoll_event *event)
{
    total_requests += client_i[event->data.fd]->requests;
    total_data += client_i[event->data.fd]->data;
    free(client_i[event->data.fd] );
    client_i[event->data.fd] = NULL;
}




void epoll_loop(int *epoll)
{
    int epoll_fd = *epoll;
    int num_fds,i;
    for (i = 0; i < EPOLL_QUEUE_LEN; i++)
        client_i[i] = NULL;

    static struct epoll_event events[EPOLL_QUEUE_LEN], event;
    event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET| EPOLLRDHUP | EAGAIN;
    event.data.fd = fd_server;
    if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, fd_server, &event) == -1)
        SystemFatal("epoll_ctl");

    while (TRUE)
    {
        num_fds = epoll_wait (epoll_fd, events, EPOLL_QUEUE_LEN, -1);

        if (num_fds < 0)
            //SystemFatal ("Error in epoll_wait!");
            continue;

        //omp_set_num_threads(num_fds);
        #pragma omp parallel
        {
            #pragma omp for
            for (i = 0; i < num_fds; i++) {
                // Case 1: Error condition
                if (events[i].events & (EPOLLERR))
                {
                    fputs("epoll: EPOLLERR", stderr);
                    process_client_data(&events[i]);
                    close(events[i].data.fd);
                    continue;
                }
                //Case 2 close
                if(events[i].events & (EPOLLHUP |EPOLLRDHUP))
                {
                    printf("Connection from %s closed\n", client_i[events[i].data.u64]->host);
                    process_client_data(&events[i]);
                    close(events[i].data.fd);
                    continue;
                }
                assert (events[i].events & EPOLLIN);
                //Case 3: Connection request
                if(events[i].data.fd == fd_server) {
                    epoll_connect(&epoll_fd);
                    continue;
                }
                //Case 4: A socket has read data
                read_sock(events[i].data.fd, &events[i]);

            }

        }
    }
    close_fd(0);
}

int main(int argc, char* argv[]) {

    int port;
    switch(argc)
    {
        case 1:
            port = SERVER_PORT;	// Use the default port
            break;
        case 2:
            port = atoi(argv[1]);	// Get user specified port
            break;
        default:
            fprintf(stderr, "Usage: %s [port]\n", argv[0]);
            exit(1);
    }
    sig_handler();
    listen_sock_setup();
    bind_sock(port);
    open_fd(port);
    int epoll_fd;
    epoll_descriptor(&epoll_fd);
    epoll_loop(&epoll_fd);
    close_fd(1);
}