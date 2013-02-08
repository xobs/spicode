#include <stdio.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <errno.h>
#include <strings.h>
#include <unistd.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "sd.h"

int net_write_data(struct sd *server, void *data, size_t count) {
    return write(server->net_fd, data, count);
}

int net_fd(struct sd *server) {
	return server->net_fd;
}

/* Note: This assumes the client is very well behaved (e.g. it sends
 * complete commands in a single packet).
 * It will block until a packet is received.
 */
int net_get_packet(struct sd *server, uint8_t **data) {
    int ret;
    int tries;

    tries = 0;
    ret = -1;
    while(tries++ < NET_MAX_TRIES && ret < 0) {
        
        ret = read(server->net_fd, server->net_bfr, sizeof(server->net_bfr));

        /* Interrupted during read (unlikely).  Try again. */
        if (ret == -1 && (errno == EINTR || errno == EAGAIN)) {
            perror("Read error");
            continue;
        }

        /* All other errors (and successes) are final */
        break;
    }

    /* Generic "unable to read" error */
    if (ret == -1) {
        perror("Unable to read()");
        close(server->net_socket);
    }

    /* Client closed connection */
    else if (ret == 0) {
        fprintf(stderr, "Other side closed connection\n");
        close(server->net_socket);
        server->net_socket = 0;
    }
    else {
        *data = server->net_bfr;
    }

    return ret;
}

int net_accept(struct sd *server) {
    socklen_t len = sizeof(server->net_sockaddr);
    printf("Listening on port %d\n", server->net_port);
    server->net_fd = accept(server->net_socket,
                            (struct sockaddr *)&(server->net_sockaddr),
                            &len);
    printf("Connection from %s\n", inet_ntoa(server->net_sockaddr.sin_addr));

    return server->net_fd;
}

static int net_init_socket(struct sd *server) {
    int res;
    int val;
    socklen_t len;

    /* Set up the TCP channel */
    server->net_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server->net_socket < 0) {
        perror("Couldn't call socket()");
        return -1;
    }

    bzero(&server->net_sockaddr, sizeof(server->net_sockaddr));
    server->net_sockaddr.sin_family = AF_INET;
    server->net_sockaddr.sin_port = htons(server->net_port);
    server->net_sockaddr.sin_addr.s_addr = INADDR_ANY;

    res = bind(server->net_socket,
              (struct sockaddr *)&(server->net_sockaddr),
              sizeof(server->net_sockaddr));
    if (res != 0) {
        close(server->net_socket);
        perror("Couldn't call bind()");
        return -1;
    }

    res = listen(server->net_socket, NET_MAX_CONNECTIONS);
    if (res != 0) {
        perror("Couldn't call listen()");
        close(server->net_socket);
        return -1;
    }

    len = sizeof(server->net_buf_len);
    getsockopt(server->net_socket, SOL_SOCKET, SO_RCVBUF,
              &server->net_buf_len, &len);

    len = sizeof(val);
    val = 1;
    setsockopt(server->net_socket, SOL_SOCKET, SO_REUSEADDR, &val, len);

    return 0;
}


int net_init(struct sd *server) {

    server->net_port = NET_DATA_PORT;

    /* Set up UDP data channel */
    return net_init_socket(server);
}

int net_deinit(struct sd *server) {
    close(server->net_fd);
    close(server->net_socket);

    return 0;
}
