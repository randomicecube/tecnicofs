#ifndef COMMON_H
#define COMMON_H

/* tfs_open flags */
enum {
    TFS_O_CREAT = 0b001,
    TFS_O_TRUNC = 0b010,
    TFS_O_APPEND = 0b100,
};

/* operation codes (for client-server requests) */
enum {
    TFS_OP_CODE_MOUNT = 1,
    TFS_OP_CODE_UNMOUNT = 2,
    TFS_OP_CODE_OPEN = 3,
    TFS_OP_CODE_CLOSE = 4,
    TFS_OP_CODE_WRITE = 5,
    TFS_OP_CODE_READ = 6,
    TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED = 7
};

/*
 * 2048 is used because
 * - we need to be able to read at least a block's worth (1024) for tfs_write
 * - we also need to account for the rest of the message's content
 * together, they won't exceed 2048
 * the number itself had to be above 1024, was used because it is the
 * next base 2 exponential number
 */
#define MAX_REQUEST_SIZE (2048)
/*
 * 64 is an arbitrary number, we chose it because of it being a
 * base 2 exponential number which is also pretty-ish
 */
#define MAX_CLIENTS (64)

#define BUFFER_SIZE (40)

#endif /* COMMON_H */