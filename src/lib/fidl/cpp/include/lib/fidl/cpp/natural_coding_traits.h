// Copyright 2022 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_CODING_TRAITS_H_
#define SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_CODING_TRAITS_H_

#include <lib/fidl/cpp/natural_decoder.h>
#include <lib/fidl/cpp/natural_encoder.h>
#include <lib/fidl/llcpp/traits.h>
#include <lib/stdcompat/optional.h>

#ifdef __Fuchsia__
#include <lib/zx/channel.h>
#endif  // __Fuchsia__

#include <string>

namespace fidl::internal {

struct NaturalCodingConstraintEmpty {};

template <zx_obj_type_t ObjType, zx_rights_t Rights>
struct NaturalCodingConstraintHandle {
  static constexpr zx_obj_type_t obj_type = ObjType;
  static constexpr zx_rights_t rights = Rights;
};

template <size_t Limit = std::numeric_limits<size_t>::max()>
struct NaturalCodingConstraintString {
  static constexpr size_t limit = Limit;
};

template <typename Inner_, size_t Limit = std::numeric_limits<size_t>::max()>
struct NaturalCodingConstraintVector {
  using Inner = Inner_;
  static constexpr size_t limit = Limit;
};

static constexpr size_t kRecursionDepthInitial = 0;
static constexpr size_t kRecursionDepthMax = FIDL_RECURSION_DEPTH;

template <typename T, typename Constraint, class Enable = void>
struct NaturalCodingTraits;

template <typename T, typename Constraint>
size_t NaturalEncodingInlineSize(NaturalEncoder* encoder) {
  switch (encoder->wire_format()) {
    case ::fidl::internal::WireFormatVersion::kV1:
      return NaturalCodingTraits<T, Constraint>::inline_size_v1_no_ee;
    case ::fidl::internal::WireFormatVersion::kV2:
      return NaturalCodingTraits<T, Constraint>::inline_size_v2;
  }
  __builtin_unreachable();
}

template <typename T, typename Constraint>
size_t NaturalDecodingInlineSize(NaturalDecoder* decoder) {
  return NaturalCodingTraits<T, Constraint>::inline_size_v2;
}

template <typename T>
struct NaturalCodingTraits<T, NaturalCodingConstraintEmpty,
                           typename std::enable_if<NaturalIsPrimitive<T>::value>::type> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(T);
  static constexpr size_t inline_size_v2 = sizeof(T);
  static void Encode(NaturalEncoder* encoder, T* value, size_t offset, size_t recursion_depth) {
    *encoder->template GetPtr<T>(offset) = *value;
  }
  static void Decode(NaturalDecoder* decoder, T* value, size_t offset) {
    *value = *decoder->template GetPtr<T>(offset);
  }
};

template <>
struct NaturalCodingTraits<bool, NaturalCodingConstraintEmpty> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(bool);
  static constexpr size_t inline_size_v2 = sizeof(bool);
  static void Encode(NaturalEncoder* encoder, bool* value, size_t offset, size_t recursion_depth) {
    if (!(static_cast<uint8_t>(*value) == 0 || static_cast<uint8_t>(*value) == 1)) {
      encoder->SetError("invalid boolean value");
      return;
    }
    *encoder->template GetPtr<bool>(offset) = *value;
  }
  static void Encode(NaturalEncoder* encoder, std::vector<bool>::iterator value, size_t offset,
                     size_t recursion_depth) {
    if (!(static_cast<uint8_t>(*value) == 0 || static_cast<uint8_t>(*value) == 1)) {
      encoder->SetError("invalid boolean value");
      return;
    }
    *encoder->template GetPtr<bool>(offset) = *value;
  }
  static void Decode(NaturalDecoder* decoder, bool* value, size_t offset) {
    *value = *decoder->template GetPtr<bool>(offset);
  }
  static void Decode(NaturalDecoder* decoder, std::vector<bool>::iterator value, size_t offset) {
    *value = *decoder->template GetPtr<bool>(offset);
  }
};

template <bool Value>
class NaturalUseStdCopy {};

template <typename T, typename Constraint>
void NaturalEncodeVectorBody(NaturalUseStdCopy<true>, NaturalEncoder* encoder,
                             typename std::vector<T>::iterator in_begin,
                             typename std::vector<T>::iterator in_end, size_t out_offset,
                             size_t recursion_depth) {
  static_assert(NaturalCodingTraits<T, Constraint>::inline_size_v1_no_ee == sizeof(T),
                "stride doesn't match object size");
  std::copy(in_begin, in_end, encoder->template GetPtr<T>(out_offset));
}

