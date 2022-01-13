#include "../fs/operations.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define GRN "\x1B[32m"
#define RESET "\x1B[0m"
#define NUM_THREADS 20

size_t expected_final_bytes_read = 0;
size_t upper_bound_digits = 10;
size_t current_increment = 1;
size_t counter = 0;

typedef struct {
  char *path;
  pthread_mutex_t lock;
  size_t iteration;
} thread_data;

void *write_thread(void *arg) {
  sleep(1);
  thread_data *data = (thread_data *) arg;
  char *buffer = malloc(16);
  if (buffer == NULL) {
    perror("malloc error");
    exit(EXIT_FAILURE);
  }
  lock_mutex(&data->lock);
  data->iteration = counter++;
  if (data->iteration >= upper_bound_digits) {
    upper_bound_digits *= 10;
    current_increment++;
  }
  expected_final_bytes_read += current_increment;
  snprintf(buffer, 16, "%ld", data->iteration);
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
  char *buffer = malloc(expected_final_bytes_read);
  if (buffer == NULL) {
    perror("malloc error");
    exit(EXIT_FAILURE);
  }
  tfs_read(fd, buffer, expected_final_bytes_read);
  assert(tfs_close(fd) != -1);

  destroy_mutex(&data->lock);

  free(data);

  assert(tfs_destroy() != -1);

  printf(GRN "Successful test.\n" RESET);

  exit(EXIT_SUCCESS);
}
