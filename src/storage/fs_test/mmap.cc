// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <optional>
#include <string>
#include <vector>

#include <fbl/unique_fd.h>
#include <gmock/gmock.h>

#include "src/storage/fs_test/fs_test_fixture.h"

namespace fs_test {
namespace {

// These tests supplement the cross-platform mmap tests in `sdk/lib/fdio/tests/fdio_mman.cc` by:
// testing additional combinations of inputs and handling edge cases specific to particular
// filesystems implementations on Fuchsia.

using ::testing::_;

using MmapTest = FilesystemTest;

// Tests which require MAP_SHARED to propagate writes to/from both the mapped region and
// the underlying file.
using MmapSharedWriteTest = FilesystemTest;

enum class DeathTestOp {
  Read,
  Write,
  ReadAfterUnmap,
  WriteAfterUnmap,
};

// Helper function for death tests.
void mmap_crash(const std::string& path, int prot, int flags, DeathTestOp rw) {
  fbl::unique_fd fd(open(path.c_str(), O_RDWR));
  ASSERT_TRUE(fd);
  void* addr = mmap(nullptr, PAGE_SIZE, prot, flags, fd.get(), 0);
  ASSERT_NE(addr, MAP_FAILED);
  ASSERT_EQ(close(fd.release()), 0);

  switch (rw) {
    case DeathTestOp::Read:
      ASSERT_DEATH([[maybe_unused]] int v = *static_cast<volatile int*>(addr), _);
      ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
      break;
    case DeathTestOp::Write:
      ASSERT_DEATH(*static_cast<int*>(addr) = 5, _);
      ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
      break;
    case DeathTestOp::ReadAfterUnmap:
      ASSERT_DEATH(
          {
            // Perform the munmap here as ASSERT_DEATH creates a thread and performs allocations,
            // which could then reuse the slot we just unmapped. As there are no other active
            // threads performing allocations in these tests, unmapping here should prevent any
            // races between the unmap and the access.
            munmap(addr, PAGE_SIZE);
            [[maybe_unused]] int v = *static_cast<volatile int*>(addr);
          },
          _);
      ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
      break;
    case DeathTestOp::WriteAfterUnmap:
      ASSERT_DEATH(
          {
            // Perform the munmap here as ASSERT_DEATH creates a thread and performs allocations,
            // which could then reuse the slot we just unmapped. As there are no other active
            // threads performing allocations in these tests, unmapping here should prevent any
            // races between the unmap and the access.
            munmap(addr, PAGE_SIZE);
            *static_cast<int*>(addr) = 5;
          },
          _);
      ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
      break;
  }
}

// Certain filesystems delay creation of internal structures until the file is initially accessed.
// Test that we can actually mmap properly before the file has otherwise been accessed. This test
// relies on size changes being tracked in the underlying file.
//
// Tracking size changes is NOT required by the POSIX standard, and it is expected that not all
// Fuchsia filesystems will support that - thus, this test may need to be updated or removed.
TEST_P(MmapSharedWriteTest, Empty) {
  const std::string filename = GetPath("mmap_empty");
  fbl::unique_fd fd(open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);

  char tmp[] = "this is a temporary buffer";
  void* addr = mmap(nullptr, PAGE_SIZE, PROT_READ, MAP_SHARED, fd.get(), 0);
  ASSERT_NE(addr, MAP_FAILED);
  ASSERT_EQ(write(fd.get(), tmp, sizeof(tmp)), static_cast<ssize_t>(sizeof(tmp)));
  ASSERT_EQ(memcmp(addr, tmp, sizeof(tmp)), 0);

  ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(filename.c_str()), 0);
}

// Test that file writes are propagated to a shared read-only buffer, excluding size changes.
TEST_P(MmapTest, Readable) {
  const std::string filename = GetPath("mmap_readable");
  fbl::unique_fd fd(open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);

  char tmp1[] = "this is a temporary buffer";
  char tmp2[] = "and this is a secondary buffer";
  static_assert(sizeof(tmp2) >= sizeof(tmp1), "Size of tmp2 must be >= size of tmp1!");
  ASSERT_EQ(write(fd.get(), tmp1, sizeof(tmp1)), static_cast<ssize_t>(sizeof(tmp1)));

  // Demonstrate that a simple buffer can be mapped
  void* addr = mmap(nullptr, PAGE_SIZE, PROT_READ, MAP_SHARED, fd.get(), 0);
  ASSERT_NE(addr, MAP_FAILED);
  ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);

