#include <arpa/inet.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>

int main(int argc, char *argv[]) {   
  if (argc < 3) {
    printf("usage: HOST PORT\n");
    return -1;
  }

  uint32_t port = atoi(argv[2]);
  char *addr = argv[1];

  int fd = socket(AF_INET, SOCK_STREAM, 0);

  if (fd == -1) {
    printf("could not create socket\n");
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

  if (listen(fd, 20) == -1) {
    printf("could not listen on port\n");
    return -1;
  }

  printf("ready to accept connection\n");

  int accept_fd = accept(fd, NULL, NULL);
  if (accept_fd == -1) {
    printf("could not accept connection\n");
    return -1;
  }
  

  close(accept_fd);
  close(fd);


  return 0;
}
