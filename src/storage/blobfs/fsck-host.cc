// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/storage/blobfs/fsck-host.h"

#include <memory>

#include "src/storage/blobfs/blobfs-checker.h"
#include "zircon/errors.h"

namespace blobfs {

zx_status_t Fsck(std::unique_ptr<Blobfs> blob) {
  return BlobfsChecker(std::move(blob)).Check() ? ZX_OK : ZX_ERR_IO_DATA_INTEGRITY;
}

}  // namespace blobfs
