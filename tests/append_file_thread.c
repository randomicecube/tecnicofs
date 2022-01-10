#include "../fs/operations.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define GRN "\x1B[32m"
#define RESET "\x1B[0m"
#define NUM_THREADS 173 // random number

int expected_final_bytes_read = 0;
int upper_bound_digits = 10;
int current_increment = 1;

typedef struct {
  char *path;
  pthread_mutex_t lock;
  int iteration;
} thread_data;

void *write_thread(void *arg) {
  thread_data *data = (thread_data *) arg;
  char *buffer = malloc(16);
  lock_mutex(&data->lock);
  if (data->iteration >= upper_bound_digits) {
    upper_bound_digits *= 10;
    current_increment++;
  }
  expected_final_bytes_read += current_increment;
  snprintf(buffer, 16, "%d", data->iteration);
  int fd = tfs_open(data->path, TFS_O_APPEND);
  unlock_mutex(&data->lock);
  ssize_t r = tfs_write(fd, buffer, strlen(buffer));
  assert(tfs_close(fd) != -1);
  assert(r == strlen(buffer));
  return NULL;
}

int main() {
  assert(tfs_init() != -1);

  char *path = "/f1";
  int fd = tfs_open(path, TFS_O_CREAT);
  assert(fd != -1);
  assert(tfs_close(fd) != -1);

  thread_data *data = malloc(sizeof(thread_data));
  if (data == NULL) {
    perror("malloc error");
    exit(EXIT_FAILURE);
  }
  data->path = path;
  init_mutex(&data->lock);
  
  pthread_t tid[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; i++) {
    lock_mutex(&data->lock);
    data->iteration = i;
    unlock_mutex(&data->lock);
    if (pthread_create(&tid[i], NULL, write_thread, data) != 0) {
      perror("pthread_create error");
      exit(EXIT_FAILURE);
    }
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    if (pthread_join(tid[i], NULL)) {
      perror("pthread_join error");
      exit(EXIT_FAILURE);
    }
  }

  // checking at the end if the file was correctly appended
  assert((fd = tfs_open(path, 0)) != -1);
  char *buffer = malloc(BLOCK_SIZE); // BLOCK_SIZE works here, could be another
  ssize_t bytes_read = tfs_read(fd, buffer, BLOCK_SIZE);
  assert(bytes_read == expected_final_bytes_read);
  assert(tfs_close(fd) != -1);

  destroy_mutex(&data->lock);

  free(data);

  assert(tfs_destroy() != -1);

  printf(GRN "Successful test\n" RESET);

  exit(EXIT_SUCCESS);
}