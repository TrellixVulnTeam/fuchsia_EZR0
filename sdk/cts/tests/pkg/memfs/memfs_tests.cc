// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <dirent.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/async/cpp/task.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/memfs/memfs.h>
#include <unistd.h>

#include <zxtest/zxtest.h>

namespace {

TEST(MemfsTests, TestMemfsNull) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);
  memfs_filesystem_t* vfs;
  zx_handle_t root;

  ASSERT_EQ(memfs_create_filesystem(loop.dispatcher(), &vfs, &root), ZX_OK);
  ASSERT_EQ(zx_handle_close(root), ZX_OK);
  sync_completion_t unmounted;
  memfs_free_filesystem(vfs, &unmounted);
  ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);
}

TEST(MemfsTests, TestMemfsBasic) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  // Create a memfs filesystem, acquire a file descriptor
  memfs_filesystem_t* vfs;
  zx_handle_t root;
  ASSERT_EQ(memfs_create_filesystem(loop.dispatcher(), &vfs, &root), ZX_OK);
  int fd;
  ASSERT_EQ(fdio_fd_create(root, &fd), ZX_OK);

  // Access files within the filesystem.
  DIR* d = fdopendir(fd);

  // Create a file
  const char* filename = "file-a";
  fd = openat(dirfd(d), filename, O_CREAT | O_RDWR);
  ASSERT_GE(fd, 0);
  const char* data = "hello";
  size_t datalen = strlen(data);
  ASSERT_EQ(write(fd, data, datalen), datalen);
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  char buf[32];
  ASSERT_EQ(read(fd, buf, sizeof(buf)), datalen);
  ASSERT_EQ(memcmp(buf, data, datalen), 0);
  close(fd);

  // Readdir the file
  struct dirent* de;
  ASSERT_NOT_NULL((de = readdir(d)));
  ASSERT_EQ(strcmp(de->d_name, "."), 0);
  ASSERT_NOT_NULL((de = readdir(d)));
  ASSERT_EQ(strcmp(de->d_name, filename), 0);
  ASSERT_NULL(readdir(d));

  ASSERT_EQ(closedir(d), 0);
  sync_completion_t unmounted;
  memfs_free_filesystem(vfs, &unmounted);
  ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);
}

TEST(MemfsTests, TestMemfsAppend) {
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_EQ(loop.StartThread(), ZX_OK);

  // Create a memfs filesystem, acquire a file descriptor
  memfs_filesystem_t* vfs;
  zx_handle_t root;
  ASSERT_EQ(memfs_create_filesystem(loop.dispatcher(), &vfs, &root), ZX_OK);
  int fd;
  ASSERT_EQ(fdio_fd_create(root, &fd), ZX_OK);

  // Access files within the filesystem.
  DIR* d = fdopendir(fd);

  // Create a file
  const char* filename = "file-a";
  fd = openat(dirfd(d), filename, O_CREAT | O_RDWR | O_APPEND);
  ASSERT_GE(fd, 0);
  const char* data = "hello";
  size_t datalen = strlen(data);
  ASSERT_EQ(write(fd, data, datalen), datalen);
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  data = ", world";
  datalen = strlen(data);
  ASSERT_EQ(write(fd, data, datalen), datalen);
  ASSERT_EQ(lseek(fd, 0, SEEK_CUR), 12);
  ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
  data = "hello, world";
  datalen = strlen(data);
  char buf[32];
  ASSERT_EQ(read(fd, buf, sizeof(buf)), datalen);
  ASSERT_EQ(memcmp(buf, data, datalen), 0);
  close(fd);

  ASSERT_EQ(closedir(d), 0);
  sync_completion_t unmounted;
  memfs_free_filesystem(vfs, &unmounted);
  ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);
}

