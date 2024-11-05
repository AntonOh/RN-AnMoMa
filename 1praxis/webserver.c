#include <stdio.h>
#include <stdlib.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>

/**
* Derives a sockaddr_in structure from the provided host and port information.
*
* @param host The host (IP address or hostname) to be resolved into a network
address.
* @param port The port number to be converted into network byte order.
*
* @return A sockaddr_in structure representing the network address derived from
the host and port.
*/
static struct sockaddr_in derive_sockaddr(const char* host, const char* port) {
    struct addrinfo hints = {
    .ai_family = AF_INET,
    };
    struct addrinfo *result_info;

    // Resolve the host (IP address or hostname) into a list of possible addresses.
    int returncode = getaddrinfo(host, port, &hints, &result_info);
    if (returncode) {
    fprintf(stderr, "Error␣parsing␣host/port");
    exit(EXIT_FAILURE);
    }

    // Copy the sockaddr_in structure from the first address in the list
    struct sockaddr_in result = *((struct sockaddr_in*) result_info->ai_addr);

    // Free the allocated memory for the result_info
    freeaddrinfo(result_info);
    return result;
}

int main(int argc, char *argv[]) {
    int s;
    int l_r_s;
    struct sockaddr_storage accept_adr;
    socklen_t len_accept_adr;
    char* reply;
    char* rec_buf[8];

    struct sockaddr_in result = derive_sockaddr(argv[1], argv[2]); // creates hints-struct and does getaddrinfo

    s = socket(PF_INET,SOCK_STREAM , 0);

    // setsockopt(s, SOL_SOCKET, SO_REUSEADDR, NULL, sizeof(int));

    bind(s, (struct sockaddr*) &result, sizeof(struct sockaddr));
    
    listen(s,5);
    
    len_accept_adr = sizeof(accept_adr);
    l_r_s = accept(s, (struct sockaddr *)&accept_adr, &len_accept_adr);

    reply = "Reply";
    recv(l_r_s, rec_buf, sizeof(*rec_buf), 0);
    send(l_r_s, reply, sizeof(*reply), 0);
}

