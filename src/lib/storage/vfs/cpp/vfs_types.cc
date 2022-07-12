// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/lib/storage/vfs/cpp/vfs_types.h"

#include <fidl/fuchsia.io/cpp/wire.h>
#include <lib/fdio/vfs.h>
#include <lib/fit/function.h>

namespace fio = fuchsia_io;

namespace fs {

VnodeConnectionOptions VnodeConnectionOptions::FromIoV1Flags(
    fuchsia_io::wire::OpenFlags fidl_flags) {
  VnodeConnectionOptions options;

  // Flags:
  if (fidl_flags & fio::wire::OpenFlags::kCreate) {
    options.flags.create = true;
  }
  if (fidl_flags & fio::wire::OpenFlags::kCreateIfAbsent) {
    options.flags.fail_if_exists = true;
  }
  if (fidl_flags & fio::wire::OpenFlags::kTruncate) {
    options.flags.truncate = true;
  }
  if (fidl_flags & fio::wire::OpenFlags::kDirectory) {
    options.flags.directory = true;
  }
  if (fidl_flags & fio::wire::OpenFlags::kAppend) {
    options.flags.append = true;
  }
  if (fidl_flags & fio::wire::OpenFlags::kNodeReference) {
    options.flags.node_reference = true;
  }
  if (fidl_flags & fio::wire::OpenFlags::kDescribe) {
    options.flags.describe = true;
  }
  // Expand deprecated POSIX flag into new equivalents to maintain binary compatibility with
  // out-of-tree clients while still preventing rights escalations when crossing remote mounts.
  // TODO(fxbug.dev/81185): Remove kOpenFlagPosixDeprecated.
  if (fidl_flags & fio::wire::OpenFlags::kPosixDeprecated) {
    options.flags.posix_write = true;
    options.flags.posix_execute = true;
  }
  if (fidl_flags & fio::wire::OpenFlags::kPosixWritable) {
    options.flags.posix_write = true;
  }
  if (fidl_flags & fio::wire::OpenFlags::kPosixExecutable) {
    options.flags.posix_execute = true;
  }
  if (fidl_flags & fio::wire::OpenFlags::kNotDirectory) {
    options.flags.not_directory = true;
  }
  if (fidl_flags & fio::wire::OpenFlags::kCloneSameRights) {
    options.flags.clone_same_rights = true;
  }

  // Rights (these are smushed into |fidl_flags| in fuchsia.io v1):
  if (fidl_flags & fio::wire::OpenFlags::kRightReadable) {
    options.rights.read = true;
  }
  if (fidl_flags & fio::wire::OpenFlags::kRightWritable) {
    options.rights.write = true;
  }
  if (fidl_flags & fio::wire::OpenFlags::kRightExecutable) {
    options.rights.execute = true;
  }

  return options;
}

fuchsia_io::wire::OpenFlags VnodeConnectionOptions::ToIoV1Flags() const {
  fuchsia_io::wire::OpenFlags fidl_flags = {};

  // Flags:
  if (flags.create) {
    fidl_flags |= fio::wire::OpenFlags::kCreate;
  }
  if (flags.fail_if_exists) {
    fidl_flags |= fio::wire::OpenFlags::kCreateIfAbsent;
  }
  if (flags.truncate) {
    fidl_flags |= fio::wire::OpenFlags::kTruncate;
  }
  if (flags.directory) {
    fidl_flags |= fio::wire::OpenFlags::kDirectory;
  }
  if (flags.append) {
    fidl_flags |= fio::wire::OpenFlags::kAppend;
  }
  if (flags.node_reference) {
    fidl_flags |= fio::wire::OpenFlags::kNodeReference;
  }
  if (flags.describe) {
    fidl_flags |= fio::wire::OpenFlags::kDescribe;
  }
  if (flags.posix_write) {
    fidl_flags |= fio::wire::OpenFlags::kPosixWritable;
  }
  if (flags.posix_execute) {
    fidl_flags |= fio::wire::OpenFlags::kPosixExecutable;
  }
  if (flags.not_directory) {
    fidl_flags |= fio::wire::OpenFlags::kNotDirectory;
  }
  if (flags.clone_same_rights) {
    fidl_flags |= fio::wire::OpenFlags::kCloneSameRights;
  }

  // Rights (these are smushed into |fidl_flags| in fuchsia.io v1):
  if (rights.read) {
    fidl_flags |= fio::wire::OpenFlags::kRightReadable;
  }
  if (rights.write) {
    fidl_flags |= fio::wire::OpenFlags::kRightWritable;
  }
  if (rights.execute) {
    fidl_flags |= fio::wire::OpenFlags::kRightExecutable;
  }

  return fidl_flags;
}

VnodeConnectionOptions VnodeConnectionOptions::FilterForNewConnection(
    VnodeConnectionOptions options) {
  VnodeConnectionOptions result;
  result.flags.append = options.flags.append;
  result.flags.node_reference = options.flags.node_reference;
  result.rights = options.rights;
  return result;
}

fio::wire::NodeAttributes VnodeAttributes::ToIoV1NodeAttributes() const {
  return fio::wire::NodeAttributes{.mode = mode,
                                   .id = inode,
                                   .content_size = content_size,
                                   .storage_size = storage_size,
                                   .link_count = link_count,
                                   .creation_time = creation_time,
                                   .modification_time = modification_time};
}

void ConvertToIoV1NodeInfo(VnodeRepresentation representation,
                           fit::callback<void(fio::wire::NodeInfo&&)> callback) {
  representation.visit([&](auto&& repr) {
    using T = std::decay_t<decltype(repr)>;
    if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Connector>) {
      callback(fio::wire::NodeInfo::WithService({}));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::File>) {
      fio::wire::FileObject file = {.event = std::move(repr.observer),
                                    .stream = std::move(repr.stream)};
      callback(fio::wire::NodeInfo::WithFile(
          fidl::ObjectView<fio::wire::FileObject>::FromExternal(&file)));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Directory>) {
      callback(fio::wire::NodeInfo::WithDirectory({}));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Device>) {
      callback(fio::wire::NodeInfo::WithDevice({}));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Tty>) {
      callback(fio::wire::NodeInfo::WithTty({.event = std::move(repr.event)}));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::SynchronousDatagramSocket>) {
      callback(
          fio::wire::NodeInfo::WithSynchronousDatagramSocket({.event = std::move(repr.event)}));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::DatagramSocket>) {
      fio::wire::DatagramSocket datagram_socket = {
          .socket = std::move(repr.socket),
          .tx_meta_buf_size = repr.tx_meta_buf_size,
          .rx_meta_buf_size = repr.rx_meta_buf_size,
      };
      callback(fio::wire::NodeInfo::WithDatagramSocket(
          fidl::ObjectView<fio::wire::DatagramSocket>::FromExternal(&datagram_socket)));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::StreamSocket>) {
      callback(fio::wire::NodeInfo::WithStreamSocket({.socket = std::move(repr.socket)}));
    } else {
      ZX_PANIC("Representation variant is not initialized");
    }
  });
}

