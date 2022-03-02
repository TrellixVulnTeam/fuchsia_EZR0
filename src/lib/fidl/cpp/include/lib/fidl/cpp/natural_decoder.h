// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_DECODER_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_DECODER_H_

#include <lib/fidl/llcpp/message.h>
#include <zircon/fidl.h>

#ifdef __Fuchsia__
#include <lib/zx/handle.h>
#include <lib/zx/object.h>
#endif

namespace fidl::internal {

class NaturalDecoder final {
 public:
  explicit NaturalDecoder(fidl::IncomingMessage message);
  ~NaturalDecoder();

  template <typename T>
  T* GetPtr(size_t offset) {
    return reinterpret_cast<T*>((body_.bytes() - body_offset_) + offset);
  }

  size_t GetOffset(const void* ptr) const { return GetOffset(reinterpret_cast<uintptr_t>(ptr)); }
  size_t GetOffset(uintptr_t ptr) const {
    // The |ptr| value comes from the message buffer, which we've already
    // validated. That means it should correspond to a valid offset within the
    // message.
    return ptr - reinterpret_cast<uintptr_t>(body_.bytes() - body_offset_);
  }

#ifdef __Fuchsia__
  void DecodeHandle(zx::object_base* value, size_t offset) {
    zx_handle_t* handle = GetPtr<zx_handle_t>(offset);
    value->reset(*handle);
    *handle = ZX_HANDLE_INVALID;
    if (value->is_valid()) {
      ++handle_index_;
    }
  }
#endif

  size_t EnvelopeValueOffset(const fidl_envelope_v2_t* envelope) const {
    if ((envelope->flags & FIDL_ENVELOPE_FLAGS_INLINING_MASK) != 0) {
      return GetOffset(&envelope->inline_value);
    }
    return GetOffset(*reinterpret_cast<const void* const*>(envelope));
  }

  struct EnvelopeUnknownDataInfoResult {
    size_t value_offset;
    uint32_t num_bytes;
    uint16_t num_handles;
    uint16_t flags;
  };

  EnvelopeUnknownDataInfoResult EnvelopeUnknownDataInfo(const fidl_envelope_v2_t* envelope) const {
    const auto* unknown_data_envelope =
        reinterpret_cast<const fidl_envelope_v2_unknown_data_t*>(envelope);

    EnvelopeUnknownDataInfoResult result;
    if ((unknown_data_envelope->flags & FIDL_ENVELOPE_FLAGS_INLINING_MASK) != 0) {
      result.value_offset = GetOffset(&envelope->inline_value);
      result.num_bytes = 4;
    } else {
      result.value_offset = unknown_data_envelope->out_of_line.offset;
      result.num_bytes = unknown_data_envelope->out_of_line.num_bytes;
    }
    result.num_handles = unknown_data_envelope->num_handles;
    result.flags = unknown_data_envelope->flags;

    return result;
  }

 private:
  fidl::IncomingMessage body_;

  // The body_offset_ is either 16 (when decoding the body of a transactional message, which is
  // itself a concatenation of 2 FIDL messages, the header and body), or 0 (when decoding a
  // standalone "at-rest" message body).
  uint32_t body_offset_ = 0;
#ifdef __Fuchsia__
  uint32_t handle_index_ = 0;
#endif
};

}  // namespace fidl::internal

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_DECODER_H_
