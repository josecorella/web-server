#include <arpa/inet.h>
#include <dirent.h>
#include <fcntl.h>
#include <inttypes.h>
#include <netinet/in.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/sysinfo.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <limits.h>
#include <linux/limits.h>

#include "logger.h"

#define MAX 25 /* Numbers to produce */
#define BUF_SIZE 4096 /** buffer size for our response headers */
#define FD_CAP 4 /** File descriptor array size */

/** pthread resources */
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condc = PTHREAD_COND_INITIALIZER;
pthread_cond_t condp = PTHREAD_COND_INITIALIZER;

int buffer = 0;

/** array of file file_descriptors */
int file_descriptors[FD_CAP] = { 0 };
int work = -1;
int fd_index = -1;

/** thread pool */
pthread_t *threads = NULL;


/**
 * Generates an HTTP 1.1 compliant timestamp for use in HTTP responses.
 *
 * Inputs:
 *  - timestamp: character pointer to a string buffer to be filled with the
 *    timestamp.
 */
 void generate_timestamp(char *timestamp)
 {
     time_t now = time(0);
     struct tm time = *gmtime(&now);
     strftime(timestamp, 32, "%a, %d %b %Y %H:%M:%S %Z", &time);
 }

 /**
  * Retrieves the next token from a string.
  *
  * Parameters:
  * - str_ptr: maintains context in the string, i.e., where the next token in the
  *   string will be. If the function returns token N, then str_ptr will be
  *   updated to point to token N+1. To initialize, declare a char * that points
  *   to the string being tokenized. The pointer will be updated after each
  *   successive call to next_token.
  *
  * - delim: the set of characters to use as delimiters
  *
  * Returns: char pointer to the next token in the string.
  */
 char *next_token(char **str_ptr, const char *delim)
 {
     if (*str_ptr == NULL)
 	{
         return NULL;
     }

     size_t tok_start = strspn(*str_ptr, delim);
     size_t tok_end = strcspn(*str_ptr + tok_start, delim);

     /* Zero length token. We must be finished. */
     if (tok_end  == 0)
 	{
         *str_ptr = NULL;
         return NULL;
     }

     /* Take note of the start of the current token. We'll return it later. */
     char *current_ptr = *str_ptr + tok_start;

     /* Shift pointer forward (to the end of the current token) */
     *str_ptr += tok_start + tok_end;

     if (**str_ptr == '\0')
 	{
         /* If the end of the current token is also the end of the string, we
          * must be at the last token. */
         *str_ptr = NULL;
     }
 	else
 	{
         /* Replace the matching delimiter with a NUL character to terminate the
          * token string. */
         **str_ptr = '\0';

         /* Shift forward one character over the newly-placed NUL so that
          * next_pointer now points at the first character of the next token. */
         (*str_ptr)++;
     }

     return current_ptr;
 }

/**
 * Reads from a file descriptor until:
 *  - the newline ('\n') character is encountered
 *  - *length* is exceeded
 *  This is helpful for reading HTTP headers line by line.
 *
 * Inputs:
 *  - fd: file descriptor to read from
 *  - buf: buffer to store data read from *fd*
 *  - length: maximum capacity of the buffer
 *
 * Returns:
 *  - Number of bytes read;
 *  - -1 on read failure
 *  - 0 on EOF
 */
ssize_t read_line(int fd, char *buf, size_t length)
{
    size_t total_read = 0;
    while (total_read < length) {
        size_t read_sz = read(fd, buf + total_read, 1);
        if (*(buf + total_read) == '\n') {
            return total_read;
        }
        if (read_sz == 0) {
            /* EOF */
            return 0;
        } else if (read_sz == -1) {
            perror("read");
            return -1;
        }

        total_read += read_sz;
    }
    return total_read;
}

/**
  * Writes 404 status code to the response headet at the given time for trying to access
  * and invalid webpage.
  * \param response_header buffer where we will write our HTTP/1.1 response
  * \param timestamp the time the incident occured
  * \param the file descriptor for the file we tried to access.
  * return -1 indicating error
*/
int found_404(char *response_header, char *timestamp, int fd) {
    char *msg = "404 Page Not Found.";
    sprintf(response_header,
        "HTTP/1.1 404 Not Found\r\n"
        " Date: %s\r\n"
        " Content Lenght: %zu\r\n"
        "\r\n", timestamp, strlen(msg));
    write(fd ,response_header, strlen(response_header));
    write(fd, msg, strlen(msg));
    perror("stat");
    return -1;
}

/**
  * Writes 418 status code to the response headet at the given time for trying to access
  * and invalid webpage.
  * \param response_header buffer where we will write our HTTP/1.1 response
  * \param timestamp the time the incident occured
  * \param the socket descriptor we failed to listen from.
  * return -1 indicating error
*/
void found_500(char *response_header, char *timestamp, int sd) {
    char *msg = "418 I know you like coffee but I am merely just a Teapot";
    sprintf(response_header,
        "HTTP/1.1 404 Not Found\r\n"
        " Date: %s\r\n"
        " Content Lenght: %zu\r\n"
        "\r\n", timestamp, strlen(msg));
    write(sd ,response_header, strlen(response_header));
    write(sd, msg, strlen(msg));
    perror("listen");
    return;
}
/**
 * Handles the request by the socket upon listening.
 * \param fd page we will try to access
 * \note Thread Safe.
 * \return 0 on success non-zero otherwise
 */
