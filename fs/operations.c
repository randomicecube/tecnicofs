#include "operations.h"
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int tfs_init() {
    state_init();

    /* create root inode */
    int root = inode_create(T_DIRECTORY);
    if (root != ROOT_DIR_INUM) {
        return -1;
    }

    return 0;
}

int tfs_destroy() {
    state_destroy();
    return 0;
}

static bool valid_pathname(char const *name) {
    return name != NULL && strlen(name) > 1 && name[0] == '/';
}


int tfs_lookup(char const *name) {
    if (!valid_pathname(name)) {
        return -1;
    }

    // skip the initial '/' character
    name++;

    return find_in_dir(ROOT_DIR_INUM, name);
}

int tfs_open(char const *name, int flags) {
    int inum;
    size_t offset;


    /* Checks if the path name is valid */
    if (!valid_pathname(name)) {
        return -1;
    }

    inum = tfs_lookup(name);
    if (inum >= 0) {
        /* The file already exists */
        inode_t *inode = inode_get(inum);
        if (inode == NULL) {
            return -1;
        }

        /* Trucate (if requested) */
        if (flags & TFS_O_TRUNC) {
            lock_mutex(&inode->i_lock);
            if (inode->i_size > 0) {
                for (int i = 0; i < MAX_DIRECT_BLOCKS; i++) {
                    if (data_block_free(inode->i_data_block[i]) == -1) {
                        unlock_mutex(&inode->i_lock);
                        return -1;
                    }
                }
                if (data_block_free(inode->i_indirect_data_block) == -1) {
                    unlock_mutex(&inode->i_lock);
                    return -1;
                }
                inode->i_size = 0;
            }
            unlock_mutex(&inode->i_lock);
        }
        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {
            lock_mutex(&inode->i_lock);
            offset = inode->i_size;
            unlock_mutex(&inode->i_lock);
        } else {
            offset = 0;
        }
    } else if (flags & TFS_O_CREAT) {
        /* The file doesn't exist; the flags specify that it should be created */
        inum = inode_create(T_FILE);
        if (inum == -1) {
            return -1;
        }
        /* Add entry in the root directory */
        if (add_dir_entry(ROOT_DIR_INUM, inum, name + 1) == -1) {
            inode_delete(inum);
            return -1;
        }
        offset = 0;
    } else {
        return -1;
    }


    /* Finally, add entry to the open file table and
     * return the corresponding handle */
    return add_to_open_file_table(inum, offset);

    /* Note: for simplification, if file was created with TFS_O_CREAT and there
     * is an error adding an entry to the open file table, the file is not
     * opened but it remains created */
}


int tfs_close(int fhandle) { return remove_from_open_file_table(fhandle); }

