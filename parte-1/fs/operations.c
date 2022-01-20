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

    lock_mutex(&open_file_lock); // prevents two files with the same name being created
    inum = tfs_lookup(name);
    if (inum >= 0) {
        pthread_rwlock_t *inode_lock = get_inode_table_lock(inum);
        write_lock_rwlock(inode_lock);
        /* The file already exists */
        inode_t *inode = inode_get(inum);
        if (inode == NULL) {
        	unlock_mutex(&open_file_lock);
            unlock_rwlock(inode_lock);
            return -1;
        }
        /* Trucate (if requested) */
        if (flags & TFS_O_TRUNC) {
            if (inode->i_size > 0) {
                size_t written_blocks = inode->i_size / BLOCK_SIZE;
                if (written_blocks % BLOCK_SIZE != 0) {
                    written_blocks++;
                }
                size_t loop_limit = (written_blocks <= 10) ? written_blocks : 10;
                for (int i = 0; i < loop_limit; i++) {
                    
                    if (data_block_free(inode->i_data_block[i]) == -1) {
        				unlock_mutex(&open_file_lock);
                        unlock_rwlock(inode_lock);
                        return -1;
                    }
                    inode->i_data_block[i] = -1;
                }
                if (written_blocks >= MAX_DIRECT_BLOCKS) {
                    if (data_block_free(inode->i_indirect_data_block) == -1) {
        			    unlock_mutex(&open_file_lock);
                        unlock_rwlock(inode_lock);
                        return -1;
                    }
                    inode->i_indirect_data_block = -1;
                }
                inode->i_size = 0;
            }
        }
        /* Determine initial offset */
        if (flags & TFS_O_APPEND) {
            offset = inode->i_size;
        } else {
            offset = 0;
        }
        unlock_mutex(&open_file_lock);
        unlock_rwlock(inode_lock);
    } else if (flags & TFS_O_CREAT) {
        /* The file doesn't exist; the flags specify that it should be created */
        inum = inode_create(T_FILE);
        pthread_rwlock_t *inode_lock = get_inode_table_lock(inum);
        if (inum == -1) {
        	unlock_mutex(&open_file_lock);
            return -1;
        }
        write_lock_rwlock(inode_lock);
        /* Add entry in the root directory */
        if (add_dir_entry(ROOT_DIR_INUM, inum, name + 1) == -1) {
            inode_delete(inum);
        	unlock_mutex(&open_file_lock);
            unlock_rwlock(inode_lock);
            return -1;
        }
        offset = 0;
        unlock_mutex(&open_file_lock);
        unlock_rwlock(inode_lock);
    } else {
        unlock_mutex(&open_file_lock);
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
    pthread_rwlock_t *file_lock = get_open_file_table_lock(fhandle);

    /* From the open file table entry, we get the inode */
    write_lock_rwlock(file_lock);
    int inum = file->of_inumber;
    inode_t *inode = inode_get(inum);
    if (inode == NULL) {
		unlock_rwlock(file_lock);
        return -1;
    }

    size_t bytes_written = 0;
    pthread_rwlock_t *inode_lock = get_inode_table_lock(inum);
	write_lock_rwlock(inode_lock);

    if (to_write > 0) {
        size_t previously_written_blocks = file->of_offset / BLOCK_SIZE;
        size_t end_write_blocks = (file->of_offset + to_write) / BLOCK_SIZE;
        size_t buffer_offset = 0;
        if (to_write % BLOCK_SIZE > 0) {
            end_write_blocks++;
        }
	
        for (size_t i = previously_written_blocks; i < end_write_blocks; i++) {
            void *block;
            if (i < MAX_DIRECT_BLOCKS) {
                if (inode->i_data_block[i] == -1) {
                    inode->i_data_block[i] = data_block_alloc();
                    if (inode->i_data_block[i] == -1) {
                        unlock_rwlock(file_lock);
                        unlock_rwlock(inode_lock);
                        return -1;
                    }
                }
                block = data_block_get(inode->i_data_block[i]);
                if (block == NULL) {
                    unlock_rwlock(file_lock);
                    unlock_rwlock(inode_lock);
                    return -1;
                }
            } else {
                int indirect_block_num;
                if (inode->i_indirect_data_block == -1) {
                    inode->i_indirect_data_block = data_block_alloc();
                    indirect_block_num = inode->i_indirect_data_block;
                    int *indirect_block = data_block_get(indirect_block_num);
                    if (indirect_block == NULL) {
                        unlock_rwlock(file_lock);
                        unlock_rwlock(inode_lock);
                        return -1;
                    }
                    for (int j = MAX_DIRECT_BLOCKS; j < BLOCK_SIZE; j++) {
                        indirect_block[j-MAX_DIRECT_BLOCKS] = -1; // they start at -1, allocated if needed
                    }
                }
                indirect_block_num = inode->i_indirect_data_block;
                int *indirect_block = (int *) data_block_get(indirect_block_num);
                if (indirect_block == NULL) {
                    unlock_rwlock(file_lock);
                    unlock_rwlock(inode_lock);
                    return -1;
                }

                if (indirect_block[i-MAX_DIRECT_BLOCKS] == -1) {
                    indirect_block[i-MAX_DIRECT_BLOCKS] = data_block_alloc();
                    if (indirect_block[i-MAX_DIRECT_BLOCKS] == -1) {
                        unlock_rwlock(file_lock);
                        unlock_rwlock(inode_lock);
                        return -1;
                    }
                }
                block = data_block_get(indirect_block[i-MAX_DIRECT_BLOCKS]);
                if (block == NULL) {
                    unlock_rwlock(file_lock);
                    unlock_rwlock(inode_lock);
                    return -1;
                }
            }
            if (i + 1 < end_write_blocks && to_write > BLOCK_SIZE - (file->of_offset % BLOCK_SIZE)) {
                // we continue writing for one more block
                size_t to_write_in_block = BLOCK_SIZE - (file->of_offset % BLOCK_SIZE);
                memcpy(block + file->of_offset % BLOCK_SIZE, buffer + buffer_offset, to_write_in_block);
                bytes_written += to_write_in_block;
                buffer_offset += to_write_in_block;
                file->of_offset += to_write_in_block;
                to_write -= to_write_in_block;
            } else if (to_write <= BLOCK_SIZE) {
                // last write
                memcpy(block + file->of_offset % BLOCK_SIZE, buffer + buffer_offset, to_write);
                bytes_written += to_write;
                buffer_offset += to_write;
                file->of_offset += to_write;
                to_write -= to_write;
                if (file->of_offset > inode->i_size) {
                    inode->i_size = file->of_offset;
                }
                break;
            } else {
                // we write the whole block and still continue
                memcpy(block + file->of_offset % BLOCK_SIZE, buffer + buffer_offset, BLOCK_SIZE);
                bytes_written += BLOCK_SIZE;
                buffer_offset += BLOCK_SIZE;
                file->of_offset += BLOCK_SIZE;
                to_write -= BLOCK_SIZE;
            }

            if (file->of_offset > inode->i_size) {
                inode->i_size = file->of_offset;
            }
        }
        unlock_rwlock(file_lock);
        unlock_rwlock(inode_lock);
    }

    return (ssize_t) bytes_written;
}

