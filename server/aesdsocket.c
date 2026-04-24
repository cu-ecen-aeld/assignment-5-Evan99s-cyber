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

// Testing
// The autotester prefers this path in Yocto
#define DATA_FILE "/var/run/aesdsocketdata"
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
    syslog(LOG_INFO, "Caught signal, exiting");
    cleanup();
    exit(0);
}

int main(int argc, char *argv[]) {
    openlog("aesdsocket", LOG_PID, LOG_USER);

    struct sigaction sa;
    memset(&sa, 0, sizeof(struct sigaction));
    sa.sa_handler = signal_handler;
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(9000);

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        syslog(LOG_ERR, "Bind failed");
        return -1;
    }

    // DAEMONIZE HERE - After bind succeeds
    if (argc > 1 && strcmp(argv[1], "-d") == 0) {
        daemon(0, 0);
    }

    listen(server_fd, 10);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        FILE *fp = fopen(DATA_FILE, "a+");
        char buffer[BUFFER_SIZE];
        ssize_t nr;

        // Read until newline
        while ((nr = recv(client_fd, buffer, sizeof(buffer), 0)) > 0) {
            fwrite(buffer, 1, nr, fp);
            if (memchr(buffer, '\n', nr)) break;
        }

        // Force data to disk so the next read sees it
        fflush(fp);
        
        // Send everything back
        fseek(fp, 0, SEEK_SET);
        while ((nr = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
            send(client_fd, buffer, nr, 0);
        }

        fclose(fp);
        close(client_fd);
    }
    return 0;
}
