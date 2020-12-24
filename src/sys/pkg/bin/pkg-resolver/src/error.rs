// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

/// Wraps a TUF error and provides an additional Timeout variant
#[derive(Debug, thiserror::Error)]
pub enum TufOrTimeout {
    #[error("rust tuf error")]
    Tuf(#[source] tuf::Error),

    #[error("tuf operation timed out")]
    Timeout,
}