TEST(MemfsTests, TestMemfsInstall) {
  memfs_filesystem_t* fs;
  memfs_filesystem_t* fs_2;
  {
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/mytmp", &fs), ZX_OK);
    int fd = open("/mytmp", O_DIRECTORY | O_RDONLY);
    ASSERT_GE(fd, 0);

    // Access files within the filesystem.
    DIR* d = fdopendir(fd);

    // Create a file
    const char* filename = "file-a";
    fd = openat(dirfd(d), filename, O_CREAT | O_RDWR);
    ASSERT_GE(fd, 0);
    const char* data = "hello";
    size_t datalen = strlen(data);
    ASSERT_EQ(write(fd, data, datalen), datalen);
    ASSERT_EQ(lseek(fd, 0, SEEK_SET), 0);
    char buf[32];
    ASSERT_EQ(read(fd, buf, sizeof(buf)), datalen);
    ASSERT_EQ(memcmp(buf, data, datalen), 0);
    close(fd);

    // Readdir the file
    struct dirent* de;
    ASSERT_NOT_NULL((de = readdir(d)));
    ASSERT_EQ(strcmp(de->d_name, "."), 0);
    ASSERT_NOT_NULL((de = readdir(d)));
    ASSERT_EQ(strcmp(de->d_name, filename), 0);
    ASSERT_NULL(readdir(d));

    ASSERT_EQ(closedir(d), 0);
    ASSERT_EQ(memfs_install_at(loop.dispatcher(), "/mytmp", &fs_2), ZX_ERR_ALREADY_EXISTS);

    // Wait for cleanup of failed memfs install.
    sync_completion_t unmounted;
    async::PostTask(loop.dispatcher(), [&unmounted]() { sync_completion_signal(&unmounted); });
    ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);

    loop.Shutdown();
  }
  memfs_uninstall_unsafe(fs, "/mytmp");

  // No way to clean up the namespace entry. See fxb/31875 for more details.
}

TEST(MemfsTests, TestMemfsCloseDuringAccess) {
  for (int i = 0; i < 100; i++) {
    async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
    ASSERT_EQ(loop.StartThread(), ZX_OK);

    // Create a memfs filesystem, acquire a file descriptor
    memfs_filesystem_t* vfs;
    zx_handle_t root;
    ASSERT_EQ(memfs_create_filesystem(loop.dispatcher(), &vfs, &root), ZX_OK);
    int fd = -1;
    ASSERT_EQ(fdio_fd_create(root, &fd), ZX_OK);

    // Access files within the filesystem.
    DIR* d = fdopendir(fd);
    ASSERT_NOT_NULL(d);
    thrd_t worker;

    struct thread_args {
      DIR* d;
      sync_completion_t spinning{};
    } args{
        .d = d,
    };

    ASSERT_EQ(thrd_create(
                  &worker,
                  [](void* arg) {
                    thread_args* args = reinterpret_cast<thread_args*>(arg);
                    DIR* d = args->d;
                    int fd = openat(dirfd(d), "foo", O_CREAT | O_RDWR);
                    while (true) {
                      if (close(fd)) {
                        return errno == EPIPE ? 0 : -1;
                      }

                      if ((fd = openat(dirfd(d), "foo", O_RDWR)) < 0) {
                        return errno == EPIPE ? 0 : -1;
                      }
                      sync_completion_signal(&args->spinning);
                    }
                  },
                  &args),
              thrd_success);

    ASSERT_EQ(sync_completion_wait(&args.spinning, zx::duration::infinite().get()), ZX_OK);

    sync_completion_t unmounted;
    memfs_free_filesystem(vfs, &unmounted);
    ASSERT_EQ(sync_completion_wait(&unmounted, zx::duration::infinite().get()), ZX_OK);

    int result;
    ASSERT_EQ(thrd_join(worker, &result), thrd_success);
    ASSERT_EQ(result, 0);

    // Now that the filesystem has terminated, we should be
    // unable to access it.
    ASSERT_LT(openat(dirfd(d), "foo", O_CREAT | O_RDWR), 0);
    ASSERT_EQ(errno, EPIPE, "Expected connection to remote server to be closed");

    // Since the filesystem has terminated, this will
    // only close the client side of the connection.
    ASSERT_EQ(closedir(d), 0);
  }
}

}  // namespace
