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


struct tuple resources[MAX_RESOURCES] = {
    {"/static/foo", "Foo", sizeof "Foo" - 1},
    {"/static/bar", "Bar", sizeof "Bar" - 1},
    {"/static/baz", "Baz", sizeof "Baz" - 1}};


//LULA BEGINNT
typedef struct {
    uint16_t node_id;
    uint16_t predecessor_id;
    uint16_t successor_id;
    const char *predecessor_ip;
    const char *successor_ip;
    int predecessor_port;
    int successor_port;
    const char *self_ip;
    int self_port;
} NodeConfig;

NodeConfig config;
int udp_socket;

void send_lookup_request(uint16_t hash, int udp_socket) {
    // Nachrichtenformat: '!BHH4sH'
    uint8_t message_type = 0; // Message Type: Lookup
    uint16_t network_hash = htons(hash);
    uint16_t network_node_id = htons(config.node_id);
    uint32_t network_self_ip; // Binäre IP-Adresse (4 Bytes)
    uint16_t network_self_port = htons(config.self_port);

    // Konvertiere eigene IP-Adresse zu binärem Format
    if (inet_pton(AF_INET, config.self_ip, &network_self_ip) != 1) {
        perror("Fehler bei der Umwandlung der eigenen IP-Adresse");
        return;
    }

    // Nachricht erstellen
    uint8_t buffer[11];
    memset(buffer, 0, sizeof(buffer));
    buffer[0] = message_type;
    memcpy(buffer + 1, &network_hash, sizeof(network_hash));
    memcpy(buffer + 3, &network_node_id, sizeof(network_node_id));
    memcpy(buffer + 5, &network_self_ip, sizeof(network_self_ip));
    memcpy(buffer + 9, &network_self_port, sizeof(network_self_port));

    // Zieladresse für den Nachfolger vorbereiten
    struct sockaddr_in successor_addr = {0};
    successor_addr.sin_family = AF_INET;
    successor_addr.sin_port = htons(config.successor_port);
    if (inet_pton(AF_INET, config.successor_ip, &successor_addr.sin_addr) != 1) {
        perror("Fehler bei der Umwandlung der Nachfolger-IP-Adresse");
        return;
    }

    // Nachricht senden
    ssize_t sent_bytes = sendto(udp_socket, buffer, sizeof(buffer), 0, 
                                (struct sockaddr *)&successor_addr, sizeof(successor_addr));
    if (sent_bytes == -1) {
        perror("Fehler beim Senden der Lookup-Nachricht");
    } else if (sent_bytes != sizeof(buffer)) {
        fprintf(stderr, "Warnung: Nur %zd von %zu Bytes gesendet\n", sent_bytes, sizeof(buffer));
    }
}


int correct_node(uint16_t hash, uint16_t node_id, uint16_t predecessor_id) {
    fprintf(stderr, "DEBUG: Entering correct_node\n");
    fprintf(stderr, "DEBUG: Hash = %u, Node ID = %u, Predecessor ID = %u\n",
            hash, node_id, predecessor_id);

    int responsible = 0;
    if (predecessor_id < node_id && hash > predecessor_id && hash <= node_id) {
        responsible = 1;
        fprintf(stderr, "DEBUG: Normal case, responsible = 1\n");
    } else if (predecessor_id >= node_id && (hash > predecessor_id || hash <= node_id)) {
        responsible = 1;
        fprintf(stderr, "DEBUG: Wrap-around case, responsible = 1\n");
    } else {
        fprintf(stderr, "DEBUG: Node is NOT responsible\n");
    }

    fprintf(stderr, "DEBUG: Responsible = %d\n", responsible);
    return responsible;
}

