#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "data.h"
#include "http.h"
#include "util.h"

#define MAX_RESOURCES 100
#define LOOKUP 0
#define REPLY 1

int udp_server_socket; // global variable which is filled in main() and used in send_reply()

struct node_info this_node; // global variable that has it's values assigned by calling fill_out_node_info in main()

typedef struct node_info {  // ID, IP and PORT are saved as char* because conversion to different data types 
    char* PRED_ID;          // in fill_out_node_info let the tests fail
    char* PRED_IP; 
    char* PRED_PORT; 
    char* SUCC_ID;
    char* SUCC_IP;
    char* SUCC_PORT; 
    char* MY_IP; 
    char* MY_PORT; 
    char* MY_ID;
} node_info;

/**
 * Fills out a node_info struct which contains ID, IP and PORT of this node 
 * and it's successor and predecessor in the dht.
 *
 * @param __MY_ID ID of this node in the dht
 * @param __MY_IP IP of this node
 * @param __MY_PORT assigned port of this node
 *
 * @return struct node_info filled out with data on this, the previous and the successor node
 */
node_info fill_out_node_info(char* __MY_ID, char* __MY_IP , char* __MY_PORT) {
    node_info my_struct;
    my_struct.PRED_ID = getenv("PRED_ID");
    my_struct.PRED_IP = getenv("PRED_IP");
    my_struct.PRED_PORT = getenv("PRED_PORT");

    my_struct.SUCC_ID = getenv("SUCC_ID");
    my_struct.SUCC_IP = getenv("SUCC_IP");
    my_struct.SUCC_PORT = getenv("SUCC_PORT");

    if (__MY_ID == NULL) {my_struct.MY_ID = "0";}
    else {my_struct.MY_ID = __MY_ID;}
    my_struct.MY_IP = __MY_IP;
    my_struct.MY_PORT = __MY_PORT;

    return my_struct;
}

/**
 * Formulates 11 bytes into a message using info from the global variable this_node.
 * Meant to be sent inside the dht via udp.
 *
 * @param __message_type The type of the message, either LOOKUP(=0) or REPLY(=1)
 * @param __uri_hash hash value of the resource this message is about
 *
 * @return pointer (char*) to 11 byte long message
 */
char* dht_udp_message(u_int8_t __message_type, u_int16_t __uri_hash, char* __id, char* __ip, char* __port){
    char* message = calloc(11, sizeof(char));

    // at pos. 0: type of the message, either LOOKUP(=0) or REPLY(=1)
    uint8_t _nbo_type = htons(__message_type); // hton here fails test_lookup_reply
    memcpy(message, &__message_type, sizeof(__message_type));

    // at pos. 1-2: hash value of the resource this message is about
    uint16_t _nbo_hash = htons(__uri_hash);
    memcpy(message+1, &_nbo_hash, sizeof(_nbo_hash));

    // at pos. 3-4: id
    uint16_t _nbo_id = htons(atoi(__id));
    memcpy(message+3, &_nbo_id, sizeof(_nbo_id));

    // at pos. 5-8: ip
    uint32_t _ip_binary;
    inet_pton(AF_INET, __ip, &_ip_binary);
    memcpy(message+5, &_ip_binary, sizeof(_ip_binary));

    // at pos. 9-10: port
    uint16_t _nbo_port = htons(atoi(__port));
    memcpy(message+9, &_nbo_port, sizeof(_nbo_port));

    return message;
}

struct tuple resources[MAX_RESOURCES] = {
    {"/static/foo", "Foo", sizeof "Foo" - 1},
    {"/static/bar", "Bar", sizeof "Bar" - 1},
    {"/static/baz", "Baz", sizeof "Baz" - 1}};

/**
 * Derives a sockaddr_in structure from the provided host and port information.
 *
 * @param host The host (IP address or hostname) to be resolved into a network
 * address.
 * @param port The port number to be converted into network byte order.
 *
 * @return A sockaddr_in structure representing the network address derived from
 * the host and port.
 */
static struct sockaddr_in derive_sockaddr(const char *host, const char *port) {
    struct addrinfo hints = {
        .ai_family = AF_INET,
    };
    struct addrinfo *result_info;

    // Resolve the host (IP address or hostname) into a list of possible
    // addresses.
    int returncode = getaddrinfo(host, port, &hints, &result_info);
    if (returncode) {
        fprintf(stderr, "Error parsing host/port");
        exit(EXIT_FAILURE);
    }

    // Copy the sockaddr_in structure from the first address in the list
    struct sockaddr_in result = *((struct sockaddr_in *)result_info->ai_addr);

