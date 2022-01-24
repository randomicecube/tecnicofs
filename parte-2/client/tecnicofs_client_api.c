#include "tecnicofs_client_api.h"
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

int tfs_mount(char const *client_pipe_path, char const *server_pipe_path) {
    if (unlink(client_pipe_path) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", client_pipe_path,
                strerror(errno));
        return -1;
    }
    if (mkfifo(client_pipe_path, 0777) != 0) {
        fprintf(stderr, "[ERR]: mkfifo failed: %s\n", strerror(errno));
        return -1;
    }
    int rx = open(client_pipe_path, O_RDONLY);
    if (rx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        return -1;
    }
    int tx = open(server_pipe_path, O_WRONLY);
    if (tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        return -1;
    }
    ssize_t ret;
    char op_code = TFS_OP_CODE_MOUNT;
    ret = write(tx, &op_code, sizeof(char));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    ret = write(tx, client_pipe_path, sizeof(char) * (strlen(client_pipe_path) + 1));
    if (ret == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        return -1;
    }
    return 0;
}

int tfs_unmount() {
    /* TODO: Implement this */
    return -1;
}

int tfs_open(char const *name, int flags) {
    /* TODO: Implement this */
    return -1;
}

int tfs_close(int fhandle) {
    /* TODO: Implement this */
    return -1;
}

ssize_t tfs_write(int fhandle, void const *buffer, size_t len) {
    /* TODO: Implement this */
    return -1;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    /* TODO: Implement this */
    return -1;
}

int tfs_shutdown_after_all_closed() {
    /* TODO: Implement this */
    return -1;
}