void send_lookup(char *buffer, size_t length, struct sockaddr_in *client_addr, int udp_socket) {
    uint16_t hash;
    memcpy(&hash, buffer + 1, sizeof(hash));  // Extrair hash da mensagem
    hash = ntohs(hash);  // Converter para host byte order

    fprintf(stderr, "DEBUG: Empfangene Nachricht von %s:%d für Hash %u\n", 
            inet_ntoa(client_addr->sin_addr), ntohs(client_addr->sin_port), hash);

    // Verificar se a Node atual é responsável
    if (correct_node(hash, config.node_id, config.predecessor_id)) {
        // A Node atual é responsável: envie uma resposta Reply
        uint8_t reply[11];
        reply[0] = 1;  // Message Type: Reply
        uint16_t pred_id = htons(config.predecessor_id);
        memcpy(reply + 1, &pred_id, sizeof(pred_id));
        uint16_t node_id = htons(config.node_id);
        memcpy(reply + 3, &node_id, sizeof(node_id));
        uint32_t self_ip;
        inet_pton(AF_INET, config.self_ip, &self_ip);
        memcpy(reply + 5, &self_ip, sizeof(self_ip));
        uint16_t self_port = htons(config.self_port);
        memcpy(reply + 9, &self_port, sizeof(self_port));

        sendto(udp_socket, reply, sizeof(reply), 0, (struct sockaddr *)client_addr, sizeof(*client_addr));
        fprintf(stderr, "DEBUG: Reply gesendet für Hash %u\n", hash);
    } else if (correct_node(hash, config.successor_id, config.node_id)) {
        // Sucessora é responsável: envie uma resposta Reply com dados da sucessora
        uint8_t reply[11];
        reply[0] = 1;  // Message Type: Reply
        uint16_t node_id = htons(config.node_id);
        memcpy(reply + 1, &node_id, sizeof(node_id));
        uint16_t succ_id = htons(config.successor_id);
        memcpy(reply + 3, &succ_id, sizeof(succ_id));
        uint32_t succ_ip;
        inet_pton(AF_INET, config.successor_ip, &succ_ip);
        memcpy(reply + 5, &succ_ip, sizeof(succ_ip));
        uint16_t succ_port = htons(config.successor_port);
        memcpy(reply + 9, &succ_port, sizeof(succ_port));

        sendto(udp_socket, reply, sizeof(reply), 0, (struct sockaddr *)client_addr, sizeof(*client_addr));
        fprintf(stderr, "DEBUG: Reply gesendet für Hash %u (verantwortlich: Sucessor)\n", hash);
    } else {
        // Encaminhar Lookup para o sucessor
        fprintf(stderr, "DEBUG: Weiterleitung der Lookup-Anfrage an Nachfolger\n");

        struct sockaddr_in successor_addr;
        successor_addr.sin_family = AF_INET;
        successor_addr.sin_port = htons(config.successor_port);
        inet_pton(AF_INET, config.successor_ip, &successor_addr.sin_addr);

        sendto(udp_socket, buffer, length, 0, (struct sockaddr *)&successor_addr, sizeof(successor_addr));
        fprintf(stderr, "DEBUG: Lookup weitergeleitet an %s:%d\n", config.successor_ip, config.successor_port);
    }
}

