#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <fcntl.h>
#include <errno.h>

#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

int server_fd = -1;
int client_fd = -1;

void cleanup() {
    if (client_fd != -1) close(client_fd);
    if (server_fd != -1) close(server_fd);
    unlink(DATA_FILE);
    syslog(LOG_INFO, "Cleanup complete, closing log.");
    closelog();
}

void signal_handler(int sig) {
    syslog(LOG_INFO, "Caught signal, exiting");
    cleanup();
    exit(0);
}

int main(int argc, char *argv[]) {
    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Setup Signal Handling
    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = signal_handler;
    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    // 1. Setup Socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(9000);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Bind failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    // 2. Daemonize ONLY after bind (so parent exits only if bind succeeds)
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        if (daemon(0, 0) == -1) {
            syslog(LOG_ERR, "Daemonization failed: %s", strerror(errno));
            return -1;
        }
    }

    if (listen(server_fd, 10) == -1) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd == -1) {
            syslog(LOG_ERR, "Accept failed: %s", strerror(errno));
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, sizeof(client_ip));
        syslog(LOG_INFO, "Accepted connection from %s", client_ip);

        // 3. Receive and Append
        FILE *fp = fopen(DATA_FILE, "a+");
        if (!fp) {
            syslog(LOG_ERR, "File open failed");
            close(client_fd);
            continue;
        }

        char buffer[BUFFER_SIZE];
        ssize_t bytes_recv;
        
        // Single loop to handle stream until newline
        while ((bytes_recv = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
            fwrite(buffer, 1, bytes_recv, fp);
            if (memchr(buffer, '\n', bytes_recv)) {
                break; 
            }
        }
        
        fflush(fp);

        // 4. Reset file pointer and Send everything back
        fseek(fp, 0, SEEK_SET);
        char read_buf[BUFFER_SIZE];
        size_t bytes_read;
        while ((bytes_read = fread(read_buf, 1, BUFFER_SIZE, fp)) > 0) {
            send(client_fd, read_buf, bytes_read, 0);
        }

        fclose(fp);
        close(client_fd);
        client_fd = -1;
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    cleanup();
    return 0;
}
