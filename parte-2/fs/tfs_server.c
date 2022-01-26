#include "operations.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUFFER_SIZE (40)
#define MAX_CLIENTS (64)

typedef struct Client {
    int rx; // pipe which the client reads to
    int tx; // pipe which the client writes to
    int session_id;
    char *pipename;
} Client;

int next_session_id = 1;
Client clients[MAX_CLIENTS];

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);

    // unlink pipe
    if (unlink(pipename) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", pipename,
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    // create pipe
    if (mkfifo(pipename, 0777) != 0) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    // open pipe for reading
    int rx = open(pipename, O_RDONLY);
    if (rx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }

    char op_code;
    bool shutting_down = false;
    int session_id;
    int fhandle;
    int flags;
    int tx;
    size_t len;
    ssize_t ret;
    int tfs_ret_int;
    ssize_t tfs_ret_ssize_t;
    char filename[BUFFER_SIZE];
    char client_pipename[BUFFER_SIZE];
    char *buffer;

    do {
        ret = read(rx, &op_code, sizeof(char));
        if (ret == 0) {
            close(rx);
            rx = open(pipename, O_RDONLY);
            if (rx == -1) {
                fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
        }

        // printf for debug purposes - TODO delete it
        printf("[INFO]: Received op_code %c\n", op_code);

        switch (op_code) {
            case TFS_OP_CODE_MOUNT:
                ret = read(rx, client_pipename, sizeof(char) * (BUFFER_SIZE - 1));
                for (ssize_t i = ret; i < BUFFER_SIZE - 1; i++) {
                    client_pipename[i] = '\0';
                }
                if (unlink(client_pipename) != 0 && errno != ENOENT) {
                    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", client_pipename,
                            strerror(errno));
                    exit(EXIT_FAILURE);
                }
                if (mkfifo(client_pipename, 0777) != 0) {
                    fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                tx = open(client_pipename, O_WRONLY);
                if (tx == -1) {
                    fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                ret = write(tx, &next_session_id, sizeof(int));
                if (ret == -1) {
                    fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                clients[next_session_id - 1].tx = tx;
                clients[next_session_id - 1].rx = rx;
                clients[next_session_id - 1].session_id = next_session_id;
                clients[next_session_id - 1].pipename = client_pipename;
                next_session_id++;
                // TODO - perhaps instead of exiting, just return -1
                break;
            case TFS_OP_CODE_UNMOUNT:
                // TODO - do we need to unlink the client pipe after it is closed? 
                read(rx, &session_id, sizeof(int));
                if (close(clients[session_id - 1].tx) != 0) {
                    fprintf(stderr, "[ERR]: close failed: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                if (close(clients[session_id - 1].rx) != 0) {
                    fprintf(stderr, "[ERR]: close failed: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                if (unlink(clients[session_id - 1].pipename) != 0 && errno != ENOENT) {
                    fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", clients[session_id - 1].pipename,
                            strerror(errno));
                    exit(EXIT_FAILURE);
                }
                break;

            case TFS_OP_CODE_OPEN:
                read(rx, &session_id, sizeof(int));
                ret = read(rx, filename, sizeof(char) * (BUFFER_SIZE - 1));
                for (ssize_t i = ret; i < BUFFER_SIZE - 1; i++) {
                    filename[i] = '\0';
                }
                read(rx, &flags, sizeof(int));
                int call_ret = tfs_open(filename, flags);
                write(clients[session_id - 1].tx, &call_ret, sizeof(int));
                break;

            case TFS_OP_CODE_CLOSE:
                read(rx, &session_id, sizeof(int));
                read(rx, &fhandle, sizeof(int));
                tfs_ret_int = tfs_close(fhandle);
                write(clients[session_id - 1].tx, &tfs_ret_int, sizeof(int));
                break;

            case TFS_OP_CODE_WRITE:
                read(rx, &session_id, sizeof(int));
                read(rx, &fhandle, sizeof(int));
                read(rx, &len, sizeof(size_t));
                buffer = malloc(sizeof(char) * len);
                if (buffer == NULL) {
                    fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                read(rx, buffer, sizeof(char)*len);
                tfs_ret_ssize_t = tfs_write(fhandle, buffer, len);
                write(clients[session_id - 1].tx, &tfs_ret_ssize_t, sizeof(ssize_t));
                free(buffer);
                break;

            case TFS_OP_CODE_READ:
                read(rx, &session_id, sizeof(int));
                read(rx, &fhandle, sizeof(int));
                read(rx, &len, sizeof(size_t));
                buffer = malloc(sizeof(char) * len);
                if (buffer == NULL) {
                    fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                tfs_ret_ssize_t = tfs_read(fhandle, buffer, len);
                write(clients[session_id - 1].tx, &tfs_ret_ssize_t, sizeof(ssize_t));
                free(buffer);
                break;

            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                read(rx, &session_id, sizeof(int));
                tfs_ret_int = tfs_destroy_after_all_closed();
                write(clients[session_id - 1].tx, &tfs_ret_int, sizeof(int));
                shutting_down = true;
                break;

            default: 
                fprintf(stderr, "[ERR]: Invalid op code: %d\n", op_code);
                break;
        }
    } while(!shutting_down);

    close(rx);
    unlink(pipename);
    return 0;
}