template <typename T, typename Constraint>
void NaturalEncodeVectorBody(NaturalUseStdCopy<false>, NaturalEncoder* encoder,
                             typename std::vector<T>::iterator in_begin,
                             typename std::vector<T>::iterator in_end, size_t out_offset,
                             size_t recursion_depth) {
  size_t stride = NaturalEncodingInlineSize<T, Constraint>(encoder);
  for (typename std::vector<T>::iterator in_it = in_begin; in_it != in_end;
       in_it++, out_offset += stride) {
    NaturalCodingTraits<T, Constraint>::Encode(encoder, &*in_it, out_offset, recursion_depth);
  }
}

template <typename T, typename Constraint>
void NaturalDecodeVectorBody(NaturalUseStdCopy<true>, NaturalDecoder* decoder,
                             size_t in_begin_offset, size_t in_end_offset, std::vector<T>* out,
                             size_t count) {
  static_assert(NaturalCodingTraits<T, Constraint>::inline_size_v1_no_ee == sizeof(T),
                "stride doesn't match object size");
  *out = std::vector<T>(decoder->template GetPtr<T>(in_begin_offset),
                        decoder->template GetPtr<T>(in_end_offset));
}

template <typename T, typename Constraint>
void NaturalDecodeVectorBody(NaturalUseStdCopy<false>, NaturalDecoder* decoder,
                             size_t in_begin_offset, size_t in_end_offset, std::vector<T>* out,
                             size_t count) {
  out->resize(count);
  size_t stride = NaturalDecodingInlineSize<T, Constraint>(decoder);
  size_t in_offset = in_begin_offset;
  typename std::vector<T>::iterator out_it = out->begin();
  for (; in_offset < in_end_offset; in_offset += stride, out_it++) {
    NaturalCodingTraits<T, Constraint>::Decode(decoder, &*out_it, in_offset);
  }
}

template <typename T, typename Constraint>
struct NaturalCodingTraits<::std::vector<T>, Constraint> {
  using InnerConstraint = typename Constraint::Inner;
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_vector_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_vector_t);
  static void Encode(NaturalEncoder* encoder, ::std::vector<T>* value, size_t offset,
                     size_t recursion_depth) {
    size_t count = value->size();
    if (count > Constraint::limit) {
      encoder->SetError("vector limit exceeded");
      return;
    }
    if (recursion_depth + 1 > kRecursionDepthMax) {
      encoder->SetError("recursion depth exceeded");
      return;
    }

    fidl_vector_t* vector = encoder->template GetPtr<fidl_vector_t>(offset);
    vector->count = count;
    vector->data = reinterpret_cast<void*>(FIDL_ALLOC_PRESENT);
    size_t stride = NaturalEncodingInlineSize<T, InnerConstraint>(encoder);
    size_t base = encoder->Alloc(count * stride);
    internal::NaturalEncodeVectorBody<T, InnerConstraint>(
        internal::NaturalUseStdCopy<NaturalIsMemcpyCompatible<T>::value>(), encoder, value->begin(),
        value->end(), base, recursion_depth + 1);
  }
  static void Decode(NaturalDecoder* decoder, ::std::vector<T>* value, size_t offset) {
    fidl_vector_t* encoded = decoder->template GetPtr<fidl_vector_t>(offset);
    size_t stride = NaturalDecodingInlineSize<T, InnerConstraint>(decoder);
    size_t base = decoder->GetOffset(encoded->data);
    size_t count = encoded->count;
    internal::NaturalDecodeVectorBody<T, InnerConstraint>(
        internal::NaturalUseStdCopy<NaturalIsMemcpyCompatible<T>::value>(), decoder, base,
        base + stride * count, value, count);
  }
};

template <typename T, size_t N, typename Constraint>
struct NaturalCodingTraits<::std::array<T, N>, Constraint> {
  static constexpr size_t inline_size_v1_no_ee =
      NaturalCodingTraits<T, Constraint>::inline_size_v1_no_ee * N;
  static constexpr size_t inline_size_v2 = NaturalCodingTraits<T, Constraint>::inline_size_v2 * N;
  static void Encode(NaturalEncoder* encoder, std::array<T, N>* value, size_t offset,
                     size_t recursion_depth) {
    size_t stride;
    stride = NaturalEncodingInlineSize<T, Constraint>(encoder);
    if (NaturalIsMemcpyCompatible<T>::value) {
      memcpy(encoder->template GetPtr<void>(offset), &value[0], N * stride);
      return;
    }
    for (size_t i = 0; i < N; ++i) {
      NaturalCodingTraits<T, Constraint>::Encode(encoder, &value->at(i), offset + i * stride,
                                                 recursion_depth);
    }
  }
  static void Decode(NaturalDecoder* decoder, std::array<T, N>* value, size_t offset) {
    size_t stride = NaturalDecodingInlineSize<T, Constraint>(decoder);
    if (NaturalIsMemcpyCompatible<T>::value) {
      memcpy(&value[0], decoder->template GetPtr<void>(offset), N * stride);
      return;
    }
    for (size_t i = 0; i < N; ++i) {
      NaturalCodingTraits<T, Constraint>::Decode(decoder, &value->at(i), offset + i * stride);
    }
  }
};