  // Show that if we overwrite part of the file, the mapping is also updated within the originally
  // mapped region.
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(write(fd.get(), tmp2, sizeof(tmp2)), static_cast<ssize_t>(sizeof(tmp2)));
  // We only compare sizeof(tmp1) bytes, not sizeof(tmp2), as not all implementations track size
  // changes (and the POSIX standard does not mandate it).
  ASSERT_EQ(memcmp(addr, tmp2, sizeof(tmp1)), 0);

  ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(filename.c_str()), 0);
}

// Test that a file's writes are properly propagated to a read-only buffer, including size changes.
//
// Tracking size changes is NOT required by the POSIX standard, and it is expected that not all
// Fuchsia filesystems will support that - thus, this test may need to be updated or removed.
TEST_P(MmapSharedWriteTest, ReadableSizeChange) {
  const std::string filename = GetPath("mmap_readable");
  fbl::unique_fd fd(open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);

  char tmp1[] = "this is a temporary buffer";
  char tmp2[] = "and this is a secondary buffer";
  ASSERT_EQ(write(fd.get(), tmp1, sizeof(tmp1)), static_cast<ssize_t>(sizeof(tmp1)));

  // Demonstrate that a simple buffer can be mapped
  void* addr = mmap(nullptr, PAGE_SIZE, PROT_READ, MAP_SHARED, fd.get(), 0);
  ASSERT_NE(addr, MAP_FAILED);
  ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);

  // Show that if we keep writing to the file, the mapping is also updated
  ASSERT_EQ(write(fd.get(), tmp2, sizeof(tmp2)), static_cast<ssize_t>(sizeof(tmp2)));
  void* addr2 = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) + sizeof(tmp1));
  ASSERT_EQ(memcmp(addr2, tmp2, sizeof(tmp2)), 0);

  // But the original part of the mapping is unchanged
  ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);

  ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(filename.c_str()), 0);
}

// Test that a mapped buffer's writes are properly propagated to the file.
//
// Tracking size changes is NOT required by the POSIX standard, and it is expected that not all
// Fuchsia filesystems will support that - thus, this test may need to be updated in the future.
TEST_P(MmapSharedWriteTest, Writable) {
  const std::string filename = GetPath("mmap_writable");
  fbl::unique_fd fd(open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);

  char tmp1[] = "this is a temporary buffer";
  char tmp2[] = "and this is a secondary buffer";
  ASSERT_EQ(write(fd.get(), tmp1, sizeof(tmp1)), static_cast<ssize_t>(sizeof(tmp1)));

  // Demonstrate that a simple buffer can be mapped
  void* addr = mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
  ASSERT_NE(addr, MAP_FAILED);
  ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);

  // Extend the file length up to the necessary size
  ASSERT_EQ(ftruncate(fd.get(), sizeof(tmp1) + sizeof(tmp2)), 0);

  // Write to the file in the mapping
  void* addr2 = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) + sizeof(tmp1));
  memcpy(addr2, tmp2, sizeof(tmp2));

  // Verify the write by reading from the file
  char buf[sizeof(tmp2)];
  ASSERT_EQ(read(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));
  ASSERT_EQ(memcmp(buf, tmp2, sizeof(tmp2)), 0);
  // But the original part of the mapping is unchanged
  ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);

  // Extending the file beyond the mapping should still leave the first page
  // accessible
  ASSERT_EQ(ftruncate(fd.get(), PAGE_SIZE * 2), 0);
  ASSERT_EQ(memcmp(addr, tmp1, sizeof(tmp1)), 0);
  ASSERT_EQ(memcmp(addr2, tmp2, sizeof(tmp2)), 0);
  for (size_t i = sizeof(tmp1) + sizeof(tmp2); i < PAGE_SIZE; i++) {
    auto caddr = reinterpret_cast<char*>(addr);
    ASSERT_EQ(caddr[i], 0);
  }

  ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(filename.c_str()), 0);
}

