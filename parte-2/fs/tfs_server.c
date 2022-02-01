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
pthread_mutex_t next_session_id_lock;
pthread_mutex_t sessions_lock;

// TODO - DEAL WITH LONG READS AND WRITES (WITH A FOR LOOP)

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
    signal(SIGPIPE, SIG_IGN); // TODO - is this required?

    ssize_t ret;
    int session_id;
    char op_code;
    Session *current_session;
    do {
        ret = read(rx, &op_code, sizeof(char));
        if (op_code == TFS_OP_CODE_MOUNT) printf("FOUND OP CODE MOUNT\n");
        if (ret == -1) {
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));;
            continue;
        }
        if (ret == 0) { // reached EOF (client closed pipe)
            close(rx);
            rx = open(pipename, O_RDONLY);
            if (rx == -1) {
                fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
                end_sessions();
                exit(EXIT_FAILURE);
            }
            continue;
        }
        if (ret == -1) { // TODO - ABSTRACTION IN THIS (READ FUNCTION)
            fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
            continue;
        }
        if (op_code == TFS_OP_CODE_MOUNT) {
            printf("Received mount request.\n");
            lock_mutex(&next_session_id_lock);
            if (next_session_id > 64) {
                fprintf(stderr, "[ERR]: Too many clients connected. Try again in a bit.\n");
                unlock_mutex(&next_session_id_lock);
                continue;
            }
            session_id = next_session_id++;
            current_session = &sessions[session_id - 1];
            current_session->session_id = session_id;
            unlock_mutex(&next_session_id_lock);
            lock_mutex(&current_session->session_lock);
            memcpy(current_session->buffer, &op_code, sizeof(char));
            ret = read(rx, current_session->buffer + 1, MOUNT_SIZE_SERVER);
            printf("Ending mount request.\n");
            printf("Session id now is %d\n", session_id);
        } else {
            ret = read(rx, &session_id, sizeof(int));
            if (ret == -1) {
                fprintf(stderr, "[ERR]: read failed: %s\n", strerror(errno));
                continue;
            }
            if (session_id < 1 || session_id > MAX_CLIENTS) {
                fprintf(stderr, "[ERR]: invalid session id: %d\n", session_id);
                continue;
            }
            current_session = &sessions[session_id - 1];
            lock_mutex(&current_session->session_lock);
            memcpy(current_session->buffer, &op_code, sizeof(char));
            memcpy(current_session->buffer + 1, &session_id, sizeof(int));
            switch (op_code) {
                case TFS_OP_CODE_UNMOUNT:
                case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                    // cases not required - we have already read the session id
                    break;
                case TFS_OP_CODE_OPEN:
                    ret = read(rx, current_session->buffer + 1 + sizeof(int), OPEN_SIZE_SERVER);
                    break;
                case TFS_OP_CODE_CLOSE:
                    ret = read(rx, current_session->buffer + 1 + sizeof(int), CLOSE_SIZE_SERVER);
                    break;
                case TFS_OP_CODE_WRITE:
                    size_t len;
                    // TODO VERIFY SYSCALLS
                    read(rx, current_session->buffer + 1 + sizeof(int), sizeof(int)); // fhandle
                    read(rx, &len, sizeof(size_t));
                    memcpy(current_session->buffer + 1 + 2 * sizeof(int), &len, sizeof(size_t));
                    read(rx, current_session->buffer + 1 + 2 * sizeof(int) + sizeof(size_t), sizeof(char) * len);
                    break;
                case TFS_OP_CODE_READ:
                    ret = read(rx, current_session->buffer + 1  + sizeof(int), READ_SIZE_SERVER);
                    break;
                default:
                    fprintf(stderr, "[ERR]: Invalid op_code: %d\n", op_code);
                    continue;
            }
        }
        current_session->is_active = true;
        pthread_cond_signal(&current_session->session_flag);
        unlock_mutex(&current_session->session_lock);
    } while(!shutting_down);

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

