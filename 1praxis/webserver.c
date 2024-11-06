#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>
#include <regex.h>

#define MAX_MEM_SIZE 8192
#define CON_LIMIT 20

enum REQ_TYPES {
  GET,
  POST,
  UNK
} typedef reqtype_t;

struct http_req {
  reqtype_t type;

  size_t payload_size;
  size_t req_line_size;
  size_t headers_size; 

  char *req_line;
  char *headers; 
  char *payload;
} typedef req_t;

struct http_response {
  int value;
 
  size_t payload_size;
  size_t pack_size;

  char *payload;
  char *pack;
} typedef resp_t;

struct stream_buffer {
  char *buffer;
  size_t size;
  int tail;
} typedef stream_buffer_t;


void zombie_handler() {
  // We could save errno but i dont care
  while (waitpid(-1, NULL, WNOHANG) > 0);
}

size_t get_line_idx(char mem[], size_t mem_size) {
  for (size_t i = 0; i < mem_size-1; i++) {
    if (mem[i] == '\r' && mem[i+1] == '\n') {
      return i;
    }
  }
}

void  flush_buffer(stream_buffer_t* buffer) {
  memset(buffer->buffer, 0x0, buffer->size);
}

void flush_request(req_t* request) {
  if (request->req_line != NULL) {
    free(request->req_line);
    request->req_line = NULL;
  } 

  if (request->headers != NULL) {
    free(request->headers);
    request->headers = NULL; 
  }

  if (request->payload != NULL) {
    free(request->payload);
    request->payload = NULL; 
  }

  request->type = UNK;
  request->req_line_size = 0; 
  request->headers_size = 0; 
  request->headers_size = 0; 
}

void push_to_buffer(stream_buffer_t* buffer, char* message, size_t n) {
  if (buffer->tail + n > buffer->size) {
    printf("[Warning] Buffer full. Package loss expacted\n");
    flush_buffer(buffer);
    buffer->tail = 0;
  }

  
  memcpy((void *)&(buffer->buffer[buffer->tail]), (void *)message, n);

  for (size_t i = 0; i < buffer->size; i++) {
    if (buffer->buffer[i] == '\x00') {
      buffer->tail = i;
      return;
    }
  }
}

void set_type(req_t* request) {
  if (strncmp(request->req_line, "GET", 3) == 0) {
    request->type = GET;
  } 
}

 
bool check_valid(req_t* request) {
  regex_t regex;
  int reti;

  char *req_line_pattern = "^(GET|POST|PUT|DELETE|HEAD) ([^ \t\n\v\f\r]+) HTTP/[0-9].[0-9]\r\n$";
  char *headers_pattern = "^([^ \t\n\v\f\r]+: [^ \t\n\v\f\r]+\r\n)+\r\n$";

  // check regex
  int req_line_regex = regcomp(&regex, req_line_pattern, REG_EXTENDED);
  if (req_line_regex) {
    printf("could not compile regex\n");
    return false;
  }  

  if (regexec(&regex, request->req_line, 0, NULL, 0) != 0) {
    return false;
  }

  int headers_regex = regcomp(&regex, headers_pattern, REG_EXTENDED);
  if (headers_regex) {
    printf("could not compile regex\n");
    return false;
  }  


  if ((regexec(&regex, request->headers, 0, NULL, 0) != 0) && request->headers_size > 2) {
    printf("Here\n");
    return false;
  } 

  return true;
}


void print_request(req_t* request) {
  // print reqline
  int idx = 0;
  printf("==== REQUEST LINE ===\n");
  while (request->req_line[idx] != '\x00') {
    if (request->req_line[idx] == '\r') {
      printf("\\r");
    } else if (request->req_line[idx] == '\n') {
      printf("\\n");
    } else {
      printf("%c", request->req_line[idx]); 
    }
    idx++;
  }
  printf("\n");
  printf("==== HEADERS ===\n");
  idx = 0;
  while (request->headers[idx] != '\x00') {
    if (request->headers[idx] == '\r') {
      printf("\\r");
    } else if (request->headers[idx] == '\n') {
      printf("\\n");
      printf("\n");
    } else {
      printf("%c", request->headers[idx]); 
    }
    idx++;
  }
  printf("\n");
}