// Test that the mapping of a file remains usable even after
// the file has been closed / unlinked / renamed.
TEST_P(MmapTest, Unlinked) {
  const std::string filename = GetPath("mmap_unlinked");
  fbl::unique_fd fd(open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);

  char tmp[] = "this is a temporary buffer";
  ASSERT_EQ(write(fd.get(), tmp, sizeof(tmp)), static_cast<ssize_t>(sizeof(tmp)));

  // Demonstrate that a simple buffer can be mapped
  void* addr = mmap(nullptr, PAGE_SIZE, PROT_READ, MAP_SHARED, fd.get(), 0);
  ASSERT_NE(addr, MAP_FAILED);
  ASSERT_EQ(memcmp(addr, tmp, sizeof(tmp)), 0);

  // If we close the file, we can still access the mapping
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(memcmp(addr, tmp, sizeof(tmp)), 0);

  // If we rename the file, we can still access the mapping
  const std::string other_file = GetPath("otherfile");
  ASSERT_EQ(rename(filename.c_str(), other_file.c_str()), 0);
  ASSERT_EQ(memcmp(addr, tmp, sizeof(tmp)), 0);

  // If we unlink the file, we can still access the mapping
  ASSERT_EQ(unlink(other_file.c_str()), 0);
  ASSERT_EQ(memcmp(addr, tmp, sizeof(tmp)), 0);

  ASSERT_EQ(munmap(addr, PAGE_SIZE), 0);
}

// Test that MAP_SHARED propagates updates to the file.
TEST_P(MmapSharedWriteTest, Shared) {
  const std::string filename = GetPath("mmap_shared");
  fbl::unique_fd fd(open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);

  char tmp[] = "this is a temporary buffer";
  ASSERT_EQ(write(fd.get(), tmp, sizeof(tmp)), static_cast<ssize_t>(sizeof(tmp)));

  // Demonstrate that a simple buffer can be mapped
  void* addr1 = mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
  ASSERT_NE(addr1, MAP_FAILED);
  ASSERT_EQ(memcmp(addr1, tmp, sizeof(tmp)), 0);

  fbl::unique_fd fd2(open(filename.c_str(), O_RDWR));
  ASSERT_TRUE(fd2);

  // Demonstrate that the buffer can be mapped multiple times
  void* addr2 = mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd2.get(), 0);
  ASSERT_NE(addr2, MAP_FAILED);
  ASSERT_EQ(memcmp(addr2, tmp, sizeof(tmp)), 0);

  // Demonstrate that updates to the file are shared between mappings
  char tmp2[] = "buffer which will update through fd";
  ASSERT_EQ(lseek(fd.get(), 0, SEEK_SET), 0);
  ASSERT_EQ(write(fd.get(), tmp2, sizeof(tmp2)), static_cast<ssize_t>(sizeof(tmp2)));
  ASSERT_EQ(memcmp(addr1, tmp2, sizeof(tmp2)), 0);
  ASSERT_EQ(memcmp(addr2, tmp2, sizeof(tmp2)), 0);

  // Demonstrate that updates to the mappings are shared too
  char tmp3[] = "final buffer, which updates via mapping";
  memcpy(addr1, tmp3, sizeof(tmp3));
  ASSERT_EQ(memcmp(addr1, tmp3, sizeof(tmp3)), 0);
  ASSERT_EQ(memcmp(addr2, tmp3, sizeof(tmp3)), 0);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(close(fd2.release()), 0);
  ASSERT_EQ(munmap(addr2, PAGE_SIZE), 0);

  // Demonstrate that we can map a read-only file as shared + readable
  fd.reset(open(filename.c_str(), O_RDONLY));
  ASSERT_TRUE(fd);
  addr2 = mmap(nullptr, PAGE_SIZE, PROT_READ, MAP_SHARED, fd.get(), 0);
  ASSERT_NE(addr2, MAP_FAILED);
  ASSERT_EQ(memcmp(addr1, tmp3, sizeof(tmp3)), 0);
  ASSERT_EQ(memcmp(addr2, tmp3, sizeof(tmp3)), 0);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(munmap(addr2, PAGE_SIZE), 0);

  ASSERT_EQ(munmap(addr1, PAGE_SIZE), 0);
  ASSERT_EQ(unlink(filename.c_str()), 0);
}

