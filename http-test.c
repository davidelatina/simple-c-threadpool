// http-test.c
//
/** @file
 * Simple test program sending hello world over HTTP -
 * with a thread pool.
 *
 * @p threads_num number of threads to use for handling client requests.
 * Default: 4.
 */

#include <arpa/inet.h>
#include <stdlib.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include <threads.h>
#include "threadpool.h"

#ifndef PRINT_ERROR_LOCATION
  #define PRINT_ERROR_LOCATION \
    {fprintf(stderr, "%s -> %s -> line %i\n" ,\
    __FILE_NAME__, __func__, __LINE__);}
#endif

#define PORT 8080
#define BUFFER_SIZE 1024

/** Number of threads to use if parameter could not be read.*/
#define THREADS_NUM_DEFAULT 4

/** Simple HTTP "hello, world" response */
#define HELLO_WORLD_HTTP (char const* const)              \
                         "HTTP/1.0 200 OK\r\n"            \
                         "Server: http-test\r\n"          \
                         "Content-type: text/html\r\n\r\n"\
                         "<html>hello, world</html>\r\n"
/**
 * @brief Function to send a simple "hello, world" message over HTTP.
 *
 * Meant to be run in a separate thread.
 *
 * @param arg void pointer to a socket file descriptor.
 * Each thread must receive a separate pointer.
 */
void hello_world_http_send(void* arg);

/**
 * Takes in number of threads as parameter.
 */
int main(int argc, char* argv[argc]) {
  // Set number of threads
  size_t threads_num = THREADS_NUM_DEFAULT;
  if (argc >= 2) {
    size_t temp = strtoull(argv[1], NULL, 10);
    if (0 < temp && temp < 1024)
      threads_num = temp;
    else
      fprintf(stderr,
              "invalid number of threads, using the default of %zu",
              threads_num);
  }

  // Create a socket file descriptor
  int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd == -1) {
    perror("socket error");
    return EXIT_FAILURE;
  }

  // Create an address to bind the socket to
  struct sockaddr_in address_host;
  size_t address_host_length = sizeof(address_host);

  address_host.sin_family = AF_INET;
  // Convert from host to network byte order
  address_host.sin_port = htons(PORT);
  address_host.sin_addr.s_addr = htonl(INADDR_ANY);

  // Set the "reuse address" socket option, integer boolean flag
  int option_reuse = 1;
  int err = setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR,
                      &option_reuse,
                      sizeof(option_reuse));
  if (err) {
    perror("set socket option error");
    return EXIT_FAILURE;
  }
  // Bind socket to host address
  err = bind(sockfd, (struct sockaddr*) &address_host, address_host_length);
  if (err) {
    perror("bind error");
    return EXIT_FAILURE;
  }

  // Listen for incoming connections
  err = listen(sockfd, SOMAXCONN);
  // SOMAXCONN: max listen queue length from socket.h
  if (err == -1) {
    fprintf(stderr, "listen error");
    return EXIT_FAILURE;
  }


  // Initialize thread pool
  threadpool* pool = threadpool_create(threads_num);
  if (!pool) {
    fprintf(stderr,"error creating thread pool");
    return EXIT_FAILURE;
  }

  for (size_t i = 0; i < 8; i++) {
    // Allocate separate space for each thread's input
    int* newsockfd_ptr = malloc(sizeof(int));
    if (!newsockfd_ptr) {
      fprintf(stderr, "Out of memory");
      return EXIT_FAILURE;
    }
    // Obtain new socket file descriptor
    *newsockfd_ptr = accept(
                      sockfd,
                      (struct sockaddr*) &address_host,
                      (socklen_t*) &address_host_length
                    );
    if (*newsockfd_ptr == -1) {
      perror("accept error");
      free(newsockfd_ptr);
      return EXIT_FAILURE;
    }

    // Thread will keep a copy of the socket f.d., then close and free it
    threadpool_queue(pool, hello_world_http_send, newsockfd_ptr);
    //close(*newsockfd_ptr);
    //free(newsockfd_ptr);
    sleep(1);
  }

  threadpool_wait(pool);
  threadpool_destroy(pool);
  close(sockfd);
  return EXIT_SUCCESS;
}

void hello_world_http_send(void* arg) {
  int* sockfd_ptr =  arg;
  int  sockfd     = *sockfd_ptr;
  free(arg);

  // Create client address
  struct sockaddr_in address_client;
  size_t address_client_length = sizeof(address_client);

  if (sockfd == -1) {
    perror("accept error");
    return;
  }

  // Get client address
  int sockn = getsockname(sockfd,
                          (struct sockaddr*) &address_client,
                          (socklen_t*) &address_client_length
                          );

  if (sockn == -1) {
    perror("getsockname error");
    fprintf(stderr, "sockfd = %d\n" \
                    "addrlen = %zu\n",sockfd, address_client_length);
    close(sockfd);
    return;
  }

  char buffer[BUFFER_SIZE];

  // Read from the socket
  int valread = read(sockfd, buffer, BUFFER_SIZE-1);
  if (valread < 0) {
    perror("read error");
    close(sockfd);
    return;
  }

  // Read the request
  char method[BUFFER_SIZE] = {0};
  char uri[BUFFER_SIZE] = {0};
  char version[BUFFER_SIZE] = {0};

  // TODO use something more sensible than sscanf
  int err = sscanf(buffer, "%s %s %s", method, uri, version);
  if (err < 3) {
    perror("buffer read error");
    close(sockfd);
    return;
  }

  printf("[%s:%u] %s %s %s\n",
         inet_ntoa(address_client.sin_addr),
         ntohs(address_client.sin_port),
         method,
         version,
         uri);

  // Write response to socket
  int valwrite = write(sockfd, HELLO_WORLD_HTTP, strlen(HELLO_WORLD_HTTP));
  if (valwrite < 0) {
    perror("valwrite error");
  }

  //Must be closed here
  close(sockfd);
  sleep(3);
}
