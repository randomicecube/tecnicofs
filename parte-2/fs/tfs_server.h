#ifndef TFS_SERVER_H
#define TFS_SERVER_H

#include "common/common.h"
#include "state.h"
#include "config.h"
#include <sys/types.h>
#include <stdbool.h>

/*
 * Structure responsible for holding a given session's information.
  */
typedef struct Session{
    int session_id;
    bool is_active;
    pthread_mutex_t session_lock;
    pthread_cond_t session_flag;
    pthread_t session_t;
    char buffer[MAX_REQUEST_SIZE];
    int tx;
    char *pipename;
} Session;

#define MOUNT_SIZE_SERVER (BUFFER_SIZE * sizeof(char))
#define OPEN_SIZE_SERVER (sizeof(int) + BUFFER_SIZE * sizeof(char))
#define CLOSE_SIZE_SERVER (sizeof(int))
#define READ_SIZE_SERVER (sizeof(int) + sizeof(size_t))

/*
 * Performs the bridge between server and client in the tfs_mount operation
 */
void case_mount(Session *session);

/*
 * Performs the bridge between server and client in the tfs_unmount operation
 */
void case_unmount(Session *session);

/*
 * Performs the bridge between server and client in the tfs_open operation
 */
void case_open(Session *session);

/*
 * Performs the bridge between server and client in the tfs_close operation
 */
void case_close(Session *session);

/*
 * Performs the bridge between server and client in the tfs_write operation
 */
void case_write(Session *session);

/*
 * Performs the bridge between server and client in the tfs_read operation
 */
void case_read(Session *session);

/*
 * Performs the bridge between server and client in the tfs_shutdown operation
 */
void case_shutdown(Session *session);

/*
 * Starts all the available sessions in the server, initializing:
 * - each session's lock
 * - each session's cond_var
 */
void start_sessions();

/*
 * Handles the requests from the receptor thread to a worker thread
 */
void *thread_handler(void *arg);

/*
 * Helper function for reading from pipe in main.
 * Checks if it was able to read correctly from the pipe.
 * If it finds out that the pipe was closed, it tries to open it again.
 */
bool check_pipe_open(ssize_t ret, int rx, char *pipename);

/*
 * Helper function for handling the case where it's not possible for another
 * client to connect to the server.
 */
void handle_too_many_clients(Session *session);

#endif