// Test that MAP_PRIVATE keeps all copies of the buffer
// separate
TEST_P(MmapTest, Private) {
  const std::string filename = GetPath("mmap_private");
  fbl::unique_fd fd(open(filename.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);

  char buf[64];
  memset(buf, 'a', sizeof(buf));
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));

  // Demonstrate that a simple buffer can be mapped
  void* addr1 = mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd.get(), 0);
  ASSERT_NE(addr1, MAP_FAILED);
  ASSERT_EQ(memcmp(addr1, buf, sizeof(buf)), 0);
  // ... multiple times
  void* addr2 = mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd.get(), 0);
  ASSERT_NE(addr2, MAP_FAILED);
  ASSERT_EQ(memcmp(addr2, buf, sizeof(buf)), 0);

  // File: 'a'
  // addr1 private copy: 'b'
  // addr2 private copy: 'c'
  memset(buf, 'b', sizeof(buf));
  memcpy(addr1, buf, sizeof(buf));
  memset(buf, 'c', sizeof(buf));
  memcpy(addr2, buf, sizeof(buf));

  // Verify the file and two buffers all have independent contents
  memset(buf, 'a', sizeof(buf));
  char tmp[sizeof(buf)];
  ASSERT_EQ(lseek(fd.get(), SEEK_SET, 0), 0);
  ASSERT_EQ(read(fd.get(), tmp, sizeof(tmp)), static_cast<ssize_t>(sizeof(tmp)));
  ASSERT_EQ(memcmp(tmp, buf, sizeof(tmp)), 0);
  memset(buf, 'b', sizeof(buf));
  ASSERT_EQ(memcmp(addr1, buf, sizeof(buf)), 0);
  memset(buf, 'c', sizeof(buf));
  ASSERT_EQ(memcmp(addr2, buf, sizeof(buf)), 0);

  ASSERT_EQ(munmap(addr1, PAGE_SIZE), 0);
  ASSERT_EQ(munmap(addr2, PAGE_SIZE), 0);
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(unlink(filename.c_str()), 0);
}

// Test that we fail to mmap an fd that does not support it.
TEST_P(MmapTest, FailMapDirectory) {
  // Try (and fail) to mmap a directory
  const std::string mydir = GetPath("mydir");
  ASSERT_EQ(mkdir(mydir.c_str(), 0666), 0);
  fbl::unique_fd fd(open(mydir.c_str(), O_RDONLY | O_DIRECTORY));
  ASSERT_TRUE(fd);
  ASSERT_EQ(mmap(nullptr, PAGE_SIZE, PROT_READ, MAP_SHARED, fd.get(), 0), MAP_FAILED);
  ASSERT_EQ(errno, ENODEV);
  errno = 0;
  ASSERT_EQ(close(fd.release()), 0);
  ASSERT_EQ(rmdir(mydir.c_str()), 0);
}

TEST_P(MmapTest, BadPermissions) {
  const std::string myfile = GetPath("myfile");
  fbl::unique_fd fd(open(myfile.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);
  // Test all cases of MAP_PRIVATE + PROT_WRITE and MAP_SHARED + PROT_READ which require a
  // readable file.
  fd.reset(open(myfile.c_str(), O_WRONLY));
  ASSERT_TRUE(fd);
  ASSERT_EQ(mmap(nullptr, PAGE_SIZE, PROT_READ, MAP_PRIVATE, fd.get(), 0), MAP_FAILED);
  ASSERT_EQ(errno, EACCES);
  errno = 0;
  ASSERT_EQ(mmap(nullptr, PAGE_SIZE, PROT_WRITE, MAP_PRIVATE, fd.get(), 0), MAP_FAILED);
  ASSERT_EQ(errno, EACCES);
  errno = 0;
  ASSERT_EQ(mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE, fd.get(), 0), MAP_FAILED);
  ASSERT_EQ(errno, EACCES);
  errno = 0;
  ASSERT_EQ(mmap(nullptr, PAGE_SIZE, PROT_READ, MAP_SHARED, fd.get(), 0), MAP_FAILED);
  ASSERT_EQ(errno, EACCES);
  errno = 0;
  ASSERT_EQ(close(fd.release()), 0);

  ASSERT_EQ(unlink(myfile.c_str()), 0);
}