ssize_t tfs_write(int fhandle, void const *buffer, size_t to_write) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    lock_mutex(&file->file_lock);
    inode_t *inode = inode_get(file->of_inumber);
    unlock_mutex(&file->file_lock);
    if (inode == NULL) {
        return -1;
    }

    size_t bytes_written = 0;

    if (to_write > 0) {
        lock_mutex(&inode->i_lock);
        lock_mutex(&file->file_lock);
        size_t previously_written_blocks = file->of_offset / BLOCK_SIZE; //is it offset or isize?
        size_t end_write_blocks = (file->of_offset + to_write) / BLOCK_SIZE;    
        size_t current_write_size = BLOCK_SIZE;
        size_t buffer_offset = 0;
        if (to_write % BLOCK_SIZE > 0) {
            end_write_blocks++;
        }

        for (size_t i = previously_written_blocks; i < previously_written_blocks + end_write_blocks; i++, to_write -= BLOCK_SIZE) {
            current_write_size = (to_write > BLOCK_SIZE ? BLOCK_SIZE : to_write);
            void *block;
            if (i < MAX_DIRECT_BLOCKS) {
                if (inode->i_data_block[i] == -1) {
                    inode->i_data_block[i] = data_block_alloc();
                }
                block = data_block_get(inode->i_data_block[i]);
                if (block == NULL) {
                    unlock_mutex(&inode->i_lock);
                    unlock_mutex(&file->file_lock);
                    return -1;
                }
            } else {
                if (inode->i_indirect_data_block == -1) {
                    inode->i_indirect_data_block = data_block_alloc();
                    int *indirect_block = data_block_get(inode->i_indirect_data_block);
                    if (indirect_block == NULL) {
                        unlock_mutex(&inode->i_lock);
                        unlock_mutex(&file->file_lock);
                        return -1;
                    }
                    for (int j = MAX_DIRECT_BLOCKS; j < previously_written_blocks + end_write_blocks; j++) {
                        indirect_block[j-MAX_DIRECT_BLOCKS] = -1; // they start at -1, allocated if needed
                    }
                }

                int *indirect_block = (int *) data_block_get(inode->i_indirect_data_block);
                if (indirect_block == NULL) {
                    unlock_mutex(&inode->i_lock);
                    unlock_mutex(&file->file_lock);
                    return -1;
                }

                if (indirect_block[i-MAX_DIRECT_BLOCKS] == -1) {
                    indirect_block[i-MAX_DIRECT_BLOCKS] = data_block_alloc();
                    if (indirect_block[i-MAX_DIRECT_BLOCKS] == -1) {
                        unlock_mutex(&inode->i_lock);
                        unlock_mutex(&file->file_lock);
                        return -1;
                    }
                }
                block = data_block_get(indirect_block[i-MAX_DIRECT_BLOCKS]);
                if (block == NULL) {
                    unlock_mutex(&inode->i_lock);
                    unlock_mutex(&file->file_lock);
                    return -1;
                }
            }
            memcpy(block + file->of_offset % BLOCK_SIZE, buffer + buffer_offset, current_write_size);
            bytes_written += current_write_size;
            buffer_offset += current_write_size;
            file->of_offset += current_write_size;
            if (file->of_offset > inode->i_size) {
                inode->i_size = file->of_offset;
            }
            if ((int) to_write - BLOCK_SIZE <= 0) {
                break;
            }
        }
    }

    unlock_mutex(&inode->i_lock);
    unlock_mutex(&file->file_lock);
    return (ssize_t) bytes_written;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    /* From the open file table entry, we get the inode */
    inode_t *inode = inode_get(file->of_inumber);
    if (inode == NULL) {
        return -1;
    }

    /* Determine how many bytes to read */
    lock_mutex(&inode->i_lock);
    lock_mutex(&file->file_lock);
    size_t to_read = inode->i_size - file->of_offset;
    unlock_mutex(&inode->i_lock);
    unlock_mutex(&file->file_lock);
    if (to_read > len) {
        to_read = len;
    }

    if (to_read <= 0) {
        return 0;
    }

    size_t bytes_read = 0;

    lock_mutex(&inode->i_lock);
    lock_mutex(&file->file_lock);
    size_t previously_read_blocks = file->of_offset / BLOCK_SIZE; //is it offset or isize?
    size_t end_read_blocks = (file->of_offset + to_read) / BLOCK_SIZE;
    size_t current_read_size = BLOCK_SIZE;
    size_t buffer_offset = 0;
    if (to_read % BLOCK_SIZE > 0) {
        end_read_blocks++;
    }

    for (size_t i = previously_read_blocks; i < end_read_blocks; i++, to_read -= BLOCK_SIZE) {
        current_read_size = (to_read > BLOCK_SIZE) ? BLOCK_SIZE : to_read;
        void *block;
        if (i < MAX_DIRECT_BLOCKS) {
            block = data_block_get(inode->i_data_block[i]);
            if (block == NULL) {
                unlock_mutex(&inode->i_lock);
                unlock_mutex(&file->file_lock);
                return -1;
            }
        } else {
            int *indirect_block = (int *) data_block_get(inode->i_indirect_data_block);
            if (indirect_block == NULL) {
                unlock_mutex(&inode->i_lock);
                unlock_mutex(&file->file_lock);
                return -1;
            }
            block = data_block_get(indirect_block[i-MAX_DIRECT_BLOCKS]);
            if (block == NULL) {
                unlock_mutex(&inode->i_lock);
                unlock_mutex(&file->file_lock);
                return -1;
            }
        }
        memcpy(buffer + buffer_offset, block + file->of_offset % BLOCK_SIZE, current_read_size);
        bytes_read += current_read_size;
        file->of_offset += current_read_size;
        buffer_offset += current_read_size;
        if ((int) to_read - BLOCK_SIZE <= 0) {
            break;
        }
    }

    unlock_mutex(&inode->i_lock);
    unlock_mutex(&file->file_lock);
    return (ssize_t) bytes_read;
}

int tfs_copy_to_external_fs(char const *source_path, char const *dest_path) {
    int source_handle = tfs_open(source_path, 0);
    if (source_handle == -1) {
        return -1;
    }

    open_file_entry_t *source_file = get_open_file_entry(source_handle);
    if (source_file == NULL) {
        return -1;
    }

    lock_mutex(&source_file->file_lock);
    inode_t *source_inode = inode_get(source_file->of_inumber);
    unlock_mutex(&source_file->file_lock);
    if (source_inode == NULL) {
        return -1;
    }

    FILE *dest_file = fopen(dest_path, "w");
    if (dest_file == NULL) {
        return -1;
    }

    ssize_t bytes_read;
    void *buffer;
    buffer = malloc(BLOCK_SIZE);
    if (buffer == NULL) {
        return -1;
    }

    do {
        memset(buffer, 0, BLOCK_SIZE);
        bytes_read = tfs_read(source_handle, buffer, BLOCK_SIZE);
        if (bytes_read == -1) {
            free(buffer);
            return -1;
        }
        size_t bytes_to_be_written = (size_t) bytes_read;
        size_t bytes_written = fwrite(buffer, 1, bytes_to_be_written, dest_file);
        if (bytes_written != bytes_to_be_written) {
            free(buffer);
            return -1;
        }
    } while (bytes_read == BLOCK_SIZE); // stops when it reads less than BLOCK_SIZE (we have, then, read the final block)

    free(buffer);

    if (tfs_close(source_handle) == -1) {
        return -1;
    }
    fclose(dest_file);
    return 0;
}