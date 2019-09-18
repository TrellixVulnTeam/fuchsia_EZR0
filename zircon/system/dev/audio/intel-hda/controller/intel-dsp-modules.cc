// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "intel-dsp-modules.h"

#include <cstdint>

#include <fbl/span.h>
#include <fbl/string_printf.h>
#include <intel-hda/utils/intel-audio-dsp-ipc.h>
#include <intel-hda/utils/intel-hda-registers.h>
#include <intel-hda/utils/utils.h>

#include "debug-logging.h"
#include "intel-dsp-ipc.h"

namespace audio {
namespace intel_hda {

// Maximum number of instances of a particular module or pipelines we
// will allocate before producing an error.
//
// In practice, the DSP will likely fail creation far before we reach
// this number.
constexpr int kMaxInstancesPerModule = 255;
constexpr int kMaxPipelines = 255;

namespace {
zx_status_t dsp_to_zx_status(MsgStatus status) {
  return (status == MsgStatus::IPC_SUCCESS) ? ZX_OK : ZX_ERR_INTERNAL;
}
}  // namespace

zx_status_t DspLargeConfigGet(DspChannel* ipc, uint16_t module_id, uint8_t instance_id,
                              BaseFWParamType large_param_id, fbl::Span<uint8_t> buffer,
                              size_t* bytes_received) {
  GLOBAL_LOG(DEBUG1, "LARGE_CONFIG_GET (mod %u inst %u large_param_id %u)\n", module_id,
             instance_id, to_underlying(large_param_id));

  if (buffer.size_bytes() > IPC_EXT_DATA_OFF_MAX_SIZE) {
    buffer = buffer.subspan(0, IPC_EXT_DATA_OFF_MAX_SIZE);
  }

  size_t bytes_received_local;
  Status result =
      ipc->SendWithData(IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST,
                                ModuleMsgType::LARGE_CONFIG_GET, instance_id, module_id),
                        IPC_LARGE_CONFIG_EXT(true, false, to_underlying(large_param_id),
                                             static_cast<uint32_t>(buffer.size())),
                        fbl::Span<const uint8_t>(), buffer, &bytes_received_local);
  if (!result.ok()) {
    GLOBAL_LOG(ERROR, "LARGE_CONFIG_GET (mod %u inst %u large_param_id %u) failed: %s\n", module_id,
               instance_id, to_underlying(large_param_id), result.ToString().c_str());
    return result.code();
  }

  GLOBAL_LOG(DEBUG1,
             "LARGE_CONFIG_GET (mod %u inst %u large_param_id %u) success: received %ld byte(s).\n",
             module_id, instance_id, to_underlying(large_param_id), bytes_received_local);
  if (bytes_received != nullptr) {
    *bytes_received = bytes_received_local;
  }
  return ZX_OK;
}

DspModuleController::DspModuleController(DspChannel* channel) : channel_(channel) {}

// Create an instance of the module "type".
//
// Returns the ID of the created module on success.
StatusOr<DspModuleId> DspModuleController::CreateModule(DspModuleType type,
                                                        DspPipelineId parent_pipeline,
                                                        ProcDomain scheduling_domain,
                                                        fbl::Span<const uint8_t> data) {
  // Ensure data is not too large.
  if (data.size() >= std::numeric_limits<uint16_t>::max()) {
    return Status(ZX_ERR_INVALID_ARGS);
  }

  // Allocate an ID.
  StatusOr<uint8_t> instance_id = AllocateInstanceId(type);
  if (!instance_id.ok()) {
    return instance_id.status();
  }
  GLOBAL_LOG(DEBUG1, "CreateModule(type %u, inst %u)\n", type, instance_id.ValueOrDie());

  // Create the module.
  Status result = channel_->SendWithData(
      IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST, ModuleMsgType::INIT_INSTANCE,
              instance_id.ValueOrDie(), type),
      IPC_INIT_INSTANCE_EXT(scheduling_domain, /*core_id=*/0, parent_pipeline.id,
                            static_cast<uint16_t>(data.size())),
      data, fbl::Span<uint8_t>(), nullptr);
  if (!result.ok()) {
    GLOBAL_LOG(DEBUG1, "CreateModule failed: %s\n", result.ToString().c_str());
    return PrependMessage(fbl::StringPrintf("Failed to create module of type %u (instance #%u)",
                                            type, instance_id.ValueOrDie()),
                          result);
  }

  return DspModuleId{/*type=*/type, /*id=*/instance_id.ValueOrDie()};
}

