#include "../fs/operations.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>

#define GRN "\x1B[32m"
#define RESET "\x1B[0m"
#define NUM_THREADS 50

typedef struct {
  char *path;
  size_t bytes;
  pthread_mutex_t lock;
} thread_data;

void *read_thread(void *arg) {
  sleep(1);
  thread_data *data = (thread_data *) arg;
  char *buffer = malloc(data->bytes);
  if (buffer == NULL) {
    perror("malloc error");
    exit(EXIT_FAILURE);
  }
  int fd;
  lock_mutex(&data->lock);
  assert((fd = tfs_open(data->path, 0)) != -1);
  unlock_mutex(&data->lock);
  ssize_t bytes_read = tfs_read(fd, buffer, data->bytes);
  lock_mutex(&data->lock);
  assert(bytes_read == data->bytes);
  unlock_mutex(&data->lock);
  assert(tfs_close(fd) != -1);
  free(buffer);
  return NULL;
}

int main() {
  assert(tfs_init() != -1);

  char *path = "/f1";
  char *str = "cc -std=c11 -D_POSIX_C_SOURCE=200809L -Ifs -I. -fdiagnostics-color=always -Wall -Werror -Wextra -Wcast-align -Wconversion -Wfloat-equal -Wformat=2 -Wnull-dereference -Wshadow -Wsign-conversion -Wswitch-default -Wswitch-enum -Wundef -Wunreachable-code -Wunused -Wno-sign-compare -fsanitize=thread -g -O3   -c -o tests/read_write_many_times_thread.o tests/read_write_many_times_thread.c cc -lpthread -fsanitize=thread  tests/read_write_many_times_thread.o fs/operations.o fs/state.o   -o tests/read_write_many_times_thread";

  int fd = tfs_open(path, TFS_O_CREAT);
  assert(fd != -1);

  ssize_t bytes_written = tfs_write(fd, str, strlen(str));
  assert(bytes_written == strlen(str));

  assert(tfs_close(fd) != -1);

  thread_data *data = malloc(sizeof(thread_data));
  if (data == NULL) {
    perror("malloc failed");
    exit(EXIT_FAILURE);
  }
  data->path = path;
  data->bytes = (size_t) bytes_written;
  init_mutex(&data->lock);
  
  pthread_t tid[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; i++) {
    if (pthread_create(&tid[i], NULL, read_thread, data) != 0) {
      perror("pthread_create failed");
      exit(EXIT_FAILURE);
    }
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    if (pthread_join(tid[i], NULL) != 0) {
      perror("pthread_join failed");
      exit(EXIT_FAILURE);
    }
  }

  destroy_mutex(&data->lock);
  free(data);

  assert(tfs_destroy() != -1);

  printf(GRN "Successful test\n" RESET);

  exit(EXIT_SUCCESS);
}