TEST_P(MmapSharedWriteTest, BadPermissions) {
  const std::string myfile = GetPath("myfile");
  fbl::unique_fd fd(open(myfile.c_str(), O_RDWR | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);
  ASSERT_EQ(close(fd.release()), 0);
  // Test all cases of MAP_SHARED + PROT_WRITE which require a readable file.
  fd.reset(open(myfile.c_str(), O_WRONLY));
  ASSERT_TRUE(fd);
  ASSERT_EQ(mmap(nullptr, PAGE_SIZE, PROT_WRITE, MAP_SHARED, fd.get(), 0), MAP_FAILED);
  ASSERT_EQ(errno, EACCES);
  errno = 0;
  ASSERT_EQ(mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0), MAP_FAILED);
  ASSERT_EQ(errno, EACCES);
  errno = 0;
  ASSERT_EQ(close(fd.release()), 0);
  // Test all cases of MAP_PRIVATE and MAP_SHARED which require a
  // writable file (notably, MAP_PRIVATE never requires a writable
  // file, since it makes a copy).
  fd.reset(open(myfile.c_str(), O_RDONLY));
  ASSERT_TRUE(fd);
  ASSERT_EQ(mmap(nullptr, PAGE_SIZE, PROT_WRITE, MAP_SHARED, fd.get(), 0), MAP_FAILED);
  ASSERT_EQ(errno, EACCES);
  errno = 0;
  ASSERT_EQ(mmap(nullptr, PAGE_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0), MAP_FAILED);
  ASSERT_EQ(errno, EACCES);
  errno = 0;
  ASSERT_EQ(close(fd.release()), 0);
  // PROT_WRITE requires that the file is NOT append-only
  fd.reset(open(myfile.c_str(), O_RDWR | O_APPEND));
  ASSERT_TRUE(fd);
  ASSERT_EQ(mmap(nullptr, PAGE_SIZE, PROT_WRITE, MAP_SHARED, fd.get(), 0), MAP_FAILED);
  ASSERT_EQ(errno, EACCES);
  errno = 0;
  ASSERT_EQ(close(fd.release()), 0);

  ASSERT_EQ(unlink(myfile.c_str()), 0);
}

// Tracking size changes is NOT required by the POSIX standard, and it is expected that not all
// Fuchsia filesystems will support that - thus, this test may need to be updated or removed.
TEST_P(MmapSharedWriteTest, TruncateAccess) {
  const std::string mmap_truncate = GetPath("mmap_truncate");
  fbl::unique_fd fd(open(mmap_truncate.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);

  constexpr size_t kPageCount = 5;
  char buf[PAGE_SIZE * kPageCount];
  memset(buf, 'a', sizeof(buf));
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));

  // Map all pages and validate their contents.
  void* addr = mmap(nullptr, sizeof(buf), PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
  ASSERT_NE(addr, MAP_FAILED);
  ASSERT_EQ(memcmp(addr, buf, sizeof(buf)), 0);

  constexpr size_t kHalfPage = PAGE_SIZE / 2;
  for (size_t i = (kPageCount * 2) - 1; i > 0; i--) {
    // Shrink the underlying file.
    size_t new_size = kHalfPage * i;
    ASSERT_EQ(ftruncate(fd.get(), new_size), 0);
    ASSERT_EQ(memcmp(addr, buf, new_size), 0);

    // Accessing beyond the end of the file, but within the mapping, is
    // undefined behavior on other platforms. However, on Fuchsia, this
    // behavior is explicitly memory-safe.
    char buf_beyond[PAGE_SIZE * kPageCount - new_size];
    memset(buf_beyond, 'b', sizeof(buf_beyond));
    void* beyond = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) + new_size);
    memset(beyond, 'b', sizeof(buf_beyond));
    ASSERT_EQ(memcmp(buf_beyond, beyond, sizeof(buf_beyond)), 0);
  }

  ASSERT_EQ(munmap(addr, sizeof(buf)), 0);
  ASSERT_EQ(unlink(mmap_truncate.c_str()), 0);
}

