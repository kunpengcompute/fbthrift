/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstdint>

namespace folly {
namespace io {
class QueueAppender;
} // namespace io
} // namespace folly

namespace apache {
namespace thrift {
namespace detail {
namespace compact {

// -----------------------------------------------------------------------------
// Runtime dispatched varint-list encoders.
//
// These functions live in CompactProtocolSve.cpp. When the library is built
// with THRIFT_HAS_ARM_SVE2 (set by CMake when the THRIFT_ENABLE_ARM_SVE2
// option is on), that single translation unit is compiled with
// `-march=armv8-a+sve2+sve-bitperm` and contains the SVE2 kernels plus a
// runtime CPU-capability check. All other translation units (including
// downstream consumers like benchmarks/services) keep their default compile
// flags and reach the SVE2 fast path purely through these function symbols.
//
// When THRIFT_HAS_ARM_SVE2 is off, these entry points fall back to the
// scalar encoder - wire bytes are identical either way, and there is no
// runtime overhead beyond a single cached bool check.
// -----------------------------------------------------------------------------

// Returns true iff the host CPU supports SVE2 + SVE-BitPerm with VL == 256
// bits (svcntw() == 8, svcntd() == 4), and the library was built with SVE2
// support enabled. Cached on first call.
bool hasRuntimeSve2();

// Encode `size` signed 32-bit integers as zigzag varints into `out`.
// Produces exactly the same byte sequence as looping writeI32(data[i]).
std::uint32_t dispatchVarintEncode32(
    const std::int32_t* data,
    std::uint32_t size,
    folly::io::QueueAppender& out);

// Encode `size` signed 64-bit integers as zigzag varints into `out`.
// Produces exactly the same byte sequence as looping writeI64(data[i]).
std::uint32_t dispatchVarintEncode64(
    const std::int64_t* data,
    std::uint32_t size,
    folly::io::QueueAppender& out);

} // namespace compact
} // namespace detail
} // namespace thrift
} // namespace apache