int handle_request(int fd)
{
    char path[PATH_MAX] = { 0 };
    char request[PATH_MAX] = { 0 };
    ssize_t bytes = 0;

    bytes = read_line(fd, request, PATH_MAX);
    if (bytes == -1) {
        perror("read");
        return -1;
    } else if (bytes == 0) {
        /* EOF */
        LOG("%s\n", "Reached end of stream");
        return 0;
    }

    char *next_tok = request;
    char *curr_tok;

    curr_tok = next_token(&next_tok, " \t\r\n");

    if (strcmp(curr_tok, "GET") == 0) {
        curr_tok = next_token(&next_tok, " \t\r\n");
        path[0] = '.';
        strcpy(&path[1], curr_tok);
        LOG("path = %s\n", path);
    }
    if (curr_tok == NULL) {
        return -1;
    }

    //generate timestamp
    char timestamp[32];
    generate_timestamp(timestamp);

    //generate response
    char response_header[PATH_MAX] = { 0 };
    struct stat st;
    int ret = stat(path, &st);
    if (ret == -1) {
        perror("stat");
        return found_404(response_header, timestamp, fd);
    }

    if(S_ISDIR(st.st_mode)) {
        strcat(path, "/index.html");
        LOG("path = %s\n", path);
        ret = stat(path, &st);
        if (ret == -1) {
            return found_404(response_header, timestamp, fd);
        }
    }

    sprintf(response_header,
        "HTTP/1.1 200 OK\r\n"
        " Date: %s\r\n"
        " Content Lenght: %zu\r\n"
        "\r\n", timestamp, st.st_size);

    write(fd, response_header, strlen(response_header));
    int file_fd = open(path, O_RDONLY);

    //put this in a loop
    long int upload_size = 0;
    while (upload_size < st.st_size) {
        upload_size += sendfile(fd, file_fd, &upload_size, st.st_size);
        LOG("This is the upload_size: %ld, this the file size: %ld\n", upload_size, st.st_size);
    }

    close(file_fd);
    return 0;
}

/**
  * Function used for handling requests using a multithread approach
  * \param args used for convention purposes
*/
void *thread_func(void *args)
{
    while (true) {
        pthread_mutex_lock(&mutex);
        while (work == -1) {
            pthread_cond_wait(&condc, &mutex);
        }
        int client_fd = file_descriptors[fd_index];
        --fd_index;
        if (fd_index == -1) {
            work = -1;
        }
        pthread_mutex_unlock(&mutex);
        handle_request(client_fd);
        shutdown(client_fd, SHUT_RDWR);
    }
    return 0;
}

/** Main driver for our server. */
int main(int argc, char *argv[]) {

    if (argc != 3) {
        printf("Usage: %s port dir\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    char *dir = argv[2];

    int ret = chdir(dir);
    if (ret == -1) {
        perror("chdir");
        return EXIT_FAILURE;
    }

    LOG("Serving files from directory: %s\n", dir);

    //allocate memory for our thread pool
    int num_threads = get_nprocs() * 2;
    threads = malloc(sizeof(pthread_t) * num_threads);
    for (int i = 0; i < num_threads; i++) {
        pthread_create(&threads[i], NULL, thread_func, NULL);
        LOG("This is the thread: %p\n", &threads[i]);
    }

    int socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd == -1) {
        perror("socket");
        return 1;
    }

    struct sockaddr_in addr = { 0 };
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(socket_fd, (struct sockaddr *) &addr, sizeof(addr)) == -1) {
        perror("bind");
        return EXIT_FAILURE;
    }

    if (listen(socket_fd, 10) == -1) {
        perror("listen");
        char response_header[PATH_MAX] = { 0 };
        //generate timestamp
        char timestamp[32];
        generate_timestamp(timestamp);
        found_500(response_header, timestamp, socket_fd);
        return 1;
    }

    LOG("Listening on port: %d\n", port);

    while (true) {
        /** Outer loop: this keeps accpeting connnection */
        struct sockaddr_in client_addr = { 0 };
        socklen_t slen = sizeof(client_addr);

        int client_fd = accept(
            socket_fd,
            (struct sockaddr *) &client_addr,
            &slen
        );

        if (client_fd == -1) {
            perror("accept");
            return EXIT_FAILURE;
        }

        char remote_host[INET_ADDRSTRLEN];
        inet_ntop(
            client_addr.sin_family,
            (void *) &((&client_addr)->sin_addr),
            remote_host,
            sizeof(remote_host)
        );
        LOG("Accepted connection from %s:%d\n", remote_host, client_addr.sin_port);

        //add fd to array
        pthread_mutex_lock(&mutex);
        ++fd_index;
        file_descriptors[fd_index] = client_fd;
        work = 0;
        /* Wake up the consumer */
        pthread_cond_signal(&condc);
        pthread_mutex_unlock(&mutex);
        if (fd_index == -1) {
            work = -1;
        }

    }

    // join the threads so that clean up can happen
    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
    }
    free(threads);

    return 0;
}