int pull_from_buffer(stream_buffer_t* buffer, req_t* http_request) {
  char *req_line_start = NULL;
  char *header_start = NULL;

  // heap array for package data
  char *req_line_content = NULL;
  char  *header_content = NULL;
  char *payload_content = NULL;
  
  // sizes
  size_t payload_size = 0;
  size_t req_line_size = 0;
  size_t header_size = 0;

  size_t bytes_read = 0;

  // Read request line and header
  char *req_line_end = strstr(buffer->buffer, "\r\n");
  if (req_line_end == NULL) {
    return 0;
  }

  char *header_end = strstr(req_line_end, "\r\n\r\n");
  if (header_end == NULL) {
    return 0;
  }

  // calcuate sizes and copy to memory;
  req_line_start = buffer->buffer;
  req_line_size = ((req_line_end + 2) - buffer->buffer);

  bytes_read += req_line_size;

  // load into memory
  req_line_content = (char *)malloc(req_line_size); 

  memcpy(req_line_content, req_line_start, req_line_size);


  // read header
  header_start = req_line_start + req_line_size;
  header_size = ((header_end + 4) - (req_line_end + 2));

  printf("header_size: %d\n", header_size);

  header_content = (char *)malloc(header_size);

  memcpy(header_content, header_start, header_size);

  bytes_read += header_size;

  if (header_size > 2) {
    // chech if payload present

    char* cl_idx = strstr(header_content, "Content-Length: ");

    if (cl_idx != NULL) {
      payload_size = (size_t)strtol(cl_idx + 16, NULL, 10); 

      // check if message package is complete
      if (bytes_read + payload_size > buffer->tail) {
        free(header_content);
        free(req_line_content);

        return 0;
      } else {
        payload_content = (char *)malloc(payload_size);
        memcpy(payload_content, header_start + header_size + 2, payload_size); 

        bytes_read += payload_size;
      }
    }
  }
  
  http_request->req_line = req_line_content;
  http_request->headers = header_content;
  http_request->payload = payload_content;
  
  http_request->req_line_size = req_line_size;
  http_request->headers_size = header_size;
  http_request->payload_size = payload_size;

  // TODO: check pointer arithmetic

  // remove from stream buffer
  memcpy(buffer->buffer, buffer->buffer + bytes_read, buffer->tail - bytes_read);
  buffer->tail -= bytes_read;
  memset(buffer->buffer + buffer->tail, 0x0, buffer->size - buffer->tail);
  return 1; 
}

size_t pack_response(resp_t* response) {
  response->pack_size = 0;

  if (response->value == 404) {
    response->pack_size += 24;
    response->pack = (char *)malloc(response->pack_size);
    memcpy(response->pack, "HTTP/1.1 404 Not Found\r\n", 24);
  } else if (response->value == 400) {
    response->pack_size += 26;
    response->pack = (char *)malloc(response->pack_size);
    memcpy(response->pack, "HTTP/1.1 400 Bad Request\r\n", 26);
  } else {
    response->pack_size += 30;
    response->pack = (char *)malloc(response->pack_size);
    memcpy(response->pack, "HTTP/1.1 501 Not Implemented\r\n", 30);
  }

  if (response->payload_size != 0) {
    // convert size to string
    char buffer[20];
    memset(buffer, 0x0, 20);
    sprintf(buffer, "%d", response->payload_size);

    size_t size_str_len = strlen(buffer);

    response->pack = (char *)realloc(response->pack, response->pack_size + 16 + size_str_len + 4 + response->payload_size);

    // construct package
    strncpy(response->pack + response->pack_size, "Content-Length: ", 16);
    response->pack_size += 16;

    strncpy(response->pack + response->pack_size, buffer, size_str_len);
    response->pack_size += size_str_len;

    strncpy(response->pack + response->pack_size, "\r\n\r\n", 4);
    response->pack_size += 4;

    strncpy(response->pack + response->pack_size, response->payload, response->payload_size);
    response->pack_size += response->payload_size;
  } else {
    response->pack = (char *)realloc(response->pack, response->pack_size + 2);
    strncpy(response->pack + response->pack_size, "\r\n", 2);

    response->pack_size += 2;
  }

  return response->pack_size;
}

