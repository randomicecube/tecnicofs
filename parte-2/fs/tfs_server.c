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
    size_t len;
    ssize_t ret;
    char client_pipename[BUFFER_SIZE];

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

        switch (op_code) {
            case TFS_OP_CODE_MOUNT:
                ret = read(rx, client_pipename, sizeof(char) * (BUFFER_SIZE - 1));
                for (ssize_t i = ret; i < BUFFER_SIZE - 1; i++) {
                    client_pipename[i] = '\0';
                }
                // calls tfs_mount
                break;
            case TFS_OP_CODE_UNMOUNT:
                read(rx, &session_id, sizeof(int));
                // calls tfs_unmount
                break;

            case TFS_OP_CODE_OPEN:
                read(rx, &session_id, sizeof(int));
                ret = read(rx, client_pipename, sizeof(char) * (BUFFER_SIZE - 1));
                for (ssize_t i = ret; i < BUFFER_SIZE - 1; i++) {
                    client_pipename[i] = '\0';
                }
                read(rx, &flags, sizeof(int));
                // calls tfs_open
                break;

            case TFS_OP_CODE_CLOSE:
                read(rx, &session_id, sizeof(int));
                read(rx, &fhandle, sizeof(int));
                // calls tfs_close
                break;

            case TFS_OP_CODE_WRITE:
                read(rx, &session_id, sizeof(int));
                read(rx, &fhandle, sizeof(int));
                read(rx, &len, sizeof(size_t));
                char *buffer = malloc(sizeof(char) * len);
                if (buffer == NULL) {
                    fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
                    exit(EXIT_FAILURE);
                }
                read(rx, buffer, sizeof(char)*len);
                // calls tfs_write
                free(buffer);
                break;

            case TFS_OP_CODE_READ:
                read(rx, &session_id, sizeof(int));
                read(rx, &fhandle, sizeof(int));
                read(rx, &len, sizeof(size_t));
                // calls tfs_read
                break;

            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                read(rx, &session_id, sizeof(int));
                // calls tfs_shutdown_after_all_closed
                break;

            default: break;
        }
    }

    close(rx);
    unlink(pipename);
    return 0;
}