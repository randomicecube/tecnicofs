#include "../fs/operations.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#define GRN "\x1B[32m"
#define RESET "\x1B[0m"
#define NUM_THREADS 1367 // random number

// struct that keeps the current iteration and file descriptor
typedef struct {
  char *path;
  size_t bytes;
  pthread_mutex_t lock;
} thread_data;

void *read_thread(void *arg) {
  thread_data *data = (thread_data *) arg;
  char *buffer = malloc(data->bytes);
  int fd;
  lock_mutex(&data->lock);
  assert((fd = tfs_open(data->path, 0)) != -1);
  ssize_t bytes_read = tfs_read(fd, buffer, data->bytes);
  assert(bytes_read == data->bytes);
  assert(tfs_close(fd) != -1);
  unlock_mutex(&data->lock);
  free(buffer);
  return NULL;
}

int main() {
  assert(tfs_init() != -1);

  char *path = "/f1";
  char *str;

  FILE *fp = fopen("lusiadas.txt", "r");
  size_t start = (size_t) ftell(fp);
  fseek(fp, 0, SEEK_END);
  size_t end = (size_t) ftell(fp);
  fseek(fp, 0, SEEK_SET);
  size_t delta = end - start;
  str = malloc(delta + 1);
  fread(str, 1, delta, fp);
  str[delta] = '\0';
  fclose(fp);

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
  if (pthread_mutex_init(&data->lock, NULL)) {
    perror("mutex init error");
    exit(EXIT_FAILURE);
  }
  
  pthread_t tid[NUM_THREADS];
  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_create(&tid[i], NULL, read_thread, data);
  }

  for (int i = 0; i < NUM_THREADS; i++) {
    pthread_join(tid[i], NULL);
  }

  if (pthread_mutex_destroy(&data->lock)) {
    perror("mutex destroy error");
    exit(EXIT_FAILURE);
  }
  free(data);

  assert(tfs_destroy() != -1);

  printf(GRN "Successful test\n" RESET);

  exit(EXIT_SUCCESS);
}