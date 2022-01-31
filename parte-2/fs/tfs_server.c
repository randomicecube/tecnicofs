#include "operations.h"
#include "tfs_server.h"
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
#include <pthread.h>

int next_session_id = 1;
Session sessions[MAX_CLIENTS];
bool shutting_down = false;

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

    start_sessions();

    ssize_t ret;
    char op_code;
    char *client_request = malloc(MAX_REQUEST_SIZE);
    if (client_request == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
    do {
        printf("Read request from client...\n");
        ret = read(rx, &op_code, sizeof(char));
        printf("Just read op_code from client.\n");
        if (ret == -1) {
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            free(client_request);
            end_sessions();
            exit(EXIT_FAILURE);
        }
        if (ret == 0) { // reached EOF (client closed pipe)
            printf("Client closed pipe.\n");
            close(rx);
            rx = open(pipename, O_RDONLY);
            if (rx == -1) {
                fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
                free(client_request);
                end_sessions();
                exit(EXIT_FAILURE);
            }
            continue;
        }
        printf("Request received.\n");
        client_request[0] = op_code;
        switch (op_code) {
            case TFS_OP_CODE_MOUNT:
                printf("Mounting...\n");
                ret = read(rx, client_request + 1, MOUNT_SIZE_SERVER);
                printf("Just read mount request from client.\n");
                break;
            case TFS_OP_CODE_UNMOUNT:
                printf("Unmounting...\n");
                ret = read(rx, client_request + 1, UNMOUNT_SIZE_SERVER);
                printf("Just read unmount request from client.\n");
                break;
            case TFS_OP_CODE_OPEN:
                printf("Opening...\n");
                ret = read(rx, client_request + 1, OPEN_SIZE_SERVER);
                printf("Just read open request from client.\n");
                break;
            case TFS_OP_CODE_CLOSE:
                printf("Closing...\n");
                ret = read(rx, client_request + 1, CLOSE_SIZE_SERVER);
                printf("Just read close request from client.\n");
                break;
            case TFS_OP_CODE_WRITE:
                printf("Writing...\n");
                size_t len;
                // TODO VERIFY SYSCALLS
                read(rx, client_request + 1, sizeof(int)); // session id
                read(rx, client_request + 1 + sizeof(int), sizeof(int)); // fhandle
                read(rx, &len, sizeof(size_t));
                memcpy(client_request + 1 + 2 * sizeof(int), &len, sizeof(size_t));
                read(rx, client_request + 1 + 2 * sizeof(int) + sizeof(size_t), sizeof(char) * len);
                printf("Just read write request from client.\n");
                break;
            case TFS_OP_CODE_READ:
                printf("Reading...\n");
                ret = read(rx, client_request + 1, READ_SIZE_SERVER);
                printf("Just read read request from client.\n");
                break;
            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                printf("Shutting down...\n");
                ret = read(rx, client_request + 1, SHUTDOWN_SIZE_SERVER);
                printf("Just read shutdown request from client.\n");
                break;
            default:
                fprintf(stderr, "[ERR]: Invalid op_code: %d\n", op_code);
                continue;
        }
        if (op_code == TFS_OP_CODE_MOUNT) {
            printf("Entered mount if...\n");
            lock_mutex(&sessions[next_session_id - 1].session_lock);
            sessions[next_session_id - 1].buffer = client_request;
            sessions[next_session_id - 1].is_active = true;
            pthread_cond_signal(&sessions[next_session_id - 1].session_flag);
            unlock_mutex(&sessions[next_session_id - 1].session_lock);
            printf("Exited mount if...\n");
        } else {
            printf("Entered non-mount if...\n");
            int session_id;
            memcpy(&session_id, client_request + 1, sizeof(int));
            printf("Session id is %d\n", session_id);
            if (session_id < 1 || session_id > MAX_CLIENTS) {
                fprintf(stderr, "[ERR]: invalid session id: %d\n", session_id);
                continue;
            }
            printf("Before locking session...\n");
            lock_mutex(&sessions[session_id - 1].session_lock);
            sessions[session_id - 1].buffer = client_request;
            sessions[session_id - 1].is_active = true;
            printf("Before signaling session...\n");
            pthread_cond_signal(&sessions[session_id - 1].session_flag);
            printf("Before unlocking session...\n");
            unlock_mutex(&sessions[session_id - 1].session_lock);
            printf("Exited non-mount if...\n");
        }
    } while(!shutting_down);

    printf("Left loop.\n");

    free(client_request);
    close(rx);
    unlink(pipename);
    end_sessions();
    return 0;
}

/*
 * ----------------------------------------------------------------------------
 * Below are the functions that implement each operation (server-side).
 * ----------------------------------------------------------------------------
 */

