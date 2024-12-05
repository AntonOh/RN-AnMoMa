#include <stdio.h>
#include <stdlib.h>
#include <regex.h>
#include <string.h>

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

int is_http_request_format(char input[8192]) {
    regex_t regex;
    int ret;

    //set and compile the regex pattern
    ret = regcomp(&regex,"^(GET|HEAD|POST|PUT|DELETE|CONNECT|OPTIONS|TRACE|PATCH) / HTTP/[0-9.]+\r\n(.*\r\n)*\r\n$", REG_EXTENDED | REG_NEWLINE);
    //execute the match
    ret = regexec(&regex, input, 0, NULL, 0);
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
    char rec_buf[8192];

    struct sockaddr_in result = derive_sockaddr(argv[1], argv[2]); // creates hints-struct and does getaddrinfo

    s = socket(PF_INET,SOCK_STREAM , 0);

    // setsockopt(s, SOL_SOCKET, SO_REUSEADDR, NULL, sizeof(int));

    if (bind(s, (struct sockaddr*) &result, sizeof(struct sockaddr))==-1){
        printf("failed bind\n");
        return -1;
  }
    
    listen(s,5);
    
    len_accept_adr = sizeof(accept_adr);
    io_s = accept(s, (struct sockaddr *)&accept_adr, &len_accept_adr);

    recv(io_s, rec_buf, sizeof(rec_buf) - 1, 0);
    rec_buf[sizeof(rec_buf) - 1] = '\0'; // Null-terminate the buffer
    if (is_http_request_format(rec_buf)) {
        reply= "Reply\r\n\r\n";
        printf("http recognized");
    }
    else {
        reply = "Reply\n";
        printf("%s\n",rec_buf);
    }
    send(io_s, reply, strlen(reply), 0);
}

