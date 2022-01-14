testes profs

- `a`: Check if the copy to external FS functions throws
  errors when the external file path does not exist or the source file (inside TFS)
  does not exist.
- `b`: Create file with small content and try to copy it to an external file.
- `c`: Write and read to/from file with small string.
- `l`: Fill a file up to the 10 blocks, but only writing one block at a time.
- `m`: Fill a file up to the 10 blocks, but writes may write to more than one block at a time.
- `n`: Fill a file over 10 blocks, but only writing one block at a time.

testes custom

threads:

- `d`: Copy various files multiple times concurrently to the external FS,
  and compare their contents with the original.
- `e`: Create as many files as possible, in order to test concurrency of `inode_create`.
- `f`: Try to create the same file concurrently and check if duplicate files are created.
- `g`: Fill a file with large content and read from it on multiple threads at the same time.
- `h`: Test writing and reading to/from the same file descriptor on multiple threads concurrently.
- `i`: Write to new files concurrently, and then append and/or truncate them
  concurrently as well, verifying the end result.
- `j`: Create various files in different thread with different content,
  ensuring there is spill while writing, and then compares with the original content on the main thread.

not threads:

- `k`: Fill a file over 10 blocks, but writes may write to more than one block at a time.
