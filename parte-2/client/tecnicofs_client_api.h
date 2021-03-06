#ifndef CLIENT_API_H
#define CLIENT_API_H

#include "common/common.h"
#include <sys/types.h>

/*
  * Structure responsible for holding a given client's information.
  */
typedef struct Client {
    int rx;
    int tx;
    int session_id;
    char *pipename;
} Client;

/*
 * Sizes used for writing in the pipe that connects a client to the server
 */
#define MOUNT_SIZE_API (sizeof(char) + BUFFER_SIZE * sizeof(char))
#define UNMOUNT_SIZE_API (sizeof(char) + sizeof(int))
#define OPEN_SIZE_API (sizeof(char) + 2 * sizeof(int) + BUFFER_SIZE * sizeof(char))
#define CLOSE_SIZE_API (sizeof(char) + 2 * sizeof(int))
#define WRITE_SIZE_API(len) (sizeof(char) + 2 * sizeof(int) + sizeof(char) * len + sizeof(size_t))
#define READ_SIZE_API (sizeof(char) + 2 * sizeof(int) + sizeof(size_t))
#define SHUTDOWN_SIZE_API (sizeof(char) + sizeof(int))

/*
 * Establishes a session with a TecnicoFS server.
 * Input:
 * - client_pipe_path: pathname of a named pipe that will be used for
 *   the client to receive responses. This named pipe will be created (via
 * 	 mkfifo) inside tfs_mount.
 * - server_pipe_path: pathname of the named pipe where the server is listening
 *   for client requests
 * When successful, the new session's identifier (session_id) was
 * saved internally by the client; also, the client process has
 * successfully opened both named pipes (one for reading, the other one for
 * writing, respectively).
 *
 * Returns 0 if successful, -1 otherwise.
 */
int tfs_mount(char const *client_pipe_path, char const *server_pipe_path);

/*
 * Ends the currently active session.
 * After notifying the server, both named pipes are closed by the client,
 * the client named pipe is deleted (via unlink) and the client's session_id is
 * set to none.
 *
 * Returns 0 if successful, -1 otherwise.
 */
int tfs_unmount();

/*
 * Opens a file
 * Input:
 *  - name: absolute path name
 *  - flags: can be a combination (with bitwise or) of the following flags:
 *    - append mode (TFS_O_APPEND)
 *    - truncate file contents (TFS_O_TRUNC)
 *    - create file if it does not exist (TFS_O_CREAT)
 */
int tfs_open(char const *name, int flags);

/* Closes a file
 * Input:
 * 	- file handle (obtained from a previous call to tfs_open)
 * Returns 0 if successful, -1 otherwise.
 */
int tfs_close(int fhandle);

/* Writes to an open file, starting at the current offset
 * Input:
 * 	- file handle (obtained from a previous call to tfs_open)
 * 	- buffer containing the contents to write
 * 	- length of the contents (in bytes)
 *
 * Returns the number of bytes that were written (can be lower than
 * 'len' if the maximum file size is exceeded), or -1 in case of error.
 */
ssize_t tfs_write(int fhandle, void const *buffer, size_t len);

/* Reads from an open file, starting at the current offset
 * * Input:
 * 	- file handle (obtained from a previous call to tfs_open)
 * 	- destination buffer
 * 	- length of the buffer
 *
 * Returns the number of bytes that were copied from the file to the buffer
 * (can be lower than 'len' if the file size was reached), or -1 in case of
 * error.
 */
ssize_t tfs_read(int fhandle, void *buffer, size_t len);

/*
 * Orders TecnicoFS server to wait until no file is open and then shutdown
 * Returns 0 if successful, -1 otherwise.
 */
int tfs_shutdown_after_all_closed();

int write_buffer(int tx, char *buf, size_t to_write);

#endif /* CLIENT_API_H */