ConnectionInfoConverter::ConnectionInfoConverter(VnodeRepresentation representation) : info(arena) {
  representation.visit([&](auto&& repr) {
    using T = std::decay_t<decltype(repr)>;
    if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Connector>) {
      info.set_representation(arena, fio::wire::Representation::WithConnector(arena));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::File>) {
      fio::wire::FileInfo file(arena);
      file.set_observer(std::move(repr.observer));
      file.set_stream(std::move(repr.stream));
      info.set_representation(arena, fio::wire::Representation::WithFile(arena, std::move(file)));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Directory>) {
      info.set_representation(arena, fio::wire::Representation::WithDirectory(arena));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Device>) {
      fio::wire::DeviceInfo device(arena);
      info.set_representation(arena,
                              fio::wire::Representation::WithDevice(arena, std::move(device)));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::Tty>) {
      fio::wire::TtyInfo tty(arena);
      tty.set_event(std::move(repr.event));
      info.set_representation(arena, fio::wire::Representation::WithTty(arena, std::move(tty)));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::SynchronousDatagramSocket>) {
      fio::wire::SynchronousDatagramSocketInfo synchronous_datagram_socket(arena);
      synchronous_datagram_socket.set_event(std::move(repr.event));
      info.set_representation(arena, fio::wire::Representation::WithSynchronousDatagramSocket(
                                         arena, std::move(synchronous_datagram_socket)));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::DatagramSocket>) {
      fio::wire::DatagramSocketInfo datagram_socket(arena);
      datagram_socket.set_socket(std::move(repr.socket));
      datagram_socket.set_tx_meta_buf_size(arena, repr.tx_meta_buf_size);
      datagram_socket.set_rx_meta_buf_size(arena, repr.rx_meta_buf_size);
      info.set_representation(
          arena, fio::wire::Representation::WithDatagramSocket(arena, std::move(datagram_socket)));
    } else if constexpr (std::is_same_v<T, fs::VnodeRepresentation::StreamSocket>) {
      fio::wire::StreamSocketInfo stream_socket(arena);
      stream_socket.set_socket(std::move(repr.socket));
      info.set_representation(
          arena, fio::wire::Representation::WithStreamSocket(arena, std::move(stream_socket)));
    } else {
      ZX_PANIC("Representation variant is not initialized");
    }
  });
}

}  // namespace fs