#ifdef __Fuchsia__
template <typename T, typename Constraint>
struct NaturalCodingTraits<
    T, Constraint, typename std::enable_if<std::is_base_of<zx::object_base, T>::value>::type> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(zx_handle_t);
  static constexpr size_t inline_size_v2 = sizeof(zx_handle_t);
  static void Encode(NaturalEncoder* encoder, zx::object_base* value, size_t offset,
                     size_t recursion_depth) {
    encoder->EncodeHandle(value->release(),
                          {
                              .obj_type = Constraint::obj_type,
                              .rights = Constraint::rights,
                          },
                          offset);
  }
  static void Decode(NaturalDecoder* decoder, zx::object_base* value, size_t offset) {
    decoder->DecodeHandle(value, offset);
  }
};
#endif  // __Fuchsia__

template <typename T, typename Constraint>
struct NaturalCodingTraits<cpp17::optional<std::vector<T>>, Constraint> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_vector_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_vector_t);

  static void Encode(NaturalEncoder* encoder, cpp17::optional<std::vector<T>>* value, size_t offset,
                     size_t recursion_depth) {
    if (value->has_value()) {
      fidl::internal::NaturalCodingTraits<std::vector<T>, Constraint>::Encode(
          encoder, &value->value(), offset, recursion_depth);
      return;
    }
    fidl_vector_t* vec = encoder->template GetPtr<fidl_vector_t>(offset);
    vec->count = 0;
    vec->data = reinterpret_cast<char*>(FIDL_ALLOC_ABSENT);
  }
  static void Decode(NaturalDecoder* decoder, cpp17::optional<std::vector<T>>* value,
                     size_t offset) {
    fidl_vector_t* vec = decoder->template GetPtr<fidl_vector_t>(offset);
    if (vec->data == nullptr) {
      ZX_ASSERT(vec->count == 0);
      value->reset();
      return;
    }
    std::vector<T> unwrapped;
    fidl::internal::NaturalCodingTraits<std::vector<T>, Constraint>::Decode(decoder, &unwrapped,
                                                                            offset);
    value->emplace(std::move(unwrapped));
  }
};

template <typename T, typename Constraint>
struct NaturalCodingTraits<std::unique_ptr<T>, Constraint,
                           typename std::enable_if<!IsUnion<T>::value>::type> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(uintptr_t);
  static constexpr size_t inline_size_v2 = sizeof(uintptr_t);
  static void Encode(NaturalEncoder* encoder, std::unique_ptr<T>* value, size_t offset,
                     size_t recursion_depth) {
    if (value->get()) {
      if (recursion_depth + 1 > kRecursionDepthMax) {
        encoder->SetError("recursion depth exceeded");
        return;
      }

      *encoder->template GetPtr<uintptr_t>(offset) = FIDL_ALLOC_PRESENT;

      size_t alloc_size = NaturalEncodingInlineSize<T, Constraint>(encoder);
      NaturalCodingTraits<T, Constraint>::Encode(encoder, value->get(), encoder->Alloc(alloc_size),
                                                 recursion_depth + 1);
    } else {
      *encoder->template GetPtr<uintptr_t>(offset) = FIDL_ALLOC_ABSENT;
    }
  }
  static void Decode(NaturalDecoder* decoder, std::unique_ptr<T>* value, size_t offset) {
    uintptr_t ptr = *decoder->template GetPtr<uintptr_t>(offset);
    if (!ptr)
      return value->reset();
    *value = std::make_unique<T>();
    NaturalCodingTraits<T, Constraint>::Decode(decoder, value->get(), decoder->GetOffset(ptr));
  }
};

template <typename T, typename Constraint>
struct NaturalCodingTraits<std::unique_ptr<T>, Constraint,
                           typename std::enable_if<IsUnion<T>::value>::type> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_xunion_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_xunion_v2_t);

  static void Encode(NaturalEncoder* encoder, std::unique_ptr<T>* value, size_t offset,
                     size_t recursion_depth) {
    if (*value) {
      NaturalCodingTraits<T, Constraint>::Encode(encoder, value->get(), offset, recursion_depth);
      return;
    }

    // Buffer is zero-initialized.
  }

  static void Decode(NaturalDecoder* decoder, std::unique_ptr<T>* value, size_t offset) {
    fidl_xunion_v2_t* u = decoder->template GetPtr<fidl_xunion_v2_t>(offset);
    if (FidlIsZeroEnvelope(&u->envelope)) {
      *value = nullptr;
      return;
    }
    *value = std::make_unique<T>();
    NaturalCodingTraits<T, Constraint>::Decode(decoder, value->get(), offset);
  }
};