void flush_response(resp_t* response) {
  response->value = 400;
  if (response->payload != NULL) {
    free(response->payload);
  }
  
  response->payload = NULL;
  response->payload_size = 0;

  if (response->pack != NULL) {
    free(response->pack);
  }

  response->pack = NULL;
  response->pack_size = 0;
}

void con_handler(int client_fd, void* accept_pack_mem, const size_t mem_size) {
 
 // to make sure we lose no date when payload is large
  stream_buffer_t buffer;
  req_t current_request;
  resp_t response;
  
  buffer.buffer = (char*)malloc(mem_size*2 + 80); // enough memory
  buffer.size = mem_size*2 + 80;
  buffer.tail = 0;

  current_request.req_line = NULL;
  current_request.headers = NULL;
  current_request.payload = NULL;

  flush_request(&current_request);
  flush_buffer(&buffer);

  for(;;) {
    memset(accept_pack_mem, 0x0, mem_size);
    if (recv(client_fd, accept_pack_mem, mem_size, 0) == -1 ) {
      printf("Error recv");
      exit(0);
    }

    push_to_buffer(&buffer, accept_pack_mem, mem_size); 
    bool pull_packages = true;
    while (pull_packages) {
      int n = pull_from_buffer(&buffer, &current_request);

      if (n > 0) {
        print_request(&current_request);
        flush_response(&response);

        bool is_valid = check_valid(&current_request);
        if (is_valid) {
          printf("The Package is valid\n"); 

          set_type(&current_request);

          if (current_request.type == GET) {
            response.value = 404;
          } else {
            response.value = 501;
          }
        } 

        size_t n = pack_response(&response);
        send(client_fd, response.pack, n, 0);
      } else {
        pull_packages = false;
      }
    }

  }
  close(client_fd);
  exit(0);
}

int main(int argc, char *argv[]) {   
  if (argc < 3) {
    printf("usage: HOST PORT\n");
    return -1;
  }

  uint32_t port = atoi(argv[2]);
  char *addr = argv[1];
  int optval = 1;

  int fd = socket(AF_INET, SOCK_STREAM, 0);

  if (fd == -1) {
    printf("could not create socket\n");
    return 1;
  }


  if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (void *)(&optval), sizeof(optval)) == -1) {
    printf("could not set sockopt\n");
    return 1;
  }
  
  // convert ot sockaddr
  struct sockaddr_in sock_addr;

  sock_addr.sin_port = htons(port);
  sock_addr.sin_family = AF_INET;
  inet_pton(AF_INET, addr, &(sock_addr.sin_addr));


  if (bind(fd, (struct sockaddr*)&sock_addr, sizeof(sock_addr)) == -1) {
    printf("could not bind socket to host %d and port %d\n", argv[1], argv[2]);
    return -1;
  }

  if (listen(fd, CON_LIMIT) == -1) {
    printf("could not listen on port\n");
    return -1;
  }

  // Lets hunt down Zombies
  struct sigaction sigact;

  sigact.sa_handler = zombie_handler;
  sigemptyset(&sigact.sa_mask);
  sigact.sa_flags = SA_RESTART;

  if (sigaction(SIGCHLD, &sigact, NULL) == -1) {
    printf("could not setup sigaction handler\n");
    return -1;
  }


  printf("ready to accept connection\n");

  char mem[MAX_MEM_SIZE];
  for(;;) {
    int accept_fd = accept(fd, NULL, NULL);
    if (accept_fd == -1) {
      printf("could not accept connection\n");
      return -1;
    }

    if (fork() == 0) {
      close(fd);
      con_handler(accept_fd, mem, MAX_MEM_SIZE-1);
    }

    close(accept_fd);

  }
   
  close(fd);


  return 0;
}
