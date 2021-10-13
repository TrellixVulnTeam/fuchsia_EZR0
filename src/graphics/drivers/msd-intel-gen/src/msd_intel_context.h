// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MSD_INTEL_CONTEXT_H
#define MSD_INTEL_CONTEXT_H

#include <map>
#include <memory>
#include <queue>
#include <thread>

#include "command_buffer.h"
#include "magma_util/semaphore_port.h"
#include "magma_util/status.h"
#include "msd.h"
#include "msd_intel_buffer.h"
#include "platform_logger.h"
#include "ppgtt.h"
#include "ringbuffer.h"
#include "types.h"

class MsdIntelConnection;

// Base context, not tied to a connection.
class MsdIntelContext {
 public:
  MsdIntelContext(std::shared_ptr<AddressSpace> address_space) : address_space_(address_space) {
    DASSERT(address_space_);
  }

  virtual ~MsdIntelContext() {}

  void SetEngineState(EngineCommandStreamerId id, std::unique_ptr<MsdIntelBuffer> context_buffer,
                      std::unique_ptr<Ringbuffer> ringbuffer);

  virtual bool Map(std::shared_ptr<AddressSpace> address_space, EngineCommandStreamerId id);
  virtual bool Unmap(EngineCommandStreamerId id);

  virtual std::weak_ptr<MsdIntelConnection> connection() {
    return std::weak_ptr<MsdIntelConnection>();
  }

  virtual bool killed() { return false; }

  virtual void Kill() { MAGMA_LOG(WARNING, "Attempted to kill a base context"); }

  // Gets the gpu address of the context buffer if mapped.
  bool GetGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out);
  bool GetRingbufferGpuAddress(EngineCommandStreamerId id, gpu_addr_t* addr_out);

  MsdIntelBuffer* get_context_buffer(EngineCommandStreamerId id) {
    auto iter = state_map_.find(id);
    return iter == state_map_.end() ? nullptr : iter->second.context_buffer.get();
  }

  void* GetCachedContextBufferCpuAddr(EngineCommandStreamerId id) {
    auto iter = state_map_.find(id);
    if (iter == state_map_.end())
      return nullptr;
    if (!iter->second.context_buffer_cpu_addr) {
      MsdIntelBuffer* context_buffer = iter->second.context_buffer.get();
      if (!context_buffer)
        return nullptr;
      if (!context_buffer->platform_buffer()->MapCpu(&iter->second.context_buffer_cpu_addr))
        return DRETP(nullptr, "Failed to map context buffer");
    }
    return iter->second.context_buffer_cpu_addr;
  }

  Ringbuffer* get_ringbuffer(EngineCommandStreamerId id) {
    auto iter = state_map_.find(id);
    return iter == state_map_.end() ? nullptr : iter->second.ringbuffer.get();
  }

  bool IsInitializedForEngine(EngineCommandStreamerId id) {
    return state_map_.find(id) != state_map_.end();
  }

  std::queue<std::unique_ptr<MappedBatch>>& pending_batch_queue() { return pending_batch_queue_; }

  std::shared_ptr<AddressSpace> exec_address_space() { return address_space_; }

 private:
  struct PerEngineState {
    std::shared_ptr<MsdIntelBuffer> context_buffer;
    std::unique_ptr<GpuMapping> context_mapping;
    std::unique_ptr<Ringbuffer> ringbuffer;
    gpu_addr_t ringbuffer_gpu_addr = 0;
    void* context_buffer_cpu_addr = nullptr;
  };

  std::map<EngineCommandStreamerId, PerEngineState> state_map_;
  std::queue<std::unique_ptr<MappedBatch>> pending_batch_queue_;
  std::shared_ptr<AddressSpace> address_space_;

  friend class TestContext;
};

class ClientContext : public MsdIntelContext {
 public:
  ClientContext(std::weak_ptr<MsdIntelConnection> connection,
                std::shared_ptr<AddressSpace> address_space)
      : MsdIntelContext(std::move(address_space)), connection_(connection) {}

  ~ClientContext();

  magma::Status SubmitCommandBuffer(std::unique_ptr<CommandBuffer> cmd_buf);
  magma::Status SubmitBatch(std::unique_ptr<MappedBatch> batch);

  void Shutdown();

  std::weak_ptr<MsdIntelConnection> connection() override { return connection_; }

  bool killed() override { return killed_; }

  void Kill() override;

 private:
  magma::Status SubmitBatchLocked() MAGMA_REQUIRES(presubmit_mutex_);

  std::weak_ptr<MsdIntelConnection> connection_;
  std::unique_ptr<magma::SemaphorePort> semaphore_port_;
  std::thread wait_thread_;
  std::mutex presubmit_mutex_;
  std::queue<std::unique_ptr<MappedBatch>> presubmit_queue_ MAGMA_GUARDED(presubmit_mutex_);
  bool killed_ = false;
};

class MsdIntelAbiContext : public msd_context_t {
 public:
  MsdIntelAbiContext(std::shared_ptr<ClientContext> ptr) : ptr_(std::move(ptr)) { magic_ = kMagic; }

  static MsdIntelAbiContext* cast(msd_context_t* context) {
    DASSERT(context);
    DASSERT(context->magic_ == kMagic);
    return static_cast<MsdIntelAbiContext*>(context);
  }
  std::shared_ptr<ClientContext> ptr() { return ptr_; }

 private:
  std::shared_ptr<ClientContext> ptr_;
  static const uint32_t kMagic = 0x63747874;  // "ctxt"
};

#endif  // MSD_INTEL_CONTEXT_H
