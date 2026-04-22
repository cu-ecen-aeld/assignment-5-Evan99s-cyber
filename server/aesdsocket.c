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

#define PORT "9000"
#define DATA_FILE "/var/tmp/aesdsocketdata"
#define BUFFER_SIZE 1024

int server_fd = -1;
int client_fd = -1;

void cleanup() {
    if (client_fd != -1) close(client_fd);
    if (server_fd != -1) close(server_fd);
    unlink(DATA_FILE);
    closelog();
}

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        syslog(LOG_INFO, "Caught signal, exiting");
        cleanup();
        exit(0);
    }
}

int main(int argc, char *argv[]) {
    openlog("aesdsocket", LOG_PID, LOG_USER);

    // Setup Signal Handling
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    if (sigaction(SIGINT, &sa, NULL) == -1 || sigaction(SIGTERM, &sa, NULL) == -1) {
        perror("sigaction");
        return -1;
    }

    // Inside main, before socket setup:
int daemon_mode = 0;
if (argc > 1 && strcmp(argv[1], "-d") == 0) {
    daemon_mode = 1;
}

// ... setup socket, bind, and listen FIRST ...

if (daemon_mode) {
    if (daemon(0, 0) == -1) {
        syslog(LOG_ERR, "Failed to enter daemon mode");
        return -1;
    }
}

    // 1. Create Socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        syslog(LOG_ERR, "Socket creation failed: %s", strerror(errno));
        return -1;
    }

    // Allow port reuse
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. Bind to Port 9000
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

    // 3. Listen
    if (listen(server_fd, 10) == -1) {
        syslog(LOG_ERR, "Listen failed: %s", strerror(errno));
        close(server_fd);
        return -1;
    }

// Insert this between lines 78 and 80
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed");
            return -1;
        }
        if (pid > 0) {
            // Parent exits to return control to shell/GitHub runner
            exit(0);
        }
        // Child continues - redirect standard streams
        if (setsid() < 0) return -1;
        if (chdir("/") < 0) return -1;
        
        int dev_null = open("/dev/null", O_RDWR);
        dup2(dev_null, STDIN_FILENO);
        dup2(dev_null, STDOUT_FILENO);
        dup2(dev_null, STDERR_FILENO);
        close(dev_null);
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

        // 4. Receive data and append to file
        FILE *fp = fopen(DATA_FILE, "a+");
        if (!fp) {
            syslog(LOG_ERR, "File open failed");
            close(client_fd);
            continue;
        }

        char *buffer = malloc(BUFFER_SIZE);
        if (!buffer) {
            syslog(LOG_ERR, "Malloc failed");
            fclose(fp);
            close(client_fd);
            continue;
        }

        ssize_t bytes_recv;
        while ((bytes_recv = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
            fwrite(buffer, 1, bytes_recv, fp);
            
            // Check if packet is complete (ends in newline)
            if (memchr(buffer, '\n', bytes_recv)) {
                break; 
            }
        }

        while ((bytes_recv = recv(client_fd, buffer, BUFFER_SIZE, 0)) > 0) {
    fwrite(buffer, 1, bytes_recv, fp);
    if (memchr(buffer, '\n', bytes_recv)) {
        fflush(fp); // CRITICAL: Force the data onto the disk
        break; 
    }
}
        
        // 5. Send full file content back to client
        fseek(fp, 0, SEEK_SET);
        size_t bytes_read;
        char read_buf[BUFFER_SIZE];
        while ((bytes_read = fread(read_buf, 1, BUFFER_SIZE, fp)) > 0) {
            send(client_fd, read_buf, bytes_read, 0);
        }

        free(buffer);
        fclose(fp);
        close(client_fd);
        client_fd = -1;
        syslog(LOG_INFO, "Closed connection from %s", client_ip);
    }

    return 0;
}