    // Free the allocated memory for the result_info
    freeaddrinfo(result_info);
    return result;
}

/**
 * Sends an HTTP reply to the client based on the received request.
 *
 * @param conn      The file descriptor of the client connection socket.
 * @param request   A pointer to the struct containing the parsed request
 * information.
 */
void send_reply(int conn, struct request *request) {

    // Create a buffer to hold the HTTP reply
    char buffer[HTTP_MAX_SIZE];
    char *reply = buffer;
    size_t offset = 0;

    fprintf(stderr, "Handling %s request for %s (%lu byte payload)\n",
            request->method, request->uri, request->payload_length);

    // calculate hash of resource path
    uint16_t uri_hash = pseudo_hash(request->uri, strlen(request->uri));
    if (uri_hash>atoi(this_node.MY_ID) && uri_hash<=atoi(this_node.PRED_ID)) { // check if other node is responsible and send simple lookup if so 
        sprintf(reply, "HTTP/1.1 503 Service Unavailable\r\nRetry-After: 1\r\nContent-Length: 0\r\n\r\n");
        //HTTP/1.1 503 Service Unavailable
        //Retry-After: 1
        //Content-Length: 0
        offset = strlen(reply);
        char* message_succ = dht_udp_message(LOOKUP, uri_hash, this_node.MY_ID, this_node.MY_IP, this_node.MY_PORT);
        const struct sockaddr_in succ_addr = derive_sockaddr(this_node.SUCC_IP, this_node.SUCC_PORT);
        sendto(udp_server_socket, message_succ, 11, 0, &succ_addr, sizeof(succ_addr));
        free(message_succ);

    } else if (strcmp(request->method, "GET") == 0) { // if we reach this point this node is responsible for the request
        // Find the resource with the given URI in the 'resources' array.
        size_t resource_length;
        const char *resource =
            get(request->uri, resources, MAX_RESOURCES, &resource_length);

        if (resource) {
            size_t payload_offset =
                sprintf(reply, "HTTP/1.1 200 OK\r\nContent-Length: %lu\r\n\r\n",
                        resource_length);
            memcpy(reply + payload_offset, resource, resource_length);
            offset = payload_offset + resource_length;
        } else {
            reply = "HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\n\r\n";
            offset = strlen(reply);
        }
    } else if (strcmp(request->method, "PUT") == 0) {
        // Try to set the requested resource with the given payload in the
        // 'resources' array.
        if (set(request->uri, request->payload, request->payload_length,
                resources, MAX_RESOURCES)) {
            reply = "HTTP/1.1 204 No Content\r\n\r\n";
        } else {
            reply = "HTTP/1.1 201 Created\r\nContent-Length: 0\r\n\r\n";
        }
        offset = strlen(reply);
    } else if (strcmp(request->method, "DELETE") == 0) {
        // Try to delete the requested resource from the 'resources' array
        if (delete (request->uri, resources, MAX_RESOURCES)) {
            reply = "HTTP/1.1 204 No Content\r\n\r\n";
        } else {
            reply = "HTTP/1.1 404 Not Found\r\n\r\n";
        }
        offset = strlen(reply);
    } else {
        reply = "HTTP/1.1 501 Method Not Supported\r\n\r\n";
        offset = strlen(reply);
    }

    // Send the reply back to the client
    if (send(conn, reply, offset, 0) == -1) {
        perror("send");
        close(conn);
    }
}

/**
 * Processes an incoming packet from the client.
 *
 * @param conn The socket descriptor representing the connection to the client.
 * @param buffer A pointer to the incoming packet's buffer.
 * @param n The size of the incoming packet.
 *
 * @return Returns the number of bytes processed from the packet.
 *         If the packet is successfully processed and a reply is sent, the
 * return value indicates the number of bytes processed. If the packet is
 * malformed or an error occurs during processing, the return value is -1.
 *
 */
size_t process_packet(int conn, char *buffer, size_t n) {
    struct request request = {
        .method = NULL, .uri = NULL, .payload = NULL, .payload_length = -1};
    ssize_t bytes_processed = parse_request(buffer, n, &request);

    if (bytes_processed > 0) {
        send_reply(conn, &request);

        // Check the "Connection" header in the request to determine if the
        // connection should be kept alive or closed.
        const string connection_header = get_header(&request, "Connection");
        if (connection_header && strcmp(connection_header, "close")) {
            return -1;
        }
    } else if (bytes_processed == -1) {
        // If the request is malformed or an error occurs during processing,
        // send a 400 Bad Request response to the client.
        const string bad_request = "HTTP/1.1 400 Bad Request\r\n\r\n";
        send(conn, bad_request, strlen(bad_request), 0);
        printf("Received malformed request, terminating connection.\n");
        close(conn);
        return -1;
    }

    return bytes_processed;
}

