// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_STORAGE_EXTRACTOR_C_EXTRACTOR_H_
#define SRC_STORAGE_EXTRACTOR_C_EXTRACTOR_H_

// Warning:
// This file was autogenerated by cbindgen.
// Do not modify this file manually.
//
// To update or regenerate the file, run
// > fx build
// > cd src/storage/extractor/c
// > fx gen-cargo //src/storage/extractor/c:_disk_extractor_rustc_static
// > cbindgen $PWD/ -o $PWD/extractor.h -c cbindgen.toml
//
// clang-format off


#include <cstdarg>
#include <cstdint>
#include <cstdlib>
#include <new>

// DataKind describes the type of the data within an extent.
// DataKind priority is Skipped<Zeroes<Unmodified<Modified.
enum class DataKind {
  // Skipped dumping data for the extent.
  //
  // It maybe skipped because of various reasons like ExtentKind is
  // {Unmapped, Unused, Pii} or it was skipped because storage software did not
  // find the contents useful.
  Skipped,
  // Skipped dumping extent data because it contained only zeroes.
  Zeroes,
  // Dumped data is unmodified.
  Unmodified,
  // Dumped data is modifed to obfuscate Pii.
  Modified,
};

// Defines errors used in the crate.
// Enum defines types of errors and their human readable messages.
enum class Error {
  // Given extent cannot override already added extent. This may happen
  // because a part of extent having higher priority already exists.
  CannotOverride,
  // Given extent already exists with same set of properties.
  Exists,
  // Current options do not allow extraction of this type of block.
  NotAllowed,
  // Failed to seek input stream.
  SeekFailed,
  // Failed to read the input stream.
  ReadFailed,
  // Failed to write the extracted image out out stream.
  WriteFailed,
  // The extent has invalid range.
  InvalidRange,
  // The data lenght and range lenght do not match.
  InvalidDataLength,
  // The offset found is invalid.
  InvalidOffset,
  // The invalid argument.
  InvalidArgument,
  // The failed to parse.
  ParseFailed,
};

// ExtentKind describes the type of the extent.
//
// ExtentKind may mean different things based on the storage software.
// ExtentKind priority is Unmapped<Unused<Data<Pii.
enum class ExtentKind {
  // Extent is Unmapped.
  //
  // For example,
  // * In fvm based partitions/filesystem, unmapped may mean pslice does not exist for vslice.
  // * In ftl, it may mean that the logical block is not mapped to a physical page
  Unmmapped,
  // Extent is mapped but is not in use.
  //
  // For example,
  // * In filesystem this extent maybe free block as indicated by a "bitmap"
  // * In fvm this extent maybe a free slice.
  Unused,
  // Extent contain `Data` that is pii free and can be extracted.
  //
  // `Data` itself doesn't mean it will be written to the image.
  Data,
  // Extent contains data that is Pii.
  //
  // `Pii` itself doesn't mean extent data will not written to the image.
  Pii,
};

// `Extractor` helps to extract disk images.
//
// Extractor works with storage software like filesystems, fvm, etc
// to dump data of interest to a image file, which can be used to
// debug storage issues.
//
// Storage software tells what [`Extent`]s are useful adding data location
// <start, lenght> and properties. Extractor maintains a list of added extents
// and writes to the image file on calling [`write`].
//
// # Example
//
// ```
// use extractor_lib::extractor::{Extractor, ExtractorOptions};
//
// let options: ExtractorOptions = Default::default();
// let mut extractor = Extractor::new(in_file, options, out_file);
// extractor.add(10..11, default_properties(), None).Unwrap_NEW();
// extractor.add(12..14, default_properties(), None).Unwrap_NEW();
// extractor.write().Unwrap_NEW();
// ```
struct ExtractorRust;

// A simple structure to convert rust result into C compatible error type.
struct CResult {
  // Set to `true` on success and false on failure.
  bool ok;
  // If an operation has failed i.e. `ok` is false, `kind` indicates the type
  // of error.
  Error kind;
};

// `ExtractorOptions` tells what types of extents should be extracted and
// controls the contents of the extracted image.
struct ExtractorOptions {
  // If `true`, forces dumping of blocks that are considered pii by the
  // storage software. Enable this with caustion.
  bool force_dump_pii;
  // If `true`, each extent's checksums are added to extracted image.
  bool add_checksum;
  // Forces alignment of extents and extractor metadata within extracted
  // image file.
  uint64_t alignment;
  // Using gzip, compresses extracted image before writing it.
  bool compress;
};

// Properties of an extent
//
// extent_kind has higher priority than data_kind.
struct ExtentProperties {
  ExtentKind extent_kind;
  DataKind data_kind;
};

extern "C" {

// Creates a new [`Extractor`] and returns an opaque pointer to it.
//
// # Arguments
// `out_fd`: File descriptor pointing to rw file. The file will be truncated to
// zero lenght.
// `in_file`: File descriptor pointing to readable/seekable file.
// `options`: [`ExtractorOptions`]
//
// Asserts on failure to truncate.
CResult extractor_new(int in_fd,
                      ExtractorOptions options,
                      int out_fd,
                      ExtractorRust **out_extractor);

// Destroys an [`Extractor`] object.
void extractor_delete(ExtractorRust *extractor);

// Adds a new extent to the `extractor`.
//
// # Arguments
// `offset`: Location where the extent's data can be found in `in_fd`.
// `size`: Size of the extent in bytes.
// `properties`: [`ExtentProperties`]
__attribute__((__warn_unused_result__))
CResult extractor_add(ExtractorRust *extractor,
                      uint64_t offset,
                      uint64_t size,
                      ExtentProperties properties);

// Writes staged extents to out_fd.
__attribute__((__warn_unused_result__)) CResult extractor_write(ExtractorRust *extractor);

// Deflate extracted image.
//
// # Arguments
// `out_fd`: File descriptor pointing to rw file. The file will contain
// deflated image.
// `in_file`: File descriptor pointing to readable/seekable extracted image file.
// `verbose_fd`: If valid(>=0), extractor will print information about extracted image to the
// stream.
CResult extractor_deflate(int in_fd, int out_fd, int verbose_fd);

} // extern "C"

#endif // SRC_STORAGE_EXTRACTOR_C_EXTRACTOR_H_
