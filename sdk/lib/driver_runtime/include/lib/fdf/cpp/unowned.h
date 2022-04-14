// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_UNOWNED_H_
#define LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_UNOWNED_H_

namespace fdf {

// Wraps a handle to an object to provide type-safe access to its operations
// but does not take ownership of it.  The handle is not closed when the
// wrapper is destroyed.
//
// All use of Unowned<Object<T>> as an Object<T> is via a dereference operator,
// as illustrated below:
//
// void do_something(const fdf::Channel& channel);
//
// void example(fdf_handle_t channel_handle) {
//     do_something(*fdf::Unowned<Channel>(channel_handle));
// }
template <typename T>
class Unowned final {
 public:
  explicit Unowned(typename T::HandleType h) : value_(h) {}
  explicit Unowned(const T& owner) : Unowned(owner.get()) {}
  explicit Unowned(const Unowned& other) : Unowned(*other) {}
  constexpr Unowned() = default;
  Unowned(Unowned&& other) = default;

  ~Unowned() { release_value(); }

  Unowned& operator=(const Unowned& other) {
    if (&other == this) {
      return *this;
    }

    *this = Unowned(other);
    return *this;
  }
  Unowned& operator=(Unowned&& other) {
    release_value();
    value_ = static_cast<T&&>(other.value_);
    return *this;
  }

  T& operator*() { return value_; }
  const T& operator*() const { return value_; }
  T* operator->() { return &value_; }
  const T* operator->() const { return &value_; }

 private:
  void release_value() { value_.release(); }

  T value_;
};

}  // namespace fdf

#endif  // LIB_DRIVER_RUNTIME_INCLUDE_LIB_FDF_CPP_UNOWNED_H_
