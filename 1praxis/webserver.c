#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <signal.h>
#include <string.h>


#define MAX_MEM_SIZE 8192
#define CON_LIMIT 20

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

void con_handler(int client_fd, void* accept_pack_mem, size_t mem_size) {
  // to make sure we lose no date when payload is large
  bool got_method = false;
  bool got_header = false;
  for(;;) {
    memset(accept_pack_mem, 0x0, mem_size);
    if (recv(client_fd, accept_pack_mem, mem_size, 0) == -1 ) {
      printf("Error recv");
      exit(0);
    }
    //send(client_fd, "Reply", 9, 0);

    size_t n = 0;
    size_t idx = 0;
    while (idx < mem_size) {
      n = get_line_idx(&(((char *)accept_pack_mem)[idx]), mem_size);
      idx = idx + n + 2;

      if (got_header == true ) {
        got_method = false;
        got_header = false;
        send(client_fd, "Reply\r\n\r\n", 9, 0);
      }

      if (got_method == false ) {
        got_method = true;
      } else {
        if (n == 0 && got_header == false) {
          got_header = true;
        } 
      }
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
