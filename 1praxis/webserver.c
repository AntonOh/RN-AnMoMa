#include <cstddef>
#include <stdio.h>
#include <stdlib.h>
#include <regex.h>

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

int is_http_request_format(char input[20]) {
    regex_t regex;
    int ret;

    //set and compile the regex pattern
    ret = regcomp(&regex,"&regex, /^(GET|HEAD|POST|PUT|DELETE|CONNECT|OPTIONS|TRACE|PATCH) \/ HTTP\/[.0-9]+ \r\n Host: [a-zA-Z0-9.-]+\r\n[a-zA-Z0-9.-]*\r\n$", REG_EXTENDED);
    //execute the match
    ret = regexec(&regex, input, 1, NULL, NULL);
    //free the pattern
    regfree(&regex);
    if (!ret) {
        return 1; // Match found
    } else if (ret == REG_NOMATCH) {
        return 0; // No match
    } else {
        char errbuf[100];
        regerror(ret, &regex, errbuf, sizeof(errbuf));
        printf("Regex match failed: %s\n", errbuf);
        return 0;
    }
}

int main(int argc, char *argv[]) {
    int s;
    int io_s;
    struct sockaddr_storage accept_adr;
    socklen_t len_accept_adr;
    char* reply;
    char rec_buf[20];

    struct sockaddr_in result = derive_sockaddr(argv[1], argv[2]); // creates hints-struct and does getaddrinfo

    s = socket(PF_INET,SOCK_STREAM , 0);

    // setsockopt(s, SOL_SOCKET, SO_REUSEADDR, NULL, sizeof(int));

    bind(s, (struct sockaddr*) &result, sizeof(struct sockaddr));
    
    listen(s,5);
    
    len_accept_adr = sizeof(accept_adr);
    io_s = accept(s, (struct sockaddr *)&accept_adr, &len_accept_adr);

    recv(io_s, rec_buf, sizeof(*rec_buf), 0);

    if (is_http_request_format(rec_buf)) {
            reply= "Reply\r\n\r\n";
    }
    else {
        reply = "Reply";
    }
    send(io_s, reply, strlen(reply), 0);
    
}