// Tracking size changes is NOT required by the POSIX standard, and it is expected that not all
// Fuchsia filesystems will support that - thus, this test may need to be updated or removed.
TEST_P(MmapSharedWriteTest, TruncateExtend) {
  const std::string mmap_truncate_extend = GetPath("mmap_truncate_extend");
  fbl::unique_fd fd(open(mmap_truncate_extend.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);

  constexpr size_t kPageCount = 5;
  char buf[PAGE_SIZE * kPageCount];
  memset(buf, 'a', sizeof(buf));
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));

  // Map all pages and validate their contents.
  void* addr = mmap(nullptr, sizeof(buf), PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
  ASSERT_NE(addr, MAP_FAILED);
  ASSERT_EQ(memcmp(addr, buf, sizeof(buf)), 0);

  constexpr size_t kHalfPage = PAGE_SIZE / 2;

  ASSERT_EQ(ftruncate(fd.get(), 0), 0);
  memset(buf, 0, sizeof(buf));

  // Even though we trample over the "out-of-bounds" part of the mapping,
  // ensure it is filled with zeroes as we truncate-extend it.
  for (size_t i = 1; i < kPageCount * 2; i++) {
    size_t new_size = kHalfPage * i;

    // Fill "out-of-bounds" with invalid data.
    char buf_beyond[PAGE_SIZE * kPageCount - new_size];
    memset(buf_beyond, 'b', sizeof(buf_beyond));
    void* beyond = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) + new_size);
    memset(beyond, 'b', sizeof(buf_beyond));
    ASSERT_EQ(memcmp(buf_beyond, beyond, sizeof(buf_beyond)), 0);

    // Observe that the truncate extension fills the file with zeroes.
    ASSERT_EQ(ftruncate(fd.get(), new_size), 0);
    ASSERT_EQ(memcmp(buf, addr, new_size), 0);
  }

  ASSERT_EQ(munmap(addr, sizeof(buf)), 0);
  ASSERT_EQ(unlink(mmap_truncate_extend.c_str()), 0);
}

// Tracking size changes is NOT required by the POSIX standard, and it is expected that not all
// Fuchsia filesystems will support that - thus, this test may need to be updated or removed.
TEST_P(MmapSharedWriteTest, TruncateWriteExtend) {
  const std::string mmap_write_extend = GetPath("mmap_write_extend");
  fbl::unique_fd fd(open(mmap_write_extend.c_str(), O_CREAT | O_RDWR, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);

  constexpr size_t kPageCount = 5;
  char buf[PAGE_SIZE * kPageCount];
  memset(buf, 'a', sizeof(buf));
  ASSERT_EQ(write(fd.get(), buf, sizeof(buf)), static_cast<ssize_t>(sizeof(buf)));

  // Map all pages and validate their contents.
  void* addr = mmap(nullptr, sizeof(buf), PROT_READ | PROT_WRITE, MAP_SHARED, fd.get(), 0);
  ASSERT_NE(addr, MAP_FAILED);
  ASSERT_EQ(memcmp(addr, buf, sizeof(buf)), 0);

  constexpr size_t kHalfPage = PAGE_SIZE / 2;

  ASSERT_EQ(ftruncate(fd.get(), 0), 0);
  memset(buf, 0, sizeof(buf));

  // Even though we trample over the "out-of-bounds" part of the mapping,
  // ensure it is filled with zeroes as we truncate-extend it.
  for (size_t i = 1; i < kPageCount * 2; i++) {
    size_t new_size = kHalfPage * i;

    // Fill "out-of-bounds" with invalid data.
    char buf_beyond[PAGE_SIZE * kPageCount - new_size];
    memset(buf_beyond, 'b', sizeof(buf_beyond));
    void* beyond = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(addr) + new_size);
    memset(beyond, 'b', sizeof(buf_beyond));
    ASSERT_EQ(memcmp(buf_beyond, beyond, sizeof(buf_beyond)), 0);

    // Observe that write extension fills the file with zeroes.
    off_t offset = static_cast<off_t>(new_size - 1);
    ASSERT_EQ(lseek(fd.get(), offset, SEEK_SET), offset);
    char zero = 0;
    ASSERT_EQ(write(fd.get(), &zero, 1), 1);
    ASSERT_EQ(memcmp(buf, addr, new_size), 0);
  }

  ASSERT_EQ(munmap(addr, sizeof(buf)), 0);
  ASSERT_EQ(unlink(mmap_write_extend.c_str()), 0);
}

