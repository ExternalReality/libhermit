/*
 * Copyright (c) 2018, Annika Wierichs, RWTH Aachen University.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *    * Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *    * Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *    * Neither the name of the University nor the names of its contributors
 *      may be used to endorse or promote products derived from this
 *      software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/socket.h>
#include <netinet/in.h>

#include <hermit/ibv_hermit_cm.h>


#define KEY_MSG_SIZE (59)

/*
 * Helper functions:
 */

int check_add_port(char **service, int port, const char *server_ip, struct addrinfo *hints,
                   struct addrinfo **res)
{
  int str_size_max = 6;
  *service = calloc(str_size_max, sizeof(char));
	if (snprintf(*service, str_size_max, "%05d", port) < 0) {
		return 1;
	}

	if (getaddrinfo(server_ip, *service, hints, res) < 0) {
    fprintf(stderr, "Error for %s:%d\n", server_ip, port);
		return 1;
	}

	return 0;
}

int eth_read(int sockfd, struct pingpong_dest *rem_dest)
{
  int parsed;
  char msg[KEY_MSG_SIZE];

  if (read(sockfd, msg, sizeof msg) != sizeof msg) {
    fprintf(stderr, "eth_read: Couldn't read remote address.\n");
    return 1;
  }

  parsed = sscanf(msg, KEY_PRINT_FMT,
                  (unsigned int *) &rem_dest->lid, &rem_dest->out_reads, &rem_dest->qpn,
                  &rem_dest->psn, &rem_dest->rkey, &rem_dest->vaddr, &rem_dest->srqn);

  if (parsed != 7) {
    fprintf(stderr, "Couldn't parse line <%.*s>\n",(int)sizeof msg, msg);
    return 1;
  }

  return 0;
}

int eth_write(int sockfd, struct pingpong_dest *local_dest)
{
  char msg[KEY_MSG_SIZE];
  sprintf(msg, KEY_PRINT_FMT,
          local_dest->lid, local_dest->out_reads, local_dest->qpn, local_dest->psn,
          local_dest->rkey, local_dest->vaddr, local_dest->srqn);

  if (write(sockfd, msg, sizeof msg) != sizeof msg) {
    perror("Client Write");
    fprintf(stderr, "Couldn't send local address.\n");
    return 1;
  }

  return 0;
}

/*
 * Exposed functions:
 */


int eth_client_connect(const char *server_ip, int port)
{
	struct addrinfo *res, *t;
	struct addrinfo hints;
	char *service;
  int sockfd = -1;

	memset(&hints, 0, sizeof hints);
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	if (check_add_port(&service, port, server_ip, &hints, &res)) {
		fprintf(stderr, "Problem in resolving basic address and port\n");
		return -1;
	}

	for (t = res; t; t = t->ai_next) {
		sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);

		if (sockfd >= 0) {
			if (!connect(sockfd, t->ai_addr, t->ai_addrlen))
				break; // Success.

			close(sockfd);
			sockfd = -1;
		}
	}

	freeaddrinfo(res);

	if (sockfd < 0) {
		fprintf(stderr, "Couldn't connect to %s:%d\n", server_ip, port);
		return -1;
	}

  return sockfd;
}

int eth_server_connect(int port)
{
  struct addrinfo *res, *t;
  struct addrinfo hints;
  char *service;
  int n;
  int sockfd = -1, connfd;

  memset(&hints, 0, sizeof hints);
  hints.ai_flags    = AI_PASSIVE;
  hints.ai_family   = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  if (check_add_port(&service, port, NULL, &hints, &res)) {
    fprintf(stderr, "Problem in resolving basic address and port\n");
    return -1;
  }

  for (t = res; t; t = t->ai_next) {
    sockfd = socket(t->ai_family, t->ai_socktype, t->ai_protocol);

    if (sockfd >= 0) {
      n = 1;
      setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &n, sizeof n);

      if (!bind(sockfd, t->ai_addr, t->ai_addrlen))
        break; // Success

      close(sockfd);
      sockfd = -1;
    }
  }
  freeaddrinfo(res);

  if (sockfd < 0) {
    fprintf(stderr, "Couldn't listen to port %d\n", port);
    return -1;
  }

  listen(sockfd, 1);
  connfd = accept(sockfd, NULL, 0);

  if (connfd < 0) {
    perror("Server Accept");
    fprintf(stderr, "accept() failed\n");
    close(sockfd);
    return -1;
  }

  close(sockfd);
  return connfd;
}

int eth_client_exch_dest(int sockfd, struct pingpong_dest *local_dest,
                         struct pingpong_dest *rem_dest)
{
  if (eth_write(sockfd, local_dest)) {
    fprintf(stderr, " Unable to write local destination information to socket.\n");
    return 1;
  }

  if (eth_read(sockfd, rem_dest)) {
    fprintf(stderr, " Unable to read remote destination information from socket.\n");
    return 1;
  }

  return 0;
}

int eth_server_exch_dest(int sockfd, struct pingpong_dest *local_dest,
                         struct pingpong_dest *rem_dest)
{
  if (eth_read(sockfd, rem_dest)) {
    fprintf(stderr, " Unable to read remote destination information from socket.\n");
    return 1;
  }

  if (eth_write(sockfd, local_dest)) {
    fprintf(stderr, " Unable to write local destination information to socket.\n");
    return 1;
  }

  return 0;
}

int eth_close(int sockfd)
{
  if (close(sockfd)) {
    fprintf(stderr, "Couldn't close socket.\n");
    return -1;
  }

  return 0;
}