/**
 * Sets up the connection state for a new socket connection.
 *
 * @param state A pointer to the connection_state structure to be initialized.
 * @param sock The socket descriptor representing the new connection.
 *
 */
static void connection_setup(struct connection_state *state, int sock) {
    // Set the socket descriptor for the new connection in the connection_state
    // structure.
    state->sock = sock;

    // Set the 'end' pointer of the state to the beginning of the buffer.
    state->end = state->buffer;

    // Clear the buffer by filling it with zeros to avoid any stale data.
    memset(state->buffer, 0, HTTP_MAX_SIZE);
}

/**
 * Discards the front of a buffer
 *
 * @param buffer A pointer to the buffer to be modified.
 * @param discard The number of bytes to drop from the front of the buffer.
 * @param keep The number of bytes that should be kept after the discarded
 * bytes.
 *
 * @return Returns a pointer to the first unused byte in the buffer after the
 * discard.
 * @example buffer_discard(ABCDEF0000, 4, 2):
 *          ABCDEF0000 ->  EFCDEF0000 -> EF00000000, returns pointer to first 0.
 */
char *buffer_discard(char *buffer, size_t discard, size_t keep) {
    memmove(buffer, buffer + discard, keep);
    memset(buffer + keep, 0, discard); // invalidate buffer
    return buffer + keep;
}

/**
 * Handles incoming connections and processes data received over the socket.
 *
 * @param state A pointer to the connection_state structure containing the
 * connection state.
 * @return Returns true if the connection and data processing were successful,
 * false otherwise. If an error occurs while receiving data from the socket, the
 * function exits the program.
 */
bool handle_connection(struct connection_state *state) {
    // Calculate the pointer to the end of the buffer to avoid buffer overflow
    const char *buffer_end = state->buffer + HTTP_MAX_SIZE;

    // Check if an error occurred while receiving data from the socket
    ssize_t bytes_read =
        recv(state->sock, state->end, buffer_end - state->end, 0);
    if (bytes_read == -1) {
        perror("recv");
        close(state->sock);
        exit(EXIT_FAILURE);
    } else if (bytes_read == 0) {
        return false;
    }

    char *window_start = state->buffer;
    char *window_end = state->end + bytes_read;

    ssize_t bytes_processed = 0;
    while ((bytes_processed = process_packet(state->sock, window_start,
                                             window_end - window_start)) > 0) {
        window_start += bytes_processed;
    }
    if (bytes_processed == -1) {
        return false;
    }

    state->end = buffer_discard(state->buffer, window_start - state->buffer,
                                window_end - window_start);
    return true;
}

/**
 * Sets up a TCP server socket and binds it to the provided sockaddr_in address.
 *
 * @param addr The sockaddr_in structure representing the IP address and port of
 * the server.
 *
 * @return The file descriptor of the created TCP server socket.
 */
static int setup_server_socket(struct sockaddr_in addr, int __type) {
    const int enable = 1;
    const int backlog = 1;

    // Create a socket
    int sock = socket(AF_INET, __type, 0);
    if (sock == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // Avoid dead lock on connections that are dropped after poll returns but
    // before accept is called
    if (fcntl(sock, F_SETFL, O_NONBLOCK) == -1) {
        perror("fcntl");
        exit(EXIT_FAILURE);
    }

    // Set the SO_REUSEADDR socket option to allow reuse of local addresses
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable)) ==
        -1) {
        perror("setsockopt");
        exit(EXIT_FAILURE);
    }

    // Bind socket to the provided address
    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("bind");
        close(sock);
        exit(EXIT_FAILURE);
    }

    if (__type == SOCK_STREAM) {
        // Start listening on the socket with maximum backlog of 1 pending
        // connection
        if (listen(sock, backlog)) {
            perror("listen");
            exit(EXIT_FAILURE);
        }
    }

    return sock;
}

/**
 *  The program expects 3; otherwise, it returns EXIT_FAILURE.
 *
 *  Call as:
 *
 *  ./build/webserver self.ip self.port
 */
