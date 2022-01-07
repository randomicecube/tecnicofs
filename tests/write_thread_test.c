#include "../fs/operations.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define GRN "\x1B[32m"
#define RESET "\x1B[0m"

// struct that keeps the current iteration and file descriptor
typedef struct {
    int iteration;
    int fd;
    pthread_mutex_t lock;
} thread_data;

void *read_thread(void *arg) {
  // arg is the thread_data struct
  thread_data *data = (thread_data *) arg;
  // write current iteration in the buffer and write the buffer, with tfs_write, in the file with fd
  char buffer[40];
  lock_mutex(&data->lock);
  sprintf(buffer, "%d", data->iteration);
  unlock_mutex(&data->lock);
  ssize_t r = tfs_write(data->fd, buffer, strlen(buffer));
  assert(r == strlen(buffer));
  return NULL;
}

int main() {
  assert(tfs_init() != -1);

  char *path = "/f1";
  int fd = tfs_open(path, TFS_O_CREAT);
  assert(fd != -1);

  thread_data *data = malloc(sizeof(thread_data));
  data->fd = fd;
  pthread_mutex_init(&data->lock, NULL);
  
  pthread_t tid[6 * BLOCK_SIZE];
  for (int i = 0; i < 6 * BLOCK_SIZE; i++) {
    lock_mutex(&data->lock);
    data->iteration = i;
    unlock_mutex(&data->lock);
    pthread_create(&tid[i], NULL, read_thread, data);
  }

  for (int i = 0; i < 6 * BLOCK_SIZE; i++) {
    pthread_join(tid[i], NULL);
  }

  pthread_mutex_destroy(&data->lock);
  free(data);

  assert(tfs_close(fd) != -1);

  tfs_destroy();

  printf(GRN "Successful test\n" RESET);

  exit(EXIT_SUCCESS);
}