TEST_P(MmapTest, Death) {
  const std::string inaccessible = GetPath("inaccessible");
  fbl::unique_fd fd(open(inaccessible.c_str(), O_RDWR | O_CREAT, S_IRUSR | S_IWUSR));
  ASSERT_TRUE(fd);
  char tmp[] = "this is a temporary buffer";
  ASSERT_EQ(write(fd.get(), tmp, sizeof(tmp)), static_cast<ssize_t>(sizeof(tmp)));
  ASSERT_EQ(close(fd.release()), 0);

  // Crashes while mapped
  mmap_crash(inaccessible, PROT_READ, MAP_PRIVATE, DeathTestOp::Write);
  mmap_crash(inaccessible, PROT_READ, MAP_SHARED, DeathTestOp::Write);

  // Write-only is not possible
  mmap_crash(inaccessible, PROT_NONE, MAP_SHARED, DeathTestOp::Read);
  mmap_crash(inaccessible, PROT_NONE, MAP_SHARED, DeathTestOp::Write);
  mmap_crash(inaccessible, PROT_NONE, MAP_SHARED, DeathTestOp::WriteAfterUnmap);

  // Crashes after unmapped
  mmap_crash(inaccessible, PROT_READ, MAP_PRIVATE, DeathTestOp::ReadAfterUnmap);
  mmap_crash(inaccessible, PROT_READ, MAP_SHARED, DeathTestOp::ReadAfterUnmap);
  mmap_crash(inaccessible, PROT_WRITE | PROT_READ, MAP_PRIVATE, DeathTestOp::WriteAfterUnmap);
  if (fs().GetTraits().supports_mmap_shared_write) {
    mmap_crash(inaccessible, PROT_WRITE | PROT_READ, MAP_SHARED, DeathTestOp::WriteAfterUnmap);
  }

  ASSERT_EQ(unlink(inaccessible.c_str()), 0);
}

std::vector<TestFilesystemOptions> GetMmapTestCombinations() {
  return MapAndFilterAllTestFilesystems(
      [](const TestFilesystemOptions& options) -> std::optional<TestFilesystemOptions> {
        if (options.filesystem->GetTraits().supports_mmap) {
          return options;
        } else {
          return std::nullopt;
        }
      });
}

std::vector<TestFilesystemOptions> GetMmapSharedWriteTestCombinations() {
  return MapAndFilterAllTestFilesystems(
      [](const TestFilesystemOptions& options) -> std::optional<TestFilesystemOptions> {
        if (options.filesystem->GetTraits().supports_mmap_shared_write) {
          return options;
        } else {
          return std::nullopt;
        }
      });
}

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, MmapTest, testing::ValuesIn(GetMmapTestCombinations()),
                         testing::PrintToStringParamName());

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(MmapTest);

INSTANTIATE_TEST_SUITE_P(/*no prefix*/, MmapSharedWriteTest,
                         testing::ValuesIn(GetMmapSharedWriteTestCombinations()),
                         testing::PrintToStringParamName());

GTEST_ALLOW_UNINSTANTIATED_PARAMETERIZED_TEST(MmapSharedWriteTest);

}  // namespace
}  // namespace fs_test
