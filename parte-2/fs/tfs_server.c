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

#define BUFFER_SIZE (128)

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
    char buffer[BUFFER_SIZE];

    while (true) {
        ssize_t ret = read(rx, op_code, sizeof(char));
        switch (op_code) {
            case TFS_OP_CODE_MOUNT:

                break;
            case TFS_OP_CODE_UNMOUNT:

                break;

            case TFS_OP_CODE_OPEN:

                break;

            case TFS_OP_CODE_CLOSE:

                break;

            case TFS_OP_CODE_WRITE:

                break;

            case TFS_OP_CODE_READ:

                break;

            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:

                break;

            default: break;
        }
    }

    close(rx);
    unlink(pipename);
    return 0;
}