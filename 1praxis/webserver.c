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
#include <pthread.h>

#define MAX_MEM_SIZE 8192
#define CON_LIMIT 20

enum REQ_TYPES {
  GET,
  HEAD,
  PUT,
  DELETE,
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


struct static_pair {
  char *path;
  char *content; 
} typedef pair_t;

struct dynamic_array {
  pair_t *array;
  size_t size;
  size_t used;
} typedef pair_vector_t;

void zombie_handler() {
  // We could save errno but i dont care
  while (waitpid(-1, NULL, WNOHANG) > 0);
}
/*
size_t get_line_idx(char mem[], size_t mem_size) {
  for (size_t i = 0; i < mem_size-1; i++) {
    if (mem[i] == '\r' && mem[i+1] == '\n') {
      return i;
    }
  }
}*/


void dynamic_init(pair_vector_t* pairs) {
  pairs->array = NULL;
  pairs->size = 0;
  pairs->used = 0;
}

int dynamic_add(pair_vector_t* pairs, pair_t pair) {
  if (pairs->size == 0) {
    pairs->size = 20;
    //pairs->array = (pair_t *)calloc(sizeof(pair_t) * pairs->size);
    pairs->array = (pair_t *)calloc(20, sizeof(pair_t));    // already start out with bunch of memory
  }

   printf("=====prev=======\n");
   printf("%d\n", pairs->used);
   printf("%d\n", pairs->size);
   printf("===============\n");

  // check if space is free
  if (pairs->used == pairs->size) {
    pairs->array = (pair_t *)reallocarray(pairs->array, pairs->size * 2, sizeof(pair_t));    

    memset(pairs->array + pairs->size * sizeof(pair_t), 0x0, sizeof(pair_t) * pairs->size);

    pairs->size *= 2;
  }

  pairs->used++;
  // find free
  for (int i = 0; i < pairs->size; i++ ) {
    if (pairs->array[i].path == NULL && pairs->array[i].content == NULL) {
      pairs->array[i].path = pair.path;
      pairs->array[i].content = pair.content;

      printf("======post=====\n");
      printf("%d\n", pairs->used);
      printf("%d\n", pairs->size);
      printf("===============\n");
      return 1;
    }
  }

  return -1;
}

int dynamic_remove(pair_vector_t* pairs, int idx) {
  if (pairs->array[idx].path == NULL) {
    return 0;
  }

  free(pairs->array[idx].path);
  free(pairs->array[idx].content);

  pairs->array[idx].path = NULL;
  pairs->array[idx].content = NULL;
  pairs->used -= 1;
  return 1;
}

// I'm sure there is a c function to compare until delimiter but i'm just implementing it myself
int cmpdelim(char *haystack, char *needle, char delim) {
  int idx = 0;

  while (needle[idx] != '\x00') {
    if (haystack[idx] != needle[idx]) {
      return 0;
    } 
    idx++;
  }
  if (haystack[idx] != delim) {
    return 0;
  }
  return 1;
}

char* strdelim(char* str, char delim) {
  size_t size = 0;
  while (str[size] != delim) {
    size++;
  }

  char *ret = calloc(size+1, sizeof(char));
  memcpy(ret, str, size);
  ret[size] = '\x00';

  return ret;
}

int dynamic_find(pair_vector_t* pairs, char *path, char delim) {
  int ret = pairs->size;

  int idx = 0;
  while (idx < pairs->size) {
    if (pairs->array[idx].path != 0x0) {
      if (cmpdelim(path, pairs->array[idx].path, delim) == 1) {
        break;
      }
    }
    idx++;
  }


  return idx; 
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
  if (cmpdelim(request->req_line, "GET", ' ') == 1) {
    request->type = GET;
  } else if (cmpdelim(request->req_line, "PUT", ' ')) {
    request->type = PUT;
  } else if (cmpdelim(request->req_line, "DELETE", ' ')) {
    request->type = DELETE;
  } else if (cmpdelim(request->req_line, "HEAD", ' ')) {
    request->type = HEAD;
  } else {
    request->type = UNK;
  }
  /*
  if (strncmp(request->req_line, "GET", 3) == 0) {
    request->type = GET;
  } if (strncmp(request->req_line, "GET", 3) == 0) {*/
}

 
bool check_valid(req_t* request, regex_t* reqline_regex, regex_t* headers_regex) {
  if (regexec(reqline_regex, request->req_line, 0, NULL, 0) != 0) {
    printf("failed req_line\n");
    return false;
  }

  if ((regexec(headers_regex, request->headers, 0, NULL, 0) != 0) && request->headers_size > 2) {
    printf("failed headers\n");
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

void print_resources(pair_vector_t* pairs) {
  for (int i = 0; i < pairs->size; i++) {
    if (pairs->array[i].content != NULL) {
      printf("Path: %s\n", pairs->array[i].path);
      printf("Content: %s\n", pairs->array[i].content);
    } else {
      printf("Path: NULL\n");
      printf("Content: NULL\n");
    }
    printf("\n");
  }
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
  req_line_content = (char *)calloc(req_line_size + 1, sizeof(char)); 

  strncpy(req_line_content, req_line_start, req_line_size);


  // read header
  header_start = req_line_start + req_line_size;
  header_size = ((header_end + 4) - (req_line_end + 2));


  header_content = (char *)calloc(header_size + 1, sizeof(char));

  strncpy(header_content, header_start, header_size);

  bytes_read += header_size;

  if (header_size > 2) {
    // chech if payload present

    char* cl_idx = strstr(header_content, "Content-Length: ");

    if (cl_idx != NULL) {
      payload_size = (size_t)strtol(cl_idx + 16, NULL, 10); 

      if (bytes_read + payload_size > buffer->tail) {
        free(header_content);
        free(req_line_content);

        return 0;
      } else {
        payload_content = (char *)malloc(payload_size + 1);
        memset(payload_content, 0x0, payload_size + 1);

        strncpy(payload_content, header_start + header_size, payload_size); 

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
    response->pack = strdup("HTTP/1.1 404 Not Found\r\n\r\n\x00");
    response->pack_size += strlen(response->pack);
  } else if (response->value == 403) {
    response->pack = strdup("HTTP/1.1 403 Forbidden\r\n\r\n\x00");
    response->pack_size += strlen(response->pack);
  } else if (response->value == 201) {
    response->pack = strdup("HTTP/1.1 201 Created\r\n\r\n\x00");
    response->pack_size += strlen(response->pack);
  } else if (response->value == 204) {
    response->pack = strdup("HTTP/1.1 204 No Content\r\n\r\n\x00");
    response->pack_size += strlen(response->pack);
  } else if (response->value == 200) {
    response->pack = strdup("HTTP/1.1 200 Ok\r\n\r\n\x00");
    response->pack_size += strlen(response->pack);
  } else if (response->value == 400) {
    response->pack = strdup("HTTP/1.1 400 Bad Request\r\n\r\n\x00");
    response->pack_size += strlen(response->pack);
  } else {
    response->pack = strdup("HTTP/1.1 501 Not Implemented\r\n\r\n\x00");
    response->pack_size += strlen(response->pack);

  }

  char buffer[20];
  memset(buffer, 0x0, 20);
  sprintf(buffer, "%ld", response->payload_size);

  size_t size_str_len = strlen(buffer);

  response->pack = (char *)realloc(response->pack, response->pack_size - 2 + 16 + size_str_len + 4 + response->payload_size + 1);

  // construct package
  strcpy(response->pack + response->pack_size - 2, "Content-Length: \x00");
  response->pack_size += 16-2;

  strcpy(response->pack + response->pack_size, buffer);
  response->pack_size += size_str_len;

  strcpy(response->pack + response->pack_size, "\r\n\r\n\x00");
  response->pack_size += 4;
  if (response->payload_size > 0) {
    strcpy(response->pack + response->pack_size, response->payload);
    response->pack_size += response->payload_size;
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

void init_buffer(stream_buffer_t *buffer) {
  buffer->buffer = (char*)malloc(MAX_MEM_SIZE*2 + 80); // enough memory
  buffer->size = MAX_MEM_SIZE*2 + 80;
  buffer->tail = 0;
}

void init_request(req_t *request) {
  request->req_line = NULL;
  request->headers = NULL;
  request->payload = NULL;
}

void init_response(resp_t *response) {
  response->payload = NULL;
  response->pack = NULL;
}

struct args_struct {
  int sock;
  pair_vector_t* pairs;
  regex_t reqline_regex;
  regex_t headers_regex;
} typedef args_t;

//void con_handler(void* client_fd_ptr, void* accept_pack_mem, const size_t mem_size, pair_vector_t *pair) {
void *con_handler(void* arguments) {
  // unpack args
  args_t* args =  (args_t *)arguments;
  int client_fd = args->sock;
  pair_vector_t* pairs = args->pairs;
  regex_t reqline_regex = args->reqline_regex;
  regex_t headers_regex = args->headers_regex;
  
 // to make sure we lose no date when payload is large
  stream_buffer_t buffer;
  req_t current_request;
  resp_t response;


  init_buffer(&buffer);
  init_request(&current_request);
  init_response(&response);

  char *accept_pack_mem = (char*)malloc(MAX_MEM_SIZE);

  flush_buffer(&buffer);


  memset(accept_pack_mem, 0x0, MAX_MEM_SIZE);
  while (recv(client_fd, accept_pack_mem, MAX_MEM_SIZE, 0) > 0) {
    push_to_buffer(&buffer, accept_pack_mem, MAX_MEM_SIZE); 


    memset(accept_pack_mem, 0x0, MAX_MEM_SIZE);

    while (pull_from_buffer(&buffer, &current_request) > 0) {
        flush_response(&response);

        bool is_valid = check_valid(&current_request, &reqline_regex, &headers_regex);
        if (is_valid) {
          set_type(&current_request);


          if (current_request.type == GET) {

            response.value = 404;
            int idx = dynamic_find(pairs, current_request.req_line + 4, ' ');
            if (idx < pairs->size) {
              response.value = 200;

              response.payload = strdup(pairs->array[idx].content);
              response.payload_size = strlen(pairs->array[idx].content);
            }

          } else if (current_request.type == PUT) {
            if (cmpdelim(current_request.req_line + 4, "/dynamic\x00", '/')) {
              response.value = 204;


              int idx = dynamic_find(pairs,  current_request.req_line + 4, ' ');
              if (idx < pairs->size) {

                free(pairs->array[idx].content);
                pairs->array[idx].content = strdup(current_request.payload);
              } else { 
                response.value = 201;

                pair_t pair_to_add;

                pair_to_add.content = strdup(current_request.payload);
                pair_to_add.path = strdelim(current_request.req_line + 4, ' ');
                dynamic_add(pairs, pair_to_add); 
              }
            } else {
              response.value = 403;
            }

          } else if (current_request.type == DELETE) {
            if (cmpdelim(current_request.req_line + 7, "/dynamic\x00", '/')) {
              response.value = 404;
 
              int idx = dynamic_find(pairs, current_request.req_line + 7, ' ');
              if (idx < pairs->size) {
                response.value = 204;

                dynamic_remove(pairs, idx); 
              }
            } else {
              response.value = 403;
            }

          } else {
            response.value = 501;
          }
        }
 
      
        size_t n = pack_response(&response);
        send(client_fd, response.pack, n, 0);
        flush_request(&current_request);
      }
  }
}

int main(int argc, char *argv[]) {   
  if (argc < 3) {
    printf("usage: HOST PORT\n");
    return -1;
  }

  uint32_t port = atoi(argv[2]);
  char *addr = argv[1];
  int optval = 1;


  /////////
  // SETUP
  /////////
  pair_vector_t* pairs = (pair_vector_t *)malloc(sizeof(pair_vector_t));
  dynamic_init(pairs);

  pair_t last_pair;
  last_pair.path = strdup("/static/foo");
  last_pair.content = strdup("Foo");
  printf("heap_addr %p", last_pair.path);
  dynamic_add(pairs, last_pair);

  last_pair.path = strdup("/static/bar\x00");
  last_pair.content = strdup("Bar\x00");
  dynamic_add(pairs, last_pair);

  last_pair.path = strdup("/static/baz\x00");
  last_pair.content = strdup("Baz\x00");
  dynamic_add(pairs, last_pair);
  
  regex_t reqline_regex;
  regex_t headers_regex;
  char *req_line_pattern = "^(GET|HEAD|PUT|DELETE) ([^ \t\n\v\f\r]+) HTTP/[0-9].[0-9]\r\n$";
  char *headers_pattern = "^([^ \t\n\v\f\r]+: [^ \t\n\v\f\r]+\r\n)+\r\n$";

  // check regex
  if (regcomp(&reqline_regex, req_line_pattern, REG_EXTENDED)) {
    printf("could not compile regex\n");
  }
  
  if (regcomp(&headers_regex, headers_pattern, REG_EXTENDED)) {
    printf("could not compile regex\n");
  }  


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
    printf("HELLO!!!");
    int accept_fd = accept(fd, NULL, NULL);
    if (accept_fd == -1) {
      printf("could not accept connection\n");
      return -1;
    }

    pthread_t thread_id;

    args_t arguments = {accept_fd, pairs, reqline_regex, headers_regex};
    if (pthread_create(&thread_id, NULL, con_handler, (void *)&arguments) < 0) {
      printf("could not create thread");
      return -1;
    }

    pthread_join(thread_id, NULL);

  }
  //close(accept_fd);
   
  close(fd);


  return 0;
}
