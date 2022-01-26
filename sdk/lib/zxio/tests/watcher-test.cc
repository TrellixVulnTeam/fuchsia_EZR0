// Copyright 2021 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fidl/fuchsia.io/cpp/wire.h>
#include <fidl/fuchsia.io/cpp/wire_test_base.h>
#include <lib/async-loop/cpp/loop.h>
#include <lib/async-loop/default.h>
#include <lib/fidl-async/cpp/bind.h>
#include <lib/zx/time.h>
#include <lib/zxio/watcher.h>
#include <lib/zxio/zxio.h>

#include <vector>

#include <fbl/unique_fd.h>
#include <zxtest/zxtest.h>

TEST(WatcherTest, WatchInvalidObject) {
  ASSERT_STATUS(zxio_watch_directory(nullptr, nullptr, ZX_TIME_INFINITE, nullptr),
                ZX_ERR_BAD_HANDLE);
}

template <typename F>
class Server final : public fidl::testing::WireTestBase<fuchsia_io::Directory> {
 public:
  explicit Server(F on_watch) : on_watch_(std::move(on_watch)) {}

  void NotImplemented_(const std::string& name, fidl::CompleterBase& completer) override {
    ADD_FAILURE("%s should not be called", name.c_str());
    completer.Close(ZX_ERR_NOT_SUPPORTED);
  }

  void Close(CloseRequestView request, CloseCompleter::Sync& completer) override {
    completer.Reply(ZX_OK);
    completer.Close(ZX_OK);
  }

  void Close2(Close2RequestView request, Close2Completer::Sync& completer) override {
    completer.ReplySuccess();
    completer.Close(ZX_OK);
  }

  void Describe(DescribeRequestView request, DescribeCompleter::Sync& completer) override {
    fuchsia_io::wire::DirectoryObject directory;
    completer.Reply(fuchsia_io::wire::NodeInfo::WithDirectory(std::move(directory)));
  }

  void Watch(WatchRequestView request, WatchCompleter::Sync& completer) override {
    on_watch_(request->mask, request->options, std::move(request->watcher), completer);
  }

 private:
  F on_watch_;
};

TEST(WatcherTest, WatchInvalidCallback) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(endpoints.status_value());

  Server server([](uint32_t maks, uint32_t options, zx::channel watcher,
                   fidl::WireServer<fuchsia_io::Directory>::WatchCompleter::Sync& completer) {});

  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(endpoints->server), &server));
  ASSERT_OK(loop.StartThread("fake-directory-server"));

  zxio_storage_t storage;
  ASSERT_OK(zxio_create(endpoints->client.channel().release(), &storage));
  zxio_t* io = &storage.io;

  ASSERT_STATUS(zxio_watch_directory(io, nullptr, ZX_TIME_INFINITE, nullptr), ZX_ERR_INVALID_ARGS);

  ASSERT_OK(zxio_close(io));
}

TEST(WatcherTest, Smoke) {
  auto endpoints = fidl::CreateEndpoints<fuchsia_io::Directory>();
  ASSERT_OK(endpoints.status_value());

  Server server([](uint32_t mask, uint32_t options, zx::channel watcher,
                   fidl::WireServer<fuchsia_io::Directory>::WatchCompleter::Sync& completer) {
    uint8_t bytes[fuchsia_io::wire::kMaxBuf];
    auto it = std::begin(bytes);

    {
      constexpr char name[] = "unsupported";
      *it++ = fuchsia_io::wire::kWatchEventIdle + 1;
      *it++ = sizeof(name);
      it = std::copy(std::cbegin(name), std::cend(name), it);
    }
    {
      constexpr char name[] = "valid";
      *it++ = fuchsia_io::wire::kWatchEventAdded;
      *it++ = sizeof(name);
      it = std::copy(std::cbegin(name), std::cend(name), it);
    }
    {
      // Incomplete; event without name.
      *it++ = fuchsia_io::wire::kWatchEventAdded;
      *it++ = 1;
    }

    completer.Reply(watcher.write(
        0, bytes, static_cast<uint32_t>(std::distance(std::begin(bytes), it)), nullptr, 0));
  });
  async::Loop loop(&kAsyncLoopConfigNoAttachToCurrentThread);
  ASSERT_OK(fidl::BindSingleInFlightOnly(loop.dispatcher(), std::move(endpoints->server), &server));
  ASSERT_OK(loop.StartThread("fake-directory-server"));

  zxio_storage_t storage;
  ASSERT_OK(zxio_create(endpoints->client.channel().release(), &storage));
  zxio_t* io = &storage.io;

  std::vector<std::pair<zxio_watch_directory_event_t, std::string>> events;
  ASSERT_STATUS(zxio_watch_directory(
                    io,
                    [](zxio_watch_directory_event_t event, const char* name, void* cookie) {
                      auto events_cookie = reinterpret_cast<decltype(events)*>(cookie);
                      events_cookie->emplace_back(event, std::string(name));
                      return ZX_OK;
                    },
                    zx::time::infinite().get(), &events),
                ZX_ERR_PEER_CLOSED);
  decltype(events) expected_events = {{ZXIO_WATCH_EVENT_ADD_FILE, "valid"}};
  ASSERT_EQ(events, expected_events);

  ASSERT_OK(zxio_close(io));
}
