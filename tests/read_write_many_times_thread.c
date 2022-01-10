#include "../fs/operations.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define GRN "\x1B[32m"
#define RESET "\x1B[0m"
#define NUM_THREADS 127 // random number

// struct that keeps the current iteration and file descriptor
typedef struct {
  char *path;
  size_t bytes;
  pthread_mutex_t lock;
} thread_data;

void *read_thread(void *arg) {
  thread_data *data = (thread_data *) arg;
  lock_mutex(&data->lock);
  char *buffer = malloc(data->bytes);
  int fd;
  assert((fd = tfs_open(data->path, 0)) != -1);
  ssize_t bytes_read = tfs_read(fd, buffer, data->bytes);
  assert(bytes_read == data->bytes);
  assert(tfs_close(fd) != -1);
  unlock_mutex(&data->lock);
  free(buffer);
  return NULL;
}

void *write_thread(void *arg) {
  thread_data *data = (thread_data *) arg;
  lock_mutex(&data->lock);
  char *buffer = malloc(data->bytes);
  int fd;
  assert((fd = tfs_open(data->path, 0)) != -1);
  unlock_mutex(&data->lock);
  ssize_t bytes_written = tfs_write(fd, buffer, data->bytes);
  lock_mutex(&data->lock);
  assert(bytes_written == data->bytes);
  assert(tfs_close(fd) != -1);
  unlock_mutex(&data->lock);
  free(buffer);
  return NULL;
}

int main() {
  assert(tfs_init() != -1);

  char *path = malloc(BLOCK_SIZE);
  if (path == NULL) {
    perror("malloc error");
    exit(EXIT_FAILURE);
  }
  strcat(path, "/f18275");
  char *str = "cc -std=c11 -D_POSIX_C_SOURCE=200809L -Ifs -I. -fdiagnostics-color=always -Wall -Werror -Wextra -Wcast-align -Wconversion -Wfloat-equal -Wformat=2 -Wnull-dereference -Wshadow -Wsign-conversion -Wswitch-default -Wswitch-enum -Wundef -Wunreachable-code -Wunused -Wno-sign-compare -fsanitize=thread -g -O3   -c -o tests/read_write_many_times_thread.o tests/read_write_many_times_thread.c cc -lpthread -fsanitize=thread  tests/read_write_many_times_thread.o fs/operations.o fs/state.o   -o tests/read_write_many_times_thread";
  char *iteration = malloc(BLOCK_SIZE);
  if (iteration == NULL) {
    perror("malloc error");
    exit(EXIT_FAILURE);
  }

  thread_data *data = malloc(sizeof(thread_data));
  if (data == NULL) {
    perror("malloc failed");
    exit(EXIT_FAILURE);
  }
  data->path = path;
  init_mutex(&data->lock);

  /* Section below tests writing and reading in parallel from/to the same file*/
  
  pthread_t tid[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; i++) {
    sprintf(iteration, "%d", i);
    lock_mutex(&data->lock);
    strcat(data->path, iteration);
    unlock_mutex(&data->lock);
    int fd = tfs_open(path, TFS_O_CREAT);
    assert(fd != -1);
  
    size_t bytes_written = (size_t) tfs_write(fd, str, strlen(str));
    lock_mutex(&data->lock);
    data->bytes = bytes_written;
    assert(data->bytes == strlen(str));
    unlock_mutex(&data->lock);
  
    assert(tfs_close(fd) != -1);
    
    if (i % 2 == 0) {
      pthread_create(&tid[i], NULL, read_thread, data);
    }
    else {
      pthread_create(&tid[i], NULL, write_thread, data);
    }
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_join(tid[i], NULL);
  }

  destroy_mutex(&data->lock);
  free(data);
  free(iteration);

  assert(tfs_destroy() != -1);

  printf(GRN "Successful test\n" RESET);

  exit(EXIT_SUCCESS);
}