//LULA ENDET


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

    fprintf(stderr, "Handling %s request for %s (%lu byte payload)\n", request->method, request->uri, request->payload_length);
    
    //LULA BEGINNT
    uint16_t hash = pseudo_hash((const unsigned char *)request->uri, strlen(request->uri));

    size_t resource_length = 0;
    const char *resource = NULL;

    fprintf(stderr, "DEBUG: Hash for resource '%s' = %u\n", request->uri, hash);
    fprintf(stderr, "DEBUG: Node ID = %u\n", config.node_id);
    fprintf(stderr, "DEBUG: Predecessor ID = %u\n", config.predecessor_id);

    if (correct_node(hash, config.node_id, config.predecessor_id) == 0) {
        // Node ist nicht verantwortlich
        fprintf(stderr, "Node %u is NOT responsible for resource '%s'.\n", config.node_id, request->uri);

        send_lookup_request(hash, udp_socket);

        // 503-Antwort an den Client senden
        offset = sprintf(reply, "HTTP/1.1 503 Service Unavailable\r\nRetry-After: 1\r\nContent-Length: 0\r\n\r\n");
        send(conn, reply, offset, 0);
        return;
    }

    if (strcmp(request->method, "GET") == 0) {
        resource = get(request->uri, resources, MAX_RESOURCES, &resource_length);
        if (resource) {
            size_t payload_offset =
                sprintf(reply, "HTTP/1.1 200 OK\r\nContent-Length: %lu\r\n\r\n", resource_length);
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

//LULA BEGINNT
static int setup_udp_socket(struct sockaddr_in addr) {
    udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    const int disable = 0;

    if (udp_socket == -1) {
        perror("UDP-Socket");
        exit(EXIT_FAILURE);
    }

    if (setsockopt(udp_socket, SOL_SOCKET, SO_REUSEADDR, &disable, sizeof(disable)) == -1) {
        perror("setsockopt disable SO_REUSEADDR UDP-Socket");
        close(udp_socket);
        exit(EXIT_FAILURE);
    }

    fprintf(stderr, "DEBUG: UDP-Socket erstellt, FD=%d\n", udp_socket);

    if (bind(udp_socket, (struct sockaddr *)&addr, sizeof(addr)) == -1) {
        perror("Bind UDP-Socket");
        close(udp_socket);
        fprintf(stderr, "DEBUG: Bind UDP-Socket: Address already in use\r\nBind failed with errno=%d\n", errno);
        exit(EXIT_FAILURE);
    } else {
        fprintf(stderr, "DEBUG: UDP-Socket erfolgreich gebunden an %s:%d\n", inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
    }

    return udp_socket;
}
//LULA ENDET

/**
 * Sets up a TCP server socket and binds it to the provided sockaddr_in address.
 *
 * @param addr The sockaddr_in structure representing the IP address and port of
 * the server.
 *
 * @return The file descriptor of the created TCP server socket.
 */
static int setup_server_socket(struct sockaddr_in addr) {
    const int enable = 1;
    const int backlog = 1;

    // Create a socket
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == -1) {
        perror("TCP-Socket");
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
        perror("Bind TCP-Socket");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Start listening on the socket with maximum backlog of 1 pending
    // connection
    if (listen(sock, backlog)) {
        perror("Listen TCP-Socket");
        exit(EXIT_FAILURE);
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
    //LULA BEGINNT
    if (argc != 3 && argc != 4) {
        fprintf(stderr, "Akzeptierte Eingabe: %s <IP> <Port> [Node ID]\n", argv[0]);
        return EXIT_FAILURE;
    }

    memset(&config, 0, sizeof(config));

    config.self_ip = argv[1];
    config.self_port = atoi(argv[2]);

    struct sockaddr_in addr = derive_sockaddr(config.self_ip, argv[2]);

    udp_socket = setup_udp_socket(addr);
    printf("UDP-Socket läuft auf %s:%s\n", config.self_ip, argv[2]);

    int server_socket = setup_server_socket(addr);

    if (argc >= 4 && argv[3] != NULL) {
        config.node_id = (uint16_t)atoi(argv[3]);
        printf("DEBUG: Node-ID = %d\n", config.node_id);
    } else {
        config.node_id = 0;
        printf("DEBUG: Standard-Node-ID = 0\n");
    }

    const char *pred_id_env = getenv("PRED_ID");
    const char *succ_id_env = getenv("SUCC_ID");
    const char *pred_ip_env = getenv("PRED_IP");
    const char *succ_ip_env = getenv("SUCC_IP");
    const char *pred_port_env = getenv("PRED_PORT");
    const char *succ_port_env = getenv("SUCC_PORT");

    if (!pred_id_env || !succ_id_env || !pred_ip_env || !succ_ip_env || !pred_port_env || !succ_port_env) {
        fprintf(stderr, "Fehler: Eine oder mehrere erforderliche Umgebungsvariablen fehlen.\n");
        exit(EXIT_FAILURE);
    }

    config.predecessor_id = (uint16_t)atoi(pred_id_env);
    config.successor_id = (uint16_t)atoi(succ_id_env);
    config.predecessor_ip = pred_ip_env;
    config.successor_ip = succ_ip_env;
    config.predecessor_port = atoi(pred_port_env);
    config.successor_port = atoi(succ_port_env);

    if (config.predecessor_ip == NULL || config.successor_ip == NULL) {
    printf("DEBUG: Vorgänger- oder Nachfolger-IP nicht gesetzt\n");
    exit(EXIT_FAILURE);
    }   

    printf("Node ID: %u\n", config.node_id);
    printf("Predecessor: ID=%u, IP=%s, Port=%d\n", config.predecessor_id, config.predecessor_ip, config.predecessor_port);
    printf("Successor: ID=%u, IP=%s, Port=%d\n", config.successor_id, config.successor_ip, config.successor_port);
    //LULA ENDET

    // Create an array of pollfd structures to monitor sockets.
    struct pollfd sockets[2] = {
        {.fd = server_socket, .events = POLLIN}, // TCP
        {.fd = udp_socket, .events = POLLIN},   // (LULA) UDP
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
            //LULA BEGGINNT
            if (s == udp_socket) {
                printf("DEBUG: UDP-Socket aktiviert.\n");
                char buffer[1024];
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);

                ssize_t received = recvfrom(udp_socket, buffer, sizeof(buffer) - 1, 0,
                                            (struct sockaddr *)&client_addr, &addr_len);
                if (received < 0) {
                    perror("Fehler beim Empfangen über UDP");
                    continue;
                }

                buffer[received] = '\0';
                send_lookup(buffer, received, &client_addr, udp_socket);
                printf("UDP-Nachricht von %s:%d: %s\n",
                       inet_ntoa(client_addr.sin_addr),
                       ntohs(client_addr.sin_port),
                       buffer);
            //LULA ENDET
            } else if (s == server_socket) {

                // If the event is on the server_socket, accept a new connection
                // from a client.
                int connection = accept(server_socket, NULL, NULL);
                if (connection == -1 && errno != EAGAIN &&
                    errno != EWOULDBLOCK) {
                    close(server_socket);
                    perror("accept");
                    exit(EXIT_FAILURE);
                } else {
                    connection_setup(&state, connection);

                    // limit to one connection at a time
                    sockets[0].events = 0;
                    sockets[1].fd = connection;
                    sockets[1].events = POLLIN;
                }
            } else {
                assert(s == state.sock);

                // Call the 'handle_connection' function to process the incoming
                // data on the socket.
                bool cont = handle_connection(&state);
                if (!cont) { // get ready for a new connection
                    sockets[0].events = POLLIN;
                    sockets[1].fd = -1;
                    sockets[1].events = 0;
                }
            }
        }
    }

    close(udp_socket); //(LULA)
    return EXIT_SUCCESS;
}

//12 passed