int main(int argc, char **argv) {
    if (argc < 2) { // it was argc != 3 before which is bad because the code should assume MY_ID is 0 when it isn't given instead of EXIT_FAILURE
        return EXIT_FAILURE;
    }

    struct sockaddr_in addr = derive_sockaddr(argv[1], argv[2]);

    // Set up a UDP and TCP server socket.
    udp_server_socket = setup_server_socket(addr, SOCK_DGRAM); //gloabal variable which is defined up top and later used in send_reply()
    int tcp_server_socket = setup_server_socket(addr, SOCK_STREAM);

    // gathers info from call looking like this: 
    // PRED_ID=16384 PRED_IP=127.0.0.1 PRED_PORT=2001 SUCC_ID=16384 SUCC_IP=127.0.0.1 SUCC_PORT=2001 ./build/webserver 127.0.0.1 2002 49152
    this_node = fill_out_node_info(argv[3], argv[1], argv[2]); //I am not converting the numbers from char* to int since this breaks the first test?

    // Create an array of pollfd structures to monitor sockets.
    struct pollfd sockets[3] = {
        {.fd = tcp_server_socket, .events = POLLIN},
        {.fd = udp_server_socket, .events = POLLIN},
    };

    struct connection_state state = {0};
    while (true) {

        // Use poll() to wait for events on the monitored sockets.
        int ready = poll(sockets, sizeof(sockets) / sizeof(sockets[0]), -1);
        if (ready == -1) {
            perror("poll");
            exit(EXIT_FAILURE);
        }

        // Process events on the monitored sockets.
        for (size_t i = 0; i < sizeof(sockets) / sizeof(sockets[0]); i += 1) {
            if (sockets[i].revents != POLLIN) {
                // If there are no POLLIN events on the socket, continue to the
                // next iteration.
                continue;
            }
            int s = sockets[i].fd;

            if (s == tcp_server_socket) {

                // If the event is on the tcp_server_socket, accept a new connection
                // from a client.
                int connection = accept(tcp_server_socket, NULL, NULL);
                if (connection == -1 && errno != EAGAIN &&
                    errno != EWOULDBLOCK) {
                    close(tcp_server_socket);
                    perror("accept");
                    exit(EXIT_FAILURE);
                } else {
                    connection_setup(&state, connection);

                    // limit to one connection at a time
                    sockets[0].events = 0;
                    sockets[2].fd = connection;
                    sockets[2].events = POLLIN;
                }
            } 
            
            else if (s == udp_server_socket) {
                // If the event is on the udp_server_socket
                char* _buff = calloc(11, sizeof(char));
                struct sockaddr *restrict inquirer_adr = calloc(1, sizeof(struct sockaddr));
                socklen_t *restrict inquirer_adr_len = calloc(1, sizeof(socklen_t));
                recvfrom(s, _buff, 11, 0, inquirer_adr, inquirer_adr_len);
                uint16_t _hash;
                memcpy(&_hash, _buff+1, sizeof(_hash)); 
                
                if (_hash<=atoi(this_node.SUCC_ID)) { // check whether successor node is responsible
                    char* message_succ = dht_udp_message(REPLY, 
                    atoi(this_node.MY_ID), this_node.SUCC_ID, this_node.SUCC_IP, this_node.SUCC_PORT);
                    const struct sockaddr_in pred_addr = derive_sockaddr(this_node.PRED_IP, this_node.PRED_PORT);
                    sendto(udp_server_socket, message_succ, 11, 0, &pred_addr, sizeof(pred_addr));

                } else if (_hash<=atoi(this_node.MY_ID)) { // check if this node is responsible
                    char* message_succ = dht_udp_message(REPLY, 
                    atoi(this_node.PRED_ID), this_node.MY_ID, this_node.MY_IP, this_node.MY_PORT);
                    const struct sockaddr_in pred_addr = derive_sockaddr(this_node.PRED_IP, this_node.PRED_PORT);
                    sendto(udp_server_socket, message_succ, 11, 0, &pred_addr, sizeof(pred_addr));

                } else { // forwards the message
                    const struct sockaddr_in succ_addr = derive_sockaddr(this_node.SUCC_IP, this_node.SUCC_PORT);
                    sendto(udp_server_socket, _buff, 11, 0, &succ_addr, sizeof(succ_addr));
                }
                
                free(_buff);
            }
            
            else {
                assert(s == state.sock);

                // Call the 'handle_connection' function to process the incoming
                // data on the socket.
                bool cont = handle_connection(&state);
                if (!cont) { // get ready for a new connection
                    sockets[0].events = POLLIN;
                    sockets[2].fd = -1;
                    sockets[2].events = 0;
                }
            }
        }
    }

    return EXIT_SUCCESS;
}
