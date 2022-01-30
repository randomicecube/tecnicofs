#ifndef OPERATIONS_H
#define OPERATIONS_H

#include "common/common.h"
#include "config.h"
#include "state.h"
#include <sys/types.h>

/*
 * Initializes tecnicofs
 * Returns 0 if successful, -1 otherwise.
 */
int tfs_init();

/*
 * Destroy tecnicofs
 * Returns 0 if successful, -1 otherwise.
 */
int tfs_destroy();

/*
 * Waits until no file is open and then destroy tecnicofs
 * Returns 0 if successful, -1 otherwise.
 */
int tfs_destroy_after_all_closed();

/*
 * Looks for a file
 * Note: as a simplification, only a plain directory space (root directory only)
 * is supported Input:
 *  - name: absolute path name
 * Returns the inumber of the file, -1 if unsuccessful
 */
int tfs_lookup(char const *name);

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
 * Returns the number of bytes that were written (can be lower than
 * 'len' if the maximum file size is exceeded), or -1 in case of error
 */
ssize_t tfs_write(int fhandle, void const *buffer, size_t len);

/* Reads from an open file, starting at the current offset
 * Input:
 * 	- file handle (obtained from a previous call to tfs_open)
 * 	- destination buffer
 * 	- length of the buffer
 * Returns the number of bytes that were copied from the file to the buffer
 * (can be lower than 'len' if the file size was reached), or -1 in case of
 * error
 */
ssize_t tfs_read(int fhandle, void *buffer, size_t len);

/* Copies the contents of a file that exists in TecnicoFS to the contents
 * of another file in the OS' file system tree (outside TecnicoFS).
 * Input:
 *      - path name of the source file (from TecnicoFS)
 *      - path name of the destination file (in the main file system), which
 *        is created it needed, and overwritten if it already exists
 * Returns 0 if successful, -1 otherwise.
 */
int tfs_copy_to_external_fs(char const *source_path, char const *dest_path);

/*
 * Performs the bridge between server and client in the tfs_mount operation
 */
void case_mount(char *request);

/*
 * Performs the bridge between server and client in the tfs_unmount operation
 */
void case_unmount(char *request);

/*
 * Performs the bridge between server and client in the tfs_open operation
 */
void case_open(char *request);

/*
 * Performs the bridge between server and client in the tfs_close operation
 */
void case_close(char *request);

/*
 * Performs the bridge between server and client in the tfs_write operation
 */
void case_write(char *request);

/*
 * Performs the bridge between server and client in the tfs_read operation
 */
void case_read(char *request);

/*
 * Performs the bridge between server and client in the tfs_shutdown operation
 */
void case_shutdown(char *request);

#endif // OPERATIONS_H
