// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "apps/ledger/cloud_provider_firebase/page_cloud_impl.h"

#include "apps/ledger/src/convert/convert.h"
#include "lib/fxl/functional/make_copyable.h"

namespace cloud_provider_firebase {

namespace {
// TODO(ppi): drop internal status and use cloud_provider::Status everywhere
// inside cloud_provider_firebase.
cloud_provider::Status ConvertInternalStatus(Status status) {
  switch (status) {
    case Status::OK:
      return cloud_provider::Status::OK;
    case Status::ARGUMENT_ERROR:
      return cloud_provider::Status::ARGUMENT_ERROR;
    case Status::NETWORK_ERROR:
      return cloud_provider::Status::NETWORK_ERROR;
    case Status::NOT_FOUND:
      return cloud_provider::Status::NOT_FOUND;
    case Status::INTERNAL_ERROR:
      return cloud_provider::Status::INTERNAL_ERROR;
    case Status::PARSE_ERROR:
      return cloud_provider::Status::PARSE_ERROR;
    case Status::SERVER_ERROR:
      return cloud_provider::Status::SERVER_ERROR;
  }
}

}  // namespace

PageCloudImpl::PageCloudImpl(
    auth_provider::AuthProvider* auth_provider,
    std::unique_ptr<firebase::Firebase> firebase,
    std::unique_ptr<gcs::CloudStorage> cloud_storage,
    std::unique_ptr<PageCloudHandler> handler,
    fidl::InterfaceRequest<cloud_provider::PageCloud> request)
    : auth_provider_(auth_provider),
      firebase_(std::move(firebase)),
      cloud_storage_(std::move(cloud_storage)),
      handler_(std::move(handler)),
      binding_(this, std::move(request)) {
  FXL_DCHECK(auth_provider_);
  // The class shuts down when the client connection is disconnected.
  binding_.set_connection_error_handler([this] {
    if (on_empty_) {
      on_empty_();
    }
  });
}

PageCloudImpl::~PageCloudImpl() {}

void PageCloudImpl::AddCommits(fidl::Array<cloud_provider::CommitPtr> commits,
                               const AddCommitsCallback& callback) {
  auto request = auth_provider_->GetFirebaseToken(fxl::MakeCopyable([
    this, commits = std::move(commits), callback
  ](auth_provider::AuthStatus auth_status, std::string auth_token) mutable {
    if (auth_status != auth_provider::AuthStatus::OK) {
      callback(cloud_provider::Status::AUTH_ERROR);
      return;
    }

    std::vector<Commit> handler_commits;
    for (auto& commit : commits) {
      handler_commits.emplace_back(convert::ToString(commit->id),
                                   convert::ToString(commit->data));
    }

    handler_->AddCommits(std::move(auth_token), std::move(handler_commits),
                         [callback = std::move(callback)](Status status) {
                           callback(ConvertInternalStatus(status));
                         });
  }));
  auth_token_requests_.emplace(request);
}

void PageCloudImpl::GetCommits(fidl::Array<uint8_t> min_position_token,
                               const GetCommitsCallback& callback) {
  auto request = auth_provider_->GetFirebaseToken(fxl::MakeCopyable([
    this, min_timestamp = convert::ToString(min_position_token), callback
  ](auth_provider::AuthStatus auth_status, std::string auth_token) mutable {
    if (auth_status != auth_provider::AuthStatus::OK) {
      callback(cloud_provider::Status::AUTH_ERROR, nullptr, nullptr);
      return;
    }

    handler_->GetCommits(
        std::move(auth_token), min_timestamp,
        [callback = std::move(callback)](Status status,
                                         std::vector<Record> records) {
          if (status != Status::OK) {
            callback(ConvertInternalStatus(status), nullptr, nullptr);
            return;
          }

          auto commits = fidl::Array<cloud_provider::CommitPtr>::New(0);
          if (records.empty()) {
            callback(ConvertInternalStatus(status), std::move(commits),
                     nullptr);
            return;
          }

          for (auto& record : records) {
            cloud_provider::CommitPtr commit = cloud_provider::Commit::New();
            commit->id = convert::ToArray(record.commit.id);
            commit->data = convert::ToArray(record.commit.content);
            commits.push_back(std::move(commit));
          }
          fidl::Array<uint8_t> position_token =
              convert::ToArray(records.back().timestamp);
          callback(ConvertInternalStatus(status), std::move(commits),
                   std::move(position_token));
        });
  }));
  auth_token_requests_.emplace(request);
}

void PageCloudImpl::AddObject(fidl::Array<uint8_t> id,
                              zx::vmo data,
                              const AddObjectCallback& callback) {
  auto request = auth_provider_->GetFirebaseToken(fxl::MakeCopyable([
    this, id = convert::ToString(id), data = std::move(data), callback
  ](auth_provider::AuthStatus auth_status, std::string auth_token) mutable {
    if (auth_status != auth_provider::AuthStatus::OK) {
      callback(cloud_provider::Status::AUTH_ERROR);
      return;
    }

    handler_->AddObject(
        std::move(auth_token), std::move(id),
        std::move(data), [callback = std::move(callback)](Status status) {
          callback(ConvertInternalStatus(status));
        });
  }));
  auth_token_requests_.emplace(request);
}

void PageCloudImpl::GetObject(fidl::Array<uint8_t> id,
                              const GetObjectCallback& callback) {
  auto request = auth_provider_->GetFirebaseToken([
    this, id = convert::ToString(id), callback
  ](auth_provider::AuthStatus auth_status, std::string auth_token) mutable {
    if (auth_status != auth_provider::AuthStatus::OK) {
      callback(cloud_provider::Status::AUTH_ERROR, 0u, zx::socket());
      return;
    }

    handler_->GetObject(std::move(auth_token), std::move(id),
                        [callback = std::move(callback)](
                            Status status, uint64_t size, zx::socket data) {
                          callback(ConvertInternalStatus(status), size,
                                   std::move(data));
                        });
  });
  auth_token_requests_.emplace(request);
}

void PageCloudImpl::SetWatcher(
    fidl::InterfaceHandle<cloud_provider::PageCloudWatcher> /*watcher*/,
    fidl::Array<uint8_t> /*min_position_token*/,
    const SetWatcherCallback& callback) {
  FXL_NOTIMPLEMENTED();
  callback(cloud_provider::Status::INTERNAL_ERROR);
}

}  // namespace cloud_provider_firebase