void case_mount(char *request) {
    char client_pipename[BUFFER_SIZE];
    int tx;
    memcpy(client_pipename, request + 1, sizeof(char) * BUFFER_SIZE);
    tx = open(client_pipename, O_WRONLY);
    if (tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
    if (write(tx, &next_session_id, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
    sessions[next_session_id - 1].tx = tx;
    sessions[next_session_id - 1].session_id = next_session_id;
    sessions[next_session_id - 1].pipename = client_pipename;
    next_session_id++;    
}

void case_unmount(char *request) {
    int session_id;
    memcpy(&session_id, request + 1, sizeof(int));
    if (close(sessions[session_id - 1].tx) != 0) {
        fprintf(stderr, "[ERR]: close failed: %s\n", strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
    if (unlink(sessions[session_id - 1].pipename) != 0 && errno != ENOENT) {
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", sessions[session_id - 1].pipename,
                strerror(errno));
        end_sessions();
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
    if (write(sessions[session_id - 1].tx, &call_ret, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
}

void case_close(char *request) {
    int session_id, fhandle;
    memcpy(&session_id, request + 1, sizeof(int));
    memcpy(&fhandle, request + 1 + sizeof(int), sizeof(int));
    int tfs_ret_int = tfs_close(fhandle);
    if (write(sessions[session_id - 1].tx, &tfs_ret_int, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        end_sessions();
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
        end_sessions();
        exit(EXIT_FAILURE);
    }
    memcpy(buffer, request + 1 + 2 * sizeof(int) + sizeof(size_t), sizeof(char) * len);
    tfs_ret_ssize_t = tfs_write(fhandle, buffer, len);
    if (write(sessions[session_id - 1].tx, &tfs_ret_ssize_t, sizeof(ssize_t)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        free(buffer);
        end_sessions();
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
        end_sessions();
        exit(EXIT_FAILURE);
    }
    tfs_ret_ssize_t = tfs_read(fhandle, buffer, len);
    if (write(sessions[session_id - 1].tx, &tfs_ret_ssize_t, sizeof(ssize_t)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        free(buffer);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    if (write(sessions[session_id - 1].tx, buffer, (size_t) tfs_ret_ssize_t) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        free(buffer);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    free(buffer);
}

void case_shutdown(char *request) {
    int session_id, tfs_ret_int;
    memcpy(&session_id, request + 1, sizeof(int));
    tfs_ret_int = tfs_destroy_after_all_closed();
    if (write(sessions[session_id - 1].tx, &tfs_ret_int, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
}

/*
 * ----------------------------------------------------------------------------
 * Below are the session initialization/destruction functions.
 * ----------------------------------------------------------------------------
 */

void start_sessions() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        sessions[i].session_id = i + 1;
        sessions[i].is_active = false;
        init_mutex(&sessions[i].session_lock);
        if (pthread_cond_init(&sessions[i].session_flag, NULL) != 0) {
            fprintf(stderr, "[ERR]: cond init failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
        if (pthread_create(&sessions[i].session_t, NULL, thread_handler, (void *) &sessions[i]) != 0) {
            fprintf(stderr, "[ERR]: thread create failed: %s\n", strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
}

void end_sessions() {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        pthread_mutex_destroy(&sessions[i].session_lock);
        pthread_cond_destroy(&sessions[i].session_flag);
    }
}

/*
 * ----------------------------------------------------------------------------
 * Below is the thread handler function.
 * ----------------------------------------------------------------------------
 */

void *thread_handler(void *arg) {
    Session *session = (Session *) arg;
    char op_code;
    while (true) {
        lock_mutex(&session->session_lock);
        while (session->is_active == false) {
            pthread_cond_wait(&session->session_flag, &session->session_lock);
        }
        session->is_active = true;
        memcpy(&op_code, session->buffer, sizeof(char));
        switch (op_code) {
            case TFS_OP_CODE_MOUNT:
                printf("[INFO]: Entering case TFS_OP_CODE_MOUNT\n");
                case_mount(session->buffer);
                printf("Mounted successfully\n");
                break;
            case TFS_OP_CODE_UNMOUNT:
                printf("[INFO]: Entering case TFS_OP_CODE_UNMOUNT\n");
                case_unmount(session->buffer);
                printf("Unmounted successfully\n");
                break;
            case TFS_OP_CODE_OPEN:
                printf("[INFO]: Entering case TFS_OP_CODE_OPEN\n");
                case_open(session->buffer);
                printf("Opened successfully\n");
                break;
            case TFS_OP_CODE_CLOSE:
                printf("[INFO]: Entering case TFS_OP_CODE_CLOSE\n");
                case_close(session->buffer);
                printf("Closed successfully\n");
                break;
            case TFS_OP_CODE_WRITE:
                printf("[INFO]: Entering case TFS_OP_CODE_WRITE\n");
                case_write(session->buffer);
                printf("Wrote successfully\n");
                break;
            case TFS_OP_CODE_READ:
                printf("[INFO]: Entering case TFS_OP_CODE_READ\n");
                case_read(session->buffer);
                printf("Read successfully\n");
                break;
            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                printf("[INFO]: Entering case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED\n");
                case_shutdown(session->buffer);
                printf("Shutdown successfully\n");
                shutting_down = true;
                break;
            default:
                fprintf(stderr, "[ERR]: invalid request: %c\n", op_code);
        }
        session->is_active = false;
        unlock_mutex(&session->session_lock);
    }
}