ssize_t tfs_read(int fhandle, void *buffer, size_t len) {
    open_file_entry_t *file = get_open_file_entry(fhandle);
    if (file == NULL) {
        return -1;
    }

    pthread_rwlock_t *file_lock = get_open_file_table_lock(fhandle);

    /* From the open file table entry, we get the inode */
	write_lock_rwlock(file_lock);
    int inum = file->of_inumber;
    inode_t *inode = inode_get(inum);
    if (inode == NULL) {
        unlock_rwlock(file_lock);
        return -1;
    }

    pthread_rwlock_t *inode_lock = get_inode_table_lock(inum);

    /* Determine how many bytes to read */
    write_lock_rwlock(inode_lock);
    size_t to_read = inode->i_size - file->of_offset;
    if (to_read > len) {
        to_read = len;
    }

    if (to_read <= 0) {
    	unlock_rwlock(file_lock);
    	unlock_rwlock(inode_lock);
        return 0;
    }

    size_t bytes_read = 0;

    size_t previously_read_blocks = file->of_offset / BLOCK_SIZE; 
    size_t end_read_blocks = (file->of_offset + to_read) / BLOCK_SIZE;
    size_t buffer_offset = 0;
    if (to_read % BLOCK_SIZE > 0) {
        end_read_blocks++;
    }

    for (size_t i = previously_read_blocks; i < end_read_blocks; i++) {
        void *block;
        if (i < MAX_DIRECT_BLOCKS) {
            int data_block_num = inode->i_data_block[i];
            block = data_block_get(data_block_num);
            if (block == NULL) {
                unlock_rwlock(file_lock);
                unlock_rwlock(inode_lock);
                return -1;
            }
        } else {
            int indirect_data_block_num = inode->i_indirect_data_block;
            int *indirect_block = (int *) data_block_get(indirect_data_block_num);
            if (indirect_block == NULL) {
                unlock_rwlock(file_lock);
                unlock_rwlock(inode_lock);
                return -1;
            }
            block = data_block_get(indirect_block[i-MAX_DIRECT_BLOCKS]);
            if (block == NULL) {
                unlock_rwlock(file_lock);
                unlock_rwlock(inode_lock);
                return -1;
            }
        }


        if (i + 1 < end_read_blocks && to_read > BLOCK_SIZE - (file->of_offset % BLOCK_SIZE)) {
            // we continue reading for one more block
            size_t to_read_in_block = BLOCK_SIZE - (file->of_offset % BLOCK_SIZE);
            memcpy(buffer + buffer_offset, block + file->of_offset % BLOCK_SIZE, to_read_in_block);
            bytes_read += to_read_in_block;
            buffer_offset += to_read_in_block;
            file->of_offset += to_read_in_block;
            to_read -= to_read_in_block;
        } else if (to_read <= BLOCK_SIZE) {
            // last read
            memcpy(buffer + buffer_offset, block + file->of_offset % BLOCK_SIZE, to_read);
            bytes_read += to_read;
            buffer_offset += to_read;
            file->of_offset += to_read;
            to_read -= to_read;
            break;
        } else {
            // we read the whole block and still continue
            memcpy(buffer + buffer_offset, block + file->of_offset % BLOCK_SIZE, BLOCK_SIZE);
            bytes_read += BLOCK_SIZE;
            buffer_offset += BLOCK_SIZE;
            file->of_offset += BLOCK_SIZE;
            to_read -= BLOCK_SIZE;
        }
    }

    unlock_rwlock(file_lock);
    unlock_rwlock(inode_lock);
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

    int source_inum = source_file->of_inumber;
    inode_t *source_inode = inode_get(source_inum);
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
    } while (bytes_read == BLOCK_SIZE); // stops when it reads less than BLOCK_SIZE bytes

    free(buffer);

    if (tfs_close(source_handle) == -1) {
        return -1;
    }
    fclose(dest_file);
    return 0;
}

int tfs_destroy_after_all_closed() {
    lock_mutex(&open_files_mutex);
    while (open_files_count != 0) {
        pthread_cond_wait(&open_files_cond, &open_files_mutex);
    }
    state_destroy();
    unlock_mutex(&open_files_mutex);
    destroy_mutex(&open_files_mutex);
    return 0;
}
