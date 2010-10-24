/*
 * Copyright (c) 2010 John Gordon <jgor@indiecom.org>
 *   and Doug Farre <dougfarre@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <pthread.h>

/* 1 enables debug messages, 0 disables */
#define DEBUG 1

/* max connections the irc daemon will accept */
#define MAX_CONNS 10

/* max message buffer size */
#define MAX_BUF 255

/*
 * name: get_in_addr
 * return: ipv4 or ipv6 sockaddr
 * description: returns the appropriate protocol-specific sockaddr (ipv4/ipv6)
 *   given a generic sockaddr.
 */
void *get_in_addr(struct sockaddr *sa) {
  if (sa->sa_family == AF_INET)
    return &(((struct sockaddr_in*)sa)->sin_addr);

  return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

/*
 * name: sigchld_handler
 * return: none
 * description: child process handler, used to avoid zombie child processes.
 */
void sigchld_handler(int s) {
  while (waitpid(-1, NULL, WNOHANG) > 0);
}

/*
 * name: error
 * return: none
 * description: displays the error message via perror then exits with failure
 *   code.
 */
void error(char *msg) {
  perror(msg);
  exit(EXIT_FAILURE);
}

/*
 * name: init_srv
 * return: listening server socket file descriptor
 * description: establishes a listening socket on the specified port.
 */
int init_srv(int port) {
  int srv_sockfd, err;
  struct addrinfo hints, *res, *res0;
  struct sigaction sa;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  char port_str[6];
  if (port < 1 || port > 65535) fprintf(stderr, "Invalid port\n");
  sprintf(port_str, "%d", port);
  err = getaddrinfo(NULL, port_str, &hints, &res0);
  if (err) {
    fprintf(stderr, "ERROR on getaddrinfo: %s\n", gai_strerror(err));
    exit(EXIT_FAILURE);
  }

  for (res = res0; res != NULL; res = res->ai_next) {
    srv_sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (srv_sockfd == -1) continue;

    int reuse = 1;
    err = setsockopt(srv_sockfd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int));
    if (err == -1) error("ERROR on setsockopt");

    err = bind(srv_sockfd, res->ai_addr, res->ai_addrlen);
    if (err == -1) continue;

    break;
  }

  if (res == NULL) {
    fprintf(stderr, "ERROR binding\n");
    exit(EXIT_FAILURE);
  }

  freeaddrinfo(res0);

  err = listen(srv_sockfd, MAX_CONNS);
  if (err == -1) error("ERROR on listen");

  sa.sa_handler = sigchld_handler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  err = sigaction(SIGCHLD, &sa, NULL);
  if (err == -1) error("ERROR on sigaction");

  return srv_sockfd;
}

/*
 * name: init_cli
 * return: server<->client socket file descriptor
 * description: creates a socket for incoming client connections.
 */
int init_cli(int srv_sockfd) {
  int cli_sockfd, err;
  struct sockaddr_storage cli_addr;
  int cli_len = sizeof(cli_addr);

  cli_sockfd = accept(srv_sockfd, (struct sockaddr *) &cli_addr, &cli_len);
  if (cli_sockfd == -1) error("ERROR on accept");
 return cli_sockfd;
}

/*
 * name: raw
 * return: 0 on success
 * description: the entry point for the listening raw daemon.
 */
int raw(int port) {
  int srv_sockfd, cli_sockfd, err;
  char buf[MAX_BUF];
  fd_set master;
  fd_set read_fds;
  int fdmax;
  char remoteIP[INET6_ADDRSTRLEN];
  int i, j, nbytes;

  srv_sockfd = init_srv(port);

  FD_ZERO(&master);
  FD_ZERO(&read_fds);

  FD_SET(srv_sockfd, &master);

  fdmax = srv_sockfd;

  while(1) {
    read_fds = master;
    err = select(fdmax + 1, &read_fds, NULL, NULL, NULL);
    if (err == -1) error("ERROR on select");

    for (i = 0; i <= fdmax; i++) {
      if (FD_ISSET(i, &read_fds)) {
        if (i == srv_sockfd) {
          cli_sockfd = init_cli(srv_sockfd);
          FD_SET(cli_sockfd, &master);
          if (cli_sockfd > fdmax) fdmax = cli_sockfd;
        }
        else {
          nbytes = read(i, buf, sizeof(buf));
          if (nbytes == -1) error("ERROR on read");
          else if (nbytes == 0) {
            close(i);
            FD_CLR(i, &master);
          }
          else {
            for (j = 0; j <= fdmax; j++) {
              if (FD_ISSET(j, &master)) {
                if (j != srv_sockfd && j != i) {
                  err = write(j, buf, nbytes);
                  if (err == -1) error("ERROR on write");
                }
              }
            }
          }
        }
      }
    }
  }

  return 0;
}

int main() {

  raw(6601);

  return EXIT_SUCCESS;
}

