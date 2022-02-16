/*
-- Compile: gcc -Wall -ggdb -o epolls epoll_svr.c
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
#define EPOLL_QUEUE_LEN     256
#define BUFLEN              1024
#define SERVER_PORT         7000
#define BACKLOG             128
int fd_server;

static void SystemFatal(const char* message) {
    perror(message);
    exit(EXIT_FAILURE);
}

void close_fd(int signo)
{
    close(fd_server);
    exit(EXIT_SUCCESS);
}

void open_fd()
{
    if (listen (fd_server, BACKLOG) == -1)
        SystemFatal("listen");
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

int create_listening()
{
    fd_server = socket (AF_INET, SOCK_STREAM, 0);
    if (fd_server == -1)
        SystemFatal("socket");
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



static int read_sock(int fd)
{
    int	n, bytes_to_read,iterations;
    char	*bp, buf[BUFLEN];
    int count = 0;
    char *end = "/n";
    while (TRUE)
    {
        bp = buf;
        bytes_to_read = BUFLEN;
        while ((n = recv (fd, bp, bytes_to_read, 0)) < BUFLEN)
        {
            bp += n;
            bytes_to_read -= n;
            if (errno == EWOULDBLOCK)
            {
                continue;
            }
            //CHECK IF DATA IS DONE SENDING BEFORE CLOSING TO ADD
            //if(strstr(buf,end) != NULL)
            //  send(fd,buf,BUFLEN,0);
            //  close(fd);
            //  return TRUE
        }
        printf ("sending:%s\n", buf);
        send (fd, buf, BUFLEN, 0);
        //close (fd);
        return TRUE;
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


void epoll_connect(int *epoll,struct epoll_event *event)
{
    int fd_new;

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

    // Add the new socket descriptor to the epoll loop
    event->data.fd = fd_new;
    if (epoll_ctl (*epoll, EPOLL_CTL_ADD, fd_new, event) == -1)
        SystemFatal ("epoll_ctl");

    printf(" Remote Address:  %s\n", inet_ntoa(remote_addr.sin_addr));
}






void epoll_loop(int *epoll)
{
    int epoll_fd = *epoll;
    int num_fds,i,thread_id;
    static struct epoll_event events[EPOLL_QUEUE_LEN], event;
    event.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLET| EPOLLRDHUP;
    event.data.fd = fd_server;
    if (epoll_ctl (epoll_fd, EPOLL_CTL_ADD, fd_server, &event) == -1)
        SystemFatal("epoll_ctl");

    while (TRUE)
    {
        num_fds = epoll_wait (epoll_fd, events, EPOLL_QUEUE_LEN, -1);

        if (num_fds < 0)
            SystemFatal ("Error in epoll_wait!");
            omp_set_num_threads(num_fds);
            #pragma omp parallel private(thread_id)
            {
                #pragma omp for
                for (i = 0; i < num_fds; i++) {
                    // Case 1: Error condition
                    if (events[i].events & (EPOLLERR))
                    {
                        fputs("epoll: EPOLLERR", stderr);
                        close(events[i].data.fd);
                        continue;
                    }
                    //Case 2 close
                    if(events[i].events & (EPOLLHUP |EPOLLRDHUP))
                    {
                        printf("Connection closed\n");
                        close(events[i].data.fd);
                        continue;
                    }
                    assert (events[i].events & EPOLLIN);
                    //Case 3: Connection request
                    if(events[i].data.fd == fd_server) {
                        epoll_connect(&epoll_fd,&event);
                        continue;
                    }
                    //Case 4: A socket has read data
                    read_sock(events[i].data.fd);

            }

        }
    }
    close_fd(0);
}

int main(int argc, char* argv[]) {
    int port = SERVER_PORT;
    sig_handler();
    listen_sock_setup();
    bind_sock(port);
    open_fd();
    int epoll_fd;
    epoll_descriptor(&epoll_fd);
    epoll_loop(&epoll_fd);
    close_fd(1);
}