// Create a pipeline.
//
// Return ths ID of the created pipeline on success.
StatusOr<DspPipelineId> DspModuleController::CreatePipeline(uint8_t priority, uint16_t memory_pages,
                                                            bool low_power) {
  // Allocate a pipeline name.
  if (pipelines_allocated_ >= kMaxPipelines) {
    return Status(ZX_ERR_NO_RESOURCES, "Too many pipelines created.");
  }
  uint8_t id = pipelines_allocated_++;
  GLOBAL_LOG(DEBUG1, "CreatePipeline(inst %u)\n", id);

  // Create the pipeline.
  Status result = channel_->Send(IPC_CREATE_PIPELINE_PRI(id, priority, memory_pages),
                                 IPC_CREATE_PIPELINE_EXT(low_power));
  if (!result.ok()) {
    GLOBAL_LOG(DEBUG1, "CreatePipeline failed: %s", result.ToString().c_str());
    return PrependMessage(fbl::StringPrintf("Failed to create pipeline #%u", id), result);
  }

  return DspPipelineId{id};
}

// Connect an output pin of one module to the input pin of another.
Status DspModuleController::BindModules(DspModuleId source_module, uint8_t src_output_pin,
                                        DspModuleId dest_module, uint8_t dest_input_pin) {
  GLOBAL_LOG(DEBUG1, "BindModules (mod %u inst %u):%u --> (mod %u, inst %u):%u\n",
             source_module.type, source_module.id, src_output_pin, dest_module.type, dest_module.id,
             dest_input_pin);

  Status result = channel_->Send(
      IPC_PRI(MsgTarget::MODULE_MSG, MsgDir::MSG_REQUEST, ModuleMsgType::BIND, source_module.id,
              source_module.type),
      IPC_BIND_UNBIND_EXT(dest_module.type, dest_module.id, dest_input_pin, src_output_pin));
  if (!result.ok()) {
    GLOBAL_LOG(DEBUG1, "BindModules failed: %s", result.ToString().c_str());
    return result;
  }

  return result;
}

// Enable/disable the given pipeline.
Status DspModuleController::SetPipelineState(DspPipelineId pipeline, PipelineState state,
                                             bool sync_stop_start) {
  GLOBAL_LOG(DEBUG1, "SetPipelineStatus(pipeline=%u, state=%u, sync_stop_start=%s)\n", pipeline.id,
             static_cast<unsigned int>(state), sync_stop_start ? "true" : "false");

  Status result = channel_->Send(IPC_SET_PIPELINE_STATE_PRI(pipeline.id, state),
                                 IPC_SET_PIPELINE_STATE_EXT(false, sync_stop_start));
  if (!result.ok()) {
    GLOBAL_LOG(DEBUG1, "SetPipelineStatus failed: %s", result.ToString().c_str());
    return result;
  }

  return result;
}

StatusOr<uint8_t> DspModuleController::AllocateInstanceId(DspModuleType type) {
  uint8_t& instance_count = allocated_instances_[type];
  if (instance_count >= kMaxInstancesPerModule) {
    return Status(ZX_ERR_NO_RESOURCES, "Could not allocate more instances of given module type.");
  }
  uint8_t result = instance_count;
  instance_count++;
  return result;
}

StatusOr<DspPipelineId> CreateSimplePipeline(DspModuleController* controller,
                                             std::initializer_list<DspModule> modules) {
  // Create a pipeline.
  //
  // TODO(fxb/31426): Calculate actual memory usage.
  const uint16_t pipeline_memory_pages_needed = 4;
  StatusOr<DspPipelineId> pipeline_or_err =
      controller->CreatePipeline(/*pipeline_priority=*/0,
                                 /*pipeline_memory_pages=*/pipeline_memory_pages_needed,
                                 /*low_power=*/true);
  if (!pipeline_or_err.ok()) {
    return PrependMessage("Could not create pipeline", pipeline_or_err.status());
  }
  DspPipelineId pipeline = pipeline_or_err.ValueOrDie();

  // Create the modules.
  int module_count = 0;
  DspModuleId prev_module;
  for (const DspModule& module : modules) {
    // Create the module.
    StatusOr<DspModuleId> id =
        controller->CreateModule(module.type, pipeline, ProcDomain::LOW_LATENCY, module.data);
    if (!id.ok()) {
      return PrependMessage(fbl::StringPrintf("Failed creating module #%u.", module_count),
                            id.status());
    }

    // Join it to the previous module.
    if (module_count > 0) {
      Status result = controller->BindModules(prev_module, /*src_output_pin=*/0, id.ValueOrDie(),
                                              /*dest_input_pin=*/0);
      if (!result.ok()) {
        return PrependMessage(fbl::StringPrintf("Failed to connect module #%u to #%u",
                                                module_count - 1, module_count),
                              result);
      }
    }

    prev_module = id.ValueOrDie();
    module_count++;
  }

  return pipeline;
}

}  // namespace intel_hda
}  // namespace audio
