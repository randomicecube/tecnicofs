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

int next_session_id = 1;
Client clients[MAX_CLIENTS];

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Please specify the pathname of the server's pipe.\n");
        return 1;
    }

    tfs_init();

    char *pipename = argv[1];
    printf("Starting TecnicoFS server with pipe called %s\n", pipename);

    // unlink pipe
    if (unlink(pipename) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", pipename,
                strerror(errno));
        exit(EXIT_FAILURE);
    }

    // create pipe
    if (mkfifo(pipename, 0640) != 0) {
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
    char *client_request = malloc(MAX_REQUEST_SIZE); // TODO FREE
    if (client_request == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    ssize_t ret;
    bool shutting_down = false;
    do {
        ret = read(rx, client_request, sizeof(char) * MAX_REQUEST_SIZE);
        memcpy(&op_code, client_request, sizeof(char));
        if (ret == -1) {
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            free(client_request);
            exit(EXIT_FAILURE);
        }
        if (ret == 0) {
            close(rx);
            rx = open(pipename, O_RDONLY);
            if (rx == -1) {
                fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
                free(client_request);
                exit(EXIT_FAILURE);
            }
        }

        switch (op_code) {
            // TODO - HANDLE CASE WHERE NEXT SESSION ID IS OVER 64
            // TODO - HANDLE CASE WHERE IN WRITE THE LENGTH IS OVER MAX_REQUEST_SIZE
            case TFS_OP_CODE_MOUNT:
                printf("[INFO]: Received TFS_OP_CODE_MOUNT\n");
                case_mount(client_request);
                printf("[INFO]: Mounted\n");
                break;
            case TFS_OP_CODE_UNMOUNT:
                printf("[INFO]: Received TFS_OP_CODE_UNMOUNT\n");
                case_unmount(client_request);
                printf("[INFO]: Unmounted\n");
                break;
            case TFS_OP_CODE_OPEN:
                printf("[INFO]: Received TFS_OP_CODE_OPEN\n");
                case_open(client_request);
                printf("[INFO]: Opened\n");
                break;
            case TFS_OP_CODE_CLOSE:
                printf("[INFO]: Received TFS_OP_CODE_CLOSE\n");
                case_close(client_request);
                printf("[INFO]: Closed\n");
                break;
            case TFS_OP_CODE_WRITE:
                printf("[INFO]: Received TFS_OP_CODE_WRITE\n");
                case_write(client_request);
                printf("[INFO]: Wrote\n");
                break;
            case TFS_OP_CODE_READ:
                printf("[INFO]: Received TFS_OP_CODE_READ\n");
                case_read(client_request);
                printf("[INFO]: Read\n");
                break;
            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                printf("[INFO]: Received TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED\n");
                case_shutdown(client_request);
                printf("[INFO]: Shut down\n");
                shutting_down = true;
                break;
            default: 
                fprintf(stderr, "[ERR]: Invalid op code: %d\n", op_code);
                break;
        }
    } while(!shutting_down);

    free(client_request);
    close(rx);
    unlink(pipename);
    return 0;
}

/*
 * Below are the functions that implement each operation (server-side).
 */

void case_mount(char *request) {
    char client_pipename[BUFFER_SIZE];
    int tx;
    memcpy(client_pipename, request + 1, sizeof(char) * BUFFER_SIZE);
    tx = open(client_pipename, O_WRONLY);
    if (tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (write(tx, &next_session_id, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    clients[next_session_id - 1].tx = tx;
    clients[next_session_id - 1].session_id = next_session_id;
    clients[next_session_id - 1].pipename = client_pipename;
    next_session_id++;
}

void case_unmount(char *request) {
    // TODO - do we need to unlink the client pipe after it is closed? 
    int session_id;
    memcpy(&session_id, request + 1, sizeof(int));
    if (close(clients[session_id - 1].tx) != 0) {
        fprintf(stderr, "[ERR]: close failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (unlink(clients[session_id - 1].pipename) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", clients[session_id - 1].pipename,
                strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void case_open(char *request) {
    int session_id, flags;
    char filename[BUFFER_SIZE];
    memcpy(&session_id, request + 1, sizeof(int));
    memcpy(&flags, request + 1 + sizeof(int), sizeof(int));
    memcpy(filename, request + 1 + 2 * sizeof(int), sizeof(char) * BUFFER_SIZE);
    int call_ret = tfs_open(filename, flags);
    if (write(clients[session_id - 1].tx, &call_ret, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void case_close(char *request) {
    int session_id, fhandle;
    memcpy(&session_id, request + 1, sizeof(int));
    memcpy(&fhandle, request + 1 + sizeof(int), sizeof(int));
    int tfs_ret_int = tfs_close(fhandle);
    if (write(clients[session_id - 1].tx, &tfs_ret_int, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}

void case_write(char *request) {
    int session_id, fhandle;
    size_t len;
    char *buffer;
    ssize_t tfs_ret_ssize_t;
    memcpy(&session_id, request + 1, sizeof(int));
    memcpy(&fhandle, request + 1 + sizeof(int), sizeof(int));
    memcpy(&len, request + 1 + 2 * sizeof(int), sizeof(size_t));
    buffer = malloc(sizeof(char) * len);
    if (buffer == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    memcpy(buffer, request + 1 + 2 * sizeof(int) + sizeof(size_t), sizeof(char) * len);
    tfs_ret_ssize_t = tfs_write(fhandle, buffer, len);
    if (write(clients[session_id - 1].tx, &tfs_ret_ssize_t, sizeof(ssize_t)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        free(buffer);
        exit(EXIT_FAILURE);
    }
    free(buffer);
}

void case_read(char *request) {
    int session_id, fhandle;
    size_t len;
    char *buffer;
    ssize_t tfs_ret_ssize_t;
    memcpy(&session_id, request + 1, sizeof(int));
    memcpy(&fhandle, request + 1 + sizeof(int), sizeof(int));
    memcpy(&len, request + 1 + 2 * sizeof(int), sizeof(size_t));
    buffer = malloc(sizeof(char) * len);
    if (buffer == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
    tfs_ret_ssize_t = tfs_read(fhandle, buffer, len);
    if (write(clients[session_id - 1].tx, &tfs_ret_ssize_t, sizeof(ssize_t)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        free(buffer);
        exit(EXIT_FAILURE);
    }
    if (write(clients[session_id - 1].tx, buffer, (size_t) tfs_ret_ssize_t) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        free(buffer);
        exit(EXIT_FAILURE);
    }
    free(buffer);
}

void case_shutdown(char *request) {
    int session_id, tfs_ret_int;
    memcpy(&session_id, request + 1, sizeof(int));
    tfs_ret_int = tfs_destroy_after_all_closed();
    if (write(clients[session_id - 1].tx, &tfs_ret_int, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        exit(EXIT_FAILURE);
    }
}