void case_mount(Session *session) {
    char client_pipename[BUFFER_SIZE];
    int tx;
    memcpy(client_pipename, session->buffer + 1, sizeof(char) * BUFFER_SIZE);
    tx = open(client_pipename, O_WRONLY);
    if (tx == -1) {
        fprintf(stderr, "[ERR]: open failed: %s\n", strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
    if (write(tx, &session->session_id, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
    session->tx = tx;
    session->pipename = client_pipename;
}

void case_unmount(Session *session) {
    if (close(session->tx) != 0) {
        fprintf(stderr, "[ERR]: close failed: %s\n", strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
    if (unlink(session->pipename) != 0 && errno != ENOENT) { // TODO - REMOVE? UNLINK IN CLIENT
        fprintf(stderr, "[ERR]: unlink(%s) failed: %s\n", session->pipename,
                strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
}

void case_open(Session *session) {
    int flags;
    char filename[BUFFER_SIZE];
    memcpy(&flags, session->buffer + 1 + sizeof(int), sizeof(int));
    memcpy(filename, session->buffer + 1 + 2 * sizeof(int), sizeof(char) * BUFFER_SIZE);
    int call_ret = tfs_open(filename, flags);
    printf("session id is %d\n", session->session_id);
    printf("flags is %d\n", flags);
    printf("filename is %s\n", filename);
    int tx = session->tx;
    printf("Tx is %d\n", tx);
    if (write(tx, &call_ret, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
}

void case_close(Session *session) {
    int fhandle;
    memcpy(&fhandle, session->buffer + 1 + sizeof(int), sizeof(int));
    int tfs_ret_int = tfs_close(fhandle);
    if (write(session->tx, &tfs_ret_int, sizeof(int)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
}

void case_write(Session *session) {
    int fhandle;
    size_t len;
    char *buffer;
    ssize_t tfs_ret_ssize_t;
    memcpy(&fhandle, session->buffer + 1 + sizeof(int), sizeof(int));
    memcpy(&len, session->buffer + 1 + 2 * sizeof(int), sizeof(size_t));
    buffer = malloc(sizeof(char) * len);
    if (buffer == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
    memcpy(buffer, session->buffer + 1 + 2 * sizeof(int) + sizeof(size_t), sizeof(char) * len);
    tfs_ret_ssize_t = tfs_write(fhandle, buffer, len);
    if (write(session->tx, &tfs_ret_ssize_t, sizeof(ssize_t)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        free(buffer);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    free(buffer);
}

void case_read(Session *session) {
    int fhandle;
    size_t len;
    char *buffer;
    ssize_t tfs_ret_ssize_t;
    memcpy(&fhandle, session->buffer + 1 + sizeof(int), sizeof(int));
    memcpy(&len, session->buffer + 1 + 2 * sizeof(int), sizeof(size_t));
    buffer = malloc(sizeof(char) * len);
    if (buffer == NULL) {
        fprintf(stderr, "[ERR]: malloc failed: %s\n", strerror(errno));
        end_sessions();
        exit(EXIT_FAILURE);
    }
    tfs_ret_ssize_t = tfs_read(fhandle, buffer, len);
    if (write(session->tx, &tfs_ret_ssize_t, sizeof(ssize_t)) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        free(buffer);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    if (write(session->tx, buffer, (size_t) tfs_ret_ssize_t) == -1) {
        fprintf(stderr, "[ERR]: write failed: %s\n", strerror(errno));
        free(buffer);
        end_sessions();
        exit(EXIT_FAILURE);
    }
    free(buffer);
}

void case_shutdown(Session *session) {
    int tfs_ret_int;
    tfs_ret_int = tfs_destroy_after_all_closed();
    if (write(session->tx, &tfs_ret_int, sizeof(int)) == -1) {
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
    init_mutex(&next_session_id_lock);
    init_mutex(&sessions_lock);
    for (int i = 0; i < MAX_CLIENTS; i++) {
        sessions[i].session_id = i + 1;
        sessions[i].is_active = false;
        sessions[i].buffer = malloc(sizeof(char) * MAX_REQUEST_SIZE);
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
    destroy_mutex(&next_session_id_lock);
    destroy_mutex(&sessions_lock);
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
                printf("[INFO]: Entering MOUNT case.\n");
                case_mount(session);
                printf("[INFO]: Exiting MOUNT case.\n");
                break;
            case TFS_OP_CODE_UNMOUNT:
                printf("[INFO]: Entering UNMOUNT case.\n");
                case_unmount(session);
                printf("[INFO]: Exiting UNMOUNT case.\n");
                break;
            case TFS_OP_CODE_OPEN:
                printf("[INFO]: Entering OPEN case.\n");
                case_open(session);
                printf("[INFO]: Exiting OPEN case.\n");
                break;
            case TFS_OP_CODE_CLOSE:
                printf("[INFO]: Entering CLOSE case.\n");
                case_close(session);
                printf("[INFO]: Exiting CLOSE case.\n");
                break;
            case TFS_OP_CODE_WRITE:
                printf("[INFO]: Entering WRITE case.\n");
                case_write(session);
                printf("[INFO]: Exiting WRITE case.\n");
                break;
            case TFS_OP_CODE_READ:
                printf("[INFO]: Entering READ case.\n");
                case_read(session);
                printf("[INFO]: Exiting READ case.\n");
                break;
            case TFS_OP_CODE_SHUTDOWN_AFTER_ALL_CLOSED:
                printf("[INFO]: Entering SHUTDOWN_AFTER_ALL_CLOSED case.\n");
                case_shutdown(session);
                printf("[INFO]: Exiting SHUTDOWN_AFTER_ALL_CLOSED case.\n");
                shutting_down = true;
                break;
            default:
                fprintf(stderr, "[ERR]: invalid request: %c\n", op_code);
        }
        session->is_active = false;
        strcpy(session->buffer, ""); // clearing the buffer after each request
        unlock_mutex(&session->session_lock);
    }
}