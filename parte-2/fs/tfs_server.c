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
    int session_id;
    int fhandle;
    int flags;
    int tx;
    size_t len;
    ssize_t ret;
    char filename[BUFFER_SIZE];
    char client_pipename[BUFFER_SIZE];
    char *buffer;

    while (true) {
        ret = read(rx, &op_code, sizeof(char));
        if (ret == 0) {
            close(rx);
            rx = open(pipename, O_RDONLY);
            if (rx == -1) {
                fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
                exit(EXIT_FAILURE);
            }
        }

        // printf for debug purposed - TODO delete it
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
                // TODO - perhaps instead of exiting, just return -1
                // TODO - MISSING SAVING SESSION_ID
                // calls tfs_mount
                break;
            case TFS_OP_CODE_UNMOUNT:
                read(rx, &session_id, sizeof(int));
                // calls tfs_unmount
                break;

            case TFS_OP_CODE_OPEN:
                read(rx, &session_id, sizeof(int));
                ret = read(rx, filename, sizeof(char) * (BUFFER_SIZE - 1));
                for (ssize_t i = ret; i < BUFFER_SIZE - 1; i++) {
                    filename[i] = '\0';
                }
                read(rx, &flags, sizeof(int));
                tfs_open(filename, flags);
                // still missing session id stuff
                break;

            case TFS_OP_CODE_CLOSE:
                read(rx, &session_id, sizeof(int));
                read(rx, &fhandle, sizeof(int));
                tfs_close(fhandle);
                // still missing session id stuff
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
                tfs_write(fhandle, buffer, len);
                // still missing session id stuff
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
                tfs_read(fhandle, buffer, len);
                free(buffer);
                // still missing session id stuff
                break;

            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                read(rx, &session_id, sizeof(int));
                tfs_destroy_after_all_closed();
                // still missing session id stuff
                break;

            default: 
                fprintf(stderr, "[ERR]: Invalid op code: %d\n", op_code);
                break;
        }
    }

    close(rx);
    unlink(pipename);
    return 0;
}