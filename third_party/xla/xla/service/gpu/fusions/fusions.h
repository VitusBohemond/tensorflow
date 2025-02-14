/* Copyright 2023 The TensorFlow Authors. All Rights Reserved.

Licensed under the Apache License, Version 2.0 (the "License");
you may not use this file except in compliance with the License.
You may obtain a copy of the License at

    http://www.apache.org/licenses/LICENSE-2.0

Unless required by applicable law or agreed to in writing, software
distributed under the License is distributed on an "AS IS" BASIS,
WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
See the License for the specific language governing permissions and
limitations under the License.
==============================================================================*/
#ifndef XLA_SERVICE_GPU_FUSIONS_FUSIONS_H_
#define XLA_SERVICE_GPU_FUSIONS_FUSIONS_H_

#include <memory>
#include <optional>

#include "absl/types/span.h"
#include "xla/hlo/ir/hlo_instructions.h"
#include "xla/mlir_hlo/lhlo/IR/lhlo_ops.h"
#include "xla/service/buffer_assignment.h"
#include "xla/service/gpu/fusions/fusion_emitter.h"
#include "xla/service/gpu/hlo_fusion_analysis.h"
#include "xla/statusor.h"

namespace xla {
namespace gpu {

class FusionInfo {
 public:
  explicit FusionInfo(const HloFusionAnalysis& analysis)
      : analysis_(analysis) {}
  virtual ~FusionInfo() = default;

  const HloFusionAnalysis& analysis() const { return analysis_; }

  // If the fusion is a DUS fusion, returns whether it can be emitted in place.
  // Undefined if the fusion is not a DUS fusion.
  virtual bool CanEmitDynamicUpdateSliceInPlace() const = 0;

  // Attempts to create a memcpy fusion, if possible. Returns nullopt if the
  // fusion failed to pattern match. Returns an error if the fusion successfully
  // pattern matched, but buffer assignment failed.
  // TODO(b/204548848): Find a proper abstraction for this once LMHLO is gone.
  virtual std::optional<absl::StatusOr<std::unique_ptr<FusionInterface>>>
  GetCopyFusion() const = 0;

 private:
  const HloFusionAnalysis& analysis_;
};

class LmhloFusionInfo : public FusionInfo {
 public:
  LmhloFusionInfo(const HloFusionAnalysis& analysis,
                  mlir::lmhlo::FusionOp fusion_op,
                  absl::Span<const BufferAllocation* const> allocations)
      : FusionInfo(analysis),
        fusion_op_(fusion_op),
        allocations_(allocations) {}

  bool CanEmitDynamicUpdateSliceInPlace() const override;
  std::optional<absl::StatusOr<std::unique_ptr<FusionInterface>>>
  GetCopyFusion() const override;

 private:
  mlir::lmhlo::FusionOp fusion_op_;
  absl::Span<const BufferAllocation* const> allocations_;
};

class HloFusionInfo : public FusionInfo {
 public:
  HloFusionInfo(const HloFusionAnalysis& analysis,
                const HloFusionInstruction* instr,
                const BufferAssignment* buffer_assignment)
      : FusionInfo(analysis),
        instr_(instr),
        buffer_assignment_(buffer_assignment) {}

  bool CanEmitDynamicUpdateSliceInPlace() const override;
  std::optional<absl::StatusOr<std::unique_ptr<FusionInterface>>>
  GetCopyFusion() const override;

 private:
  const HloFusionInstruction* instr_;
  const BufferAssignment* buffer_assignment_;
};

class PreBufferAssignmentFusionInfo : public FusionInfo {
 public:
  explicit PreBufferAssignmentFusionInfo(const HloFusionAnalysis& analysis)
      : FusionInfo(analysis) {}

  bool CanEmitDynamicUpdateSliceInPlace() const override {
    // Optimistically assume all DUS fusions are in-place.
    return true;
  }

  std::optional<absl::StatusOr<std::unique_ptr<FusionInterface>>>
  GetCopyFusion() const override {
    // Copy fusions can't be created without buffer assignment. Note:
    // technically, this is only needed to generate the chunk, the validation
    // itself could be done without a buffer assignment. However, we currently
    // have no use for this, so it's OK to always fall back to the loop fusion.
    return std::nullopt;
  }
};

// Returns the emitter for the given fusion. Returns nullopt if the fusion
// type is not yet supported.
absl::StatusOr<std::unique_ptr<FusionInterface>> GetFusionEmitter(
    const FusionInfo& fusion_info);

}  // namespace gpu
}  // namespace xla

#endif  // XLA_SERVICE_GPU_FUSIONS_FUSIONS_H_