template <typename Constraint>
struct NaturalCodingTraits<::std::string, Constraint> final {
  static constexpr size_t inline_size_old = sizeof(fidl_string_t);
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_string_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_string_t);
  static void Encode(NaturalEncoder* encoder, std::string* value, size_t offset,
                     size_t recursion_depth) {
    const size_t size = value->size();
    if (value->size() > Constraint::limit) {
      encoder->SetError("string limit exceeded");
      return;
    }
    zx_status_t status = fidl_validate_string(value->data(), value->size());
    if (status != ZX_OK) {
      encoder->SetError("string is not valid utf-8");
      return;
    }
    if (recursion_depth + 1 > kRecursionDepthMax) {
      encoder->SetError("recursion depth exceeded");
      return;
    }

    fidl_string_t* string = encoder->template GetPtr<fidl_string_t>(offset);
    string->size = size;
    string->data = reinterpret_cast<char*>(FIDL_ALLOC_PRESENT);
    size_t base = encoder->Alloc(size);
    char* payload = encoder->template GetPtr<char>(base);
    memcpy(payload, value->data(), size);
  }
  static void Decode(NaturalDecoder* decoder, std::string* value, size_t offset) {
    fidl_string_t* string = decoder->template GetPtr<fidl_string_t>(offset);
    ZX_ASSERT(string->data != nullptr);
    *value = std::string(string->data, string->size);
  }
};

template <typename Constraint>
struct NaturalCodingTraits<cpp17::optional<std::string>, Constraint> {
  static constexpr size_t inline_size_v1_no_ee = sizeof(fidl_string_t);
  static constexpr size_t inline_size_v2 = sizeof(fidl_string_t);

  static void Encode(NaturalEncoder* encoder, cpp17::optional<std::string>* value, size_t offset,
                     size_t recursion_depth) {
    if (value->has_value()) {
      fidl::internal::NaturalCodingTraits<std::string, Constraint>::Encode(encoder, &value->value(),
                                                                           offset, recursion_depth);
      return;
    }
    fidl_string_t* string = encoder->template GetPtr<fidl_string_t>(offset);
    string->size = 0;
    string->data = reinterpret_cast<char*>(FIDL_ALLOC_ABSENT);
  }
  static void Decode(NaturalDecoder* decoder, cpp17::optional<std::string>* value, size_t offset) {
    fidl_string_t* string = decoder->template GetPtr<fidl_string_t>(offset);
    if (string->data == nullptr) {
      ZX_ASSERT(string->size == 0);
      value->reset();
      return;
    }
    std::string unwrapped;
    fidl::internal::NaturalCodingTraits<std::string, Constraint>::Decode(decoder, &unwrapped,
                                                                         offset);
    value->emplace(unwrapped);
  }
};

#ifdef __Fuchsia__
template <typename T, typename Constraint>
struct NaturalCodingTraits<ClientEnd<T>, Constraint> {
  static void Encode(NaturalEncoder* encoder, ClientEnd<T>* value, size_t offset,
                     size_t recursion_depth) {
    encoder->EncodeHandle(value->TakeChannel().release(),
                          {
                              .obj_type = Constraint::obj_type,
                              .rights = Constraint::rights,
                          },
                          offset);
  }

  static void Decode(NaturalDecoder* decoder, ClientEnd<T>* value, size_t offset) {
    zx::channel channel;
    decoder->DecodeHandle(&channel, offset);
    *value = ClientEnd<T>(std::move(channel));
  }
};

template <typename T, typename Constraint>
struct NaturalCodingTraits<ServerEnd<T>, Constraint> {
  static void Encode(NaturalEncoder* encoder, ServerEnd<T>* value, size_t offset,
                     size_t recursion_depth) {
    encoder->EncodeHandle(value->TakeChannel().release(),
                          {
                              .obj_type = Constraint::obj_type,
                              .rights = Constraint::rights,
                          },
                          offset);
  }

  static void Decode(NaturalDecoder* decoder, ServerEnd<T>* value, size_t offset) {
    zx::channel channel;
    decoder->DecodeHandle(&channel, offset);
    *value = ServerEnd<T>(std::move(channel));
  }
};
#endif  // __Fuchsia__

template <typename Constraint, typename T>
void NaturalEncode(NaturalEncoder* encoder, T* value, size_t offset, size_t recursion_depth) {
  NaturalCodingTraits<T, Constraint>::Encode(encoder, value, offset, recursion_depth);
}

template <typename Constraint, typename T>
void NaturalDecode(NaturalDecoder* decoder, T* value, size_t offset) {
  NaturalCodingTraits<T, Constraint>::Decode(decoder, value, offset);
}

}  // namespace fidl::internal

#endif  // SRC_LIB_FIDL_CPP_INCLUDE_LIB_FIDL_CPP_NATURAL_CODING_TRAITS_H_
