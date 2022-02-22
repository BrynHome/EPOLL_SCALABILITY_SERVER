/*
-- Compile: gcc -Wall -ggdb -o epolls main.c -fopenmp
---------------------------------------------------------------------------------------*/

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

#define TRUE 		        1
#define FALSE 		        0
#define EPOLL_QUEUE_LEN     256
#define BUFLEN              1024
#define SERVER_PORT         7000

#define FLOC "server_log.txt" //client log
int fd_server;
int total_requests = 0;
int total_data =0;
int connections=0;


typedef struct {
    char host[253];
    int requests;
    int data;
}client_data;
void adjust_client_info(client_data *clients, int fd, int bytes);
void close_fd(int signo);
void open_fd(int port);
void sig_handler();
void bind_sock(int port);
void listen_sock_setup();
static int read_sock(int fd);
_Noreturn void epoll_loop(int *epoll);
void close_conn(int epoll_fd, int fd);
void epoll_connect(int epoll);
void epoll_descriptor(int *epoll_fd);

client_data client_i[1000];
static void SystemFatal(const char* message) {
    close(fd_server);
    perror(message);
    exit(EXIT_FAILURE);
}

void close_fd(int signo)
{
    FILE *fptr = fopen(FLOC, "a+");
    if(fptr == NULL)
    {
        SystemFatal("error opening file");
    }
    int i;
    fprintf(fptr, "-------------------------\n");
    for(i =0;i< 1000; i++)
    {
        if(client_i[i].host[0] == '\0')
            break;
        fprintf(fptr, "Host %s sent %d messages totalling %d bytes\n"
                ,client_i[i].host,client_i[i].requests,client_i[i].data);
    }
    printf("Results written, closing server");


    if(fclose(fptr))
    {
        fprintf(stderr, "error closing file");
    }
    close(fd_server);
    exit(EXIT_SUCCESS);
}

void open_fd(int port)
{
    if (listen (fd_server, SOMAXCONN) == -1)
        SystemFatal("listen");
    printf("Server listening on port %d...\n", port);
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

    if (fcntl(fd_server, F_SETFL, O_NONBLOCK | fcntl (fd_server, F_GETFL, 0)) == -1)
        SystemFatal("Set blocking");

}



static int read_sock(int fd)
{
    int	n = -1, bytes_to_read;
    char	*bp, buf[BUFLEN];

    char *end = "\n";
    bp = buf;
    bytes_to_read = BUFLEN;
    while (TRUE)
    {
        n = recv (fd, bp, bytes_to_read, 0);
        if(n == -1)
        {
            if(errno == EWOULDBLOCK)
                continue;
            return FALSE;
        }
        if (n > 0)
        {
            bp += n;
            bytes_to_read -= n;
            //CHECK IF DATA IS DONE SENDING BEFORE CLOSING TO ADD
            if(strstr(buf,end) != NULL){
                //printf ("sending:%s\n", buf);
                int bytes_rec = BUFLEN - bytes_to_read;
                adjust_client_info(client_i,fd,bytes_rec);
                send(fd,buf,bytes_rec,0);
                return TRUE;
            }
            continue;
        }
        if(n == 0){
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

void adjust_client_info(client_data *clients, int fd, int bytes)
{
    int i;
    struct sockaddr_in addr;
    socklen_t addr_size = sizeof(struct sockaddr_in);
    char host[253];
    getpeername(fd,(struct sockaddr *) &addr, &addr_size);
    strcpy(host, inet_ntoa(addr.sin_addr));

    #pragma omp critical
    {
        for (i = 0; i < EPOLL_QUEUE_LEN; i++) {
            if(clients[i].host[0] != '\0')
            {
                if(strcmp(clients[i].host,host) == 0)
                {
                    clients[i].data+=bytes;
                    clients[i].requests++;
                    break;
                }
            }else{
                strcpy(clients[i].host,host);
                clients[i].requests =1;
                clients[i].data = bytes;
                break;
            }

        }
    };
}


void epoll_connect(int epoll)
{
    int fd_new;
    struct epoll_event event;
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
    memset(&event,0,sizeof (struct epoll_event));
    event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP;
    event.data.fd = fd_new;
    if (epoll_ctl (epoll, EPOLL_CTL_ADD, fd_new, &event) == -1)
        SystemFatal ("epoll_ctl");


    printf(" Remote Address:  %s\n", inet_ntoa(remote_addr.sin_addr));
}



void close_conn(int epoll_fd, int fd)
{
    if(epoll_fd != -1)
        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
    if(close(fd) == -1)
        SystemFatal("Error closing socket");
}


_Noreturn void epoll_loop(int *epoll)
{
    int epoll_fd = *epoll;
    int num_fds,i;


    static struct epoll_event events[EPOLL_QUEUE_LEN], event;
    event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP ;
    event.data.fd = fd_server;
    if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, fd_server, &event) == -1)
        SystemFatal("epoll_ctl");

    while (TRUE)
    {
        num_fds = epoll_wait (epoll_fd, events, EPOLL_QUEUE_LEN, -1);
        if (num_fds < 0)
            SystemFatal ("Error in epoll_wait!");
            //continue;
        //omp_set_num_threads(num_fds);
        #pragma omp parallel
        {
            #pragma omp for
            for (i = 0; i < num_fds; i++) {
                // Case 1: Error condition
                if (events[i].events & (EPOLLERR)) {
                    fputs("epoll: EPOLLERR", stderr);
                    close_conn(epoll_fd,events[i].data.fd);
                    continue;
                }
                //Case 2: Connection request
                if (events[i].data.fd == fd_server) {
                    epoll_connect(epoll_fd);
                    continue;
                }
                //Case 3 close
                if (events[i].events & (EPOLLHUP | EPOLLRDHUP)) {
                    close_conn(epoll_fd,events[i].data.fd);
                    continue;
                }


                //Case 4: A socket has read data
                read_sock(events[i].data.fd);

            }

        };
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