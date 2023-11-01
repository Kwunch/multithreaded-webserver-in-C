#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <sys/mman.h>
#include <pthread.h>
#include <time.h>

// Global variables
// Create and open global log file called server_thread.txt
FILE *fp;
// Create mutex
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
int sfd;
// Should be able to handle multiple clients at once each in their own thread
// Create thread
pthread_t thread;

// Function to handle client
void *handle_client(void *arg)
{
    // Get CPU time at start of worker
    // Use clock_gettime() to get CPU time
    struct timespec start, end;
    double cpu_time_used;

    // Get CPU time at start of worker
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &start);

    // Cast argument to int
    int connfd = (int)arg;

    // At this point a client has connected. The remainder of the
    // loop is handling the client's GET request and producing
    // our response.

    char buffer[1024];
    char filename[1024];
    FILE *f;

    memset(buffer, sizeof(buffer), 0);
    memset(filename, sizeof(buffer), 0);

    // We read into our buffer until we have a full request line
    // or our buffer size exhausts. A real server should really
    // parse this buffer and break it up into its constituents
    // (method, URI, etc.)
    int i = 0;
    while (strstr(buffer, "\r\n\r\n") == NULL && i < sizeof(buffer))
    {
        int bytes_read = read(connfd, buffer + i, 1);
        if (bytes_read < 0)
        {
            perror("read failed");
            exit(EXIT_FAILURE);
        }
        i++;
    }

    // If buffer size exhausted, return error
    if (i == sizeof(buffer))
    {
        perror("Request too long\n");
        return NULL;
    }

    // If buffer is empty, return error
    if (strlen(buffer) == 0)
    {
        perror("Buffer is empty\n");
        return NULL;
    }

    // We have a full request. Respond with a simple HTTP web page.
    // First we parse
    if (sscanf(buffer, "GET /%s", filename) < 1)
    {
        perror("Bad HTTP request\n");
        close(connfd);
        return NULL;
    }

    // If the HTTP request is bigger than our buffer can hold, we need to call
    // recv() until we have no more data to read, otherwise it will be
    // there waiting for us on the next call to recv(). So we'll just
    // read it and discard it. GET should be the first 3 bytes, and we'll
    // assume paths that are smaller than about 1000 characters.
    if (i == sizeof(buffer))
    {
        // if recv returns as much as we asked for, there may be more data
        while (recv(connfd, buffer, sizeof(buffer), 0) == sizeof(buffer))
            /* discard */;
    }

    // If the request is a directory, return error
    if (strstr(filename, "/") != NULL)
    {
        perror("Directory not supported\n");
        return NULL;
    }

    // If the request is a file, open file
    f = fopen(filename, "rb");
    if (f == NULL)
    {
        // Assume that failure to open the file means it doesn't exist
        strcpy(buffer, "HTTP/1.1 404 Not Found\n\n");
        send(connfd, buffer, strlen(buffer), 0);
    }
    else
    {
        int size;
        char response[1024];

        strcpy(response, "HTTP/1.1 200 OK\n");
        send(connfd, response, strlen(response), 0);

        time_t now;
        time(&now);

        // How convenient that the HTTP Date header field is exactly
        // in the format of the asctime() library function.
        //
        // asctime adds a newline for some dumb reason.
        sprintf(response, "Date: %s", asctime(gmtime(&now)));
        send(connfd, response, strlen(response), 0);

        // Get the file size via the stat system call
        struct stat file_stats;
        fstat(fileno(f), &file_stats);
        sprintf(response, "Content-Length: %ld\n", file_stats.st_size);
        send(connfd, response, strlen(response), 0);

        // Tell the client we won't reuse this connection for other files
        strcpy(response, "Connection: close\n");
        send(connfd, response, strlen(response), 0);

        // Send our MIME type and a blank line
        strcpy(response, "Content-Type: text/html\n\n");
        send(connfd, response, strlen(response), 0);

        fprintf(stderr, "File: %s\n", filename);

        int bytes_read = 0;
        do
        {
            // Read response amount of data at a time
            // Note that sizeof() in C can only tell us the number of
            // elements in an array that is declared in scope. If you
            // move the declaration elsewhere, it will degrade into
            // the sizeof a pointer instead.
            bytes_read = fread(response, 1, sizeof(response), f);

            // If we read anything, send it
            if (bytes_read > 0)
            {
                int sent = send(connfd, response, bytes_read, 0);
                // It's possible that send wasn't able to send all of
                // our response in one call. It will return how much it
                // actually sent. Keep calling send until all of it is
                // sent.
                while (sent < bytes_read)
                {
                    sent += send(connfd, response + sent, bytes_read - sent, 0);
                }
            }
        } while (bytes_read > 0 && bytes_read == sizeof(response));

        fclose(f);

        // Get CPU time at end of worker
        clock_gettime(CLOCK_THREAD_CPUTIME_ID, &end);

        // Calculate elapsed time
        // Time is seconds and has 4 decimal places of precision
        cpu_time_used = (end.tv_sec - start.tv_sec) + (end.tv_nsec - start.tv_nsec) / 1000000000.0;

        // write the code to get cput end time in seconds with 4 decimals


        // Print file name, file size, cpu end time, and elapsed time to log file
        // Exclusively write to file using mutex
        // Mutex lock, write to file, mutex unlock
        // File format: <file name> <file size> <cpu end time in seconds with 4 decimals> <elapsed time in seconds with 4 decimals>
        pthread_mutex_lock(&mutex);
        fprintf(fp, "%s\t%ld\t%.4f\t%.4f\n", filename, file_stats.st_size, (end.tv_sec + end.tv_nsec) / 1000000000.0, cpu_time_used);
        pthread_mutex_unlock(&mutex);
    }
    // Close connection
    close(connfd);
}

// Setup signal handler for ctrl-c
void signal_handler(int sig)
{

    printf("\nTerminating server\n");
    
    // Safely terminate server
    // Join threads and close sfds
    int status = pthread_join(thread, NULL);
    if (status != 0)
    {
        perror("pthread_join failed");
        exit(EXIT_FAILURE);
    }
    close(sfd);
    fclose(fp);
    exit(0);
}


int main()
{
    // Setup signal handler for ctrl-c
    signal(SIGINT, signal_handler);

    // Create file server_thread.txt
    // File logs info in format <file name> <file size> <cpu end time> <elapsed time>
    fp = fopen("server_thread.txt", "a");

    // Sockets represent potential connections
    // We make an internet socket
    sfd = socket(PF_INET, SOCK_STREAM, 0);
    if (-1 == sfd)
    {
        perror("Cannot create socket\n");
        exit(EXIT_FAILURE);
    }

    // We will configure it to use this machine's IP, or
    // for us, localhost (127.0.0.1)
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));

    addr.sin_family = AF_INET;
    // Web servers always listen on port 80
    addr.sin_port = htons(80);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    // So we bind our socket to port 80
    if (-1 == bind(sfd, (struct sockaddr *)&addr, sizeof(addr)))
    {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }

    // And set it up as a listening socket with a backlog of 10 pending connections
    if (-1 == listen(sfd, 10))
    {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }

    // A server's gotta serve...
    for (;;)
    {

        // accept() blocks until a client connects. When it returns,
        // we have a client that we can do client stuff with.
        int connfd = accept(sfd, NULL, NULL);
        if (connfd < 0)
        {
            perror("Accept failed");
            exit(EXIT_FAILURE);
        }

        pthread_create(&thread, NULL, handle_client, (void *)connfd);
    }

    return 0;
}
