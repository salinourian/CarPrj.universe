// Copyright 2022 TIER IV, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef IMAGE_PROJECTION_BASED_FUSION__POINTPAINTING_FUSION__PREPROCESS_KERNEL_HPP_
#define IMAGE_PROJECTION_BASED_FUSION__POINTPAINTING_FUSION__PREPROCESS_KERNEL_HPP_

#include <cuda.h>
#include <cuda_runtime_api.h>

namespace image_projection_based_fusion
{
cudaError_t generateFeatures_launch(
  const float * voxel_features, const float * voxel_num_points, const int * coords,
  const std::size_t num_voxels, const std::size_t max_voxel_size, const float voxel_size_x,
  const float voxel_size_y, const float voxel_size_z, const float range_min_x,
  const float range_min_y, const float range_min_z, float * features,
  const std::size_t encoder_in_feature_size, cudaStream_t stream);

}  // namespace image_projection_based_fusion

#endif  // IMAGE_PROJECTION_BASED_FUSION__POINTPAINTING_FUSION__PREPROCESS_KERNEL_HPP_
