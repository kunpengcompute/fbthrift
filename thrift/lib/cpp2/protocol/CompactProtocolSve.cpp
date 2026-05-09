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

// This translation unit is the ONLY place in fbthrift that should be compiled
// with `-march=armv8-a+sve2+sve-bitperm`. The SVE2 kernels are exposed through
// the dispatch functions declared in CompactProtocolSveDispatch.h; every other
// TU (including downstream consumers like benchmarks) keeps its default
// compile flags and reaches SVE2 solely through these exported symbols.
//
// The CMake build sets THRIFT_HAS_ARM_SVE2 only when the user opts in via
// -DTHRIFT_ENABLE_ARM_SVE2=ON. When it is not set, this file compiles a
// scalar-only fallback with zero additional requirements.

#include <thrift/lib/cpp2/protocol/detail/CompactProtocolSveDispatch.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include <folly/io/Cursor.h>

#include <thrift/lib/cpp/util/VarintUtils.h>

#if defined(THRIFT_HAS_ARM_SVE2)
#include <arm_sve.h>
#if defined(__linux__)
#include <sys/auxv.h>
#ifndef HWCAP2_SVE2
#define HWCAP2_SVE2 (1UL << 1)
#endif
#ifndef HWCAP2_SVEBITPERM
#define HWCAP2_SVEBITPERM (1UL << 4)
#endif
#endif // __linux__
#endif // THRIFT_HAS_ARM_SVE2

namespace apache {
namespace thrift {
namespace detail {
namespace compact {

namespace {

// Scalar fallback. Same bytes and call pattern as the inline per-element
// path, so this produces no regression vs. pre-patch codegen.
uint32_t varintEncodeScalar32(
    const int32_t* data, uint32_t size, folly::io::QueueAppender& out) {
  uint32_t xfer = 0;
  for (uint32_t i = 0; i < size; ++i) {
    xfer += apache::thrift::util::writeVarint(
        out, apache::thrift::util::i32ToZigzag(data[i]));
  }
  return xfer;
}

uint32_t varintEncodeScalar64(
    const int64_t* data, uint32_t size, folly::io::QueueAppender& out) {
  uint32_t xfer = 0;
  for (uint32_t i = 0; i < size; ++i) {
    xfer += apache::thrift::util::writeVarint(
        out, apache::thrift::util::i64ToZigzag(data[i]));
  }
  return xfer;
}

#if defined(THRIFT_HAS_ARM_SVE2)

// ---------------------------------------------------------------------------
// SVE2 + SVE-BitPerm kernels. Ported from the protobuf implementation, but
// adapted for folly::io::QueueAppender (we reserve an upper bound per batch,
// write directly to the raw writable bytes, and commit the exact used size
// via out.append()).
//
// Both kernels assume VL == 256 bits (svcntw() == 8 / svcntd() == 4). The
// runtime check below guarantees they are never entered on a mismatched VL.
// ---------------------------------------------------------------------------

template <class T>
uint32_t varintEncode32SveKernel(
    const T* loc, uint32_t size, folly::io::QueueAppender& out) {
  static_assert(
      std::is_same<T, int32_t>::value || std::is_same<T, uint32_t>::value,
      "T must be int32_t or uint32_t");

  uint32_t written = 0;
  svbool_t pt = svptrue_b8();
  svuint32_t dep_mask = svdup_u32(0x7F7F7F7Fu);
  alignas(16) static const uint32_t TAG_TABLE[8] = {
      0u, 0u, 0x80u, 0x8080u, 0x808080u, 0x80808080u, 0u, 0u};
  alignas(16) static const uint32_t EXT_TABLE[8] = {
      0u, 0xFFu, 0xFFFFu, 0xFFFFFFu, 0xFFFFFFFFu, 0u, 0u, 0u};
  svuint32_t tag_table = svld1(pt, TAG_TABLE);
  svuint32_t ext_table = svld1(pt, EXT_TABLE);
  svuint8_t fast_index = svindex_u8(0, 4);
  const uint32_t* data = reinterpret_cast<const uint32_t*>(loc);

  // Per batch we reserve an upper bound of 8 * 5 = 40 bytes, plus 8 bytes of
  // slack for the trailing 8-byte store of the packed single-byte fast path.
  constexpr size_t kBatchReserve = 8 * 5 + 8;

  uint32_t i = 0;
  for (; i + 8 <= size; i += 8) {
    out.ensure(kBatchReserve);
    uint8_t* const batch_start = out.writableData();
    uint8_t* ptr = batch_start;

    svuint32_t origin_value = svld1(pt, data + i);
    if (std::is_same<T, int32_t>::value) {
      svint32_t v_s = svreinterpret_s32_u32(origin_value);
      svuint32_t left = svlsl_n_u32_z(pt, origin_value, 1);
      svint32_t sign = svasr_n_s32_z(pt, v_s, 31);
      svuint32_t sign_u = svreinterpret_u32_s32(sign);
      origin_value = sveor_u32_z(pt, left, sign_u);
    }

    svbool_t p1 = svcmpgt_n_u32(pt, origin_value, 0x7Fu);
    if (!svptest_any(pt, p1)) {
      // All-single-byte fast path: emit exactly 8 bytes.
      svuint8_t fast_data = svreinterpret_u8_u32(origin_value);
      svuint8_t fast_lookup = svtbl(fast_data, fast_index);
      svuint64_t fast_result = svreinterpret_u64_u8(fast_lookup);
      svst1(
          svwhilelt_b64(0u, 1u),
          reinterpret_cast<uint64_t*>(ptr),
          fast_result);
      ptr += 8;
      const size_t used = static_cast<size_t>(ptr - batch_start);
      out.append(used);
      written += static_cast<uint32_t>(used);
      continue;
    }

    svuint32_t data_value = svbdep(origin_value, dep_mask);
    svuint32_t cnt_val = svorr_n_u32_x(pt, data_value, 1);
    svuint32_t lz_count = svclz_x(pt, cnt_val);
    svuint32_t bits_count = svsubr_n_u32_x(pt, lz_count, 32 + 7);
    svuint32_t byte_count = svlsr_n_u32_x(pt, bits_count, 3);

    svbool_t p5 = svcmpgt_n_u32(pt, origin_value, 0xFFFFFFFu);
    if (svptest_any(pt, p5)) {
      uint32_t remain_buffer[8];
      const uint32_t* remain_bits;
      if (std::is_same<T, int32_t>::value) {
        svst1(pt, remain_buffer, origin_value);
        remain_bits = remain_buffer;
      } else {
        remain_bits = data + i;
      }
      byte_count = svsel(p5, svdup_n_u32(5), byte_count);
      svuint32_t tag_value = svtbl(tag_table, byte_count);
      svuint32_t sepint_value = svorr_x(pt, tag_value, data_value);
      uint32_t val[8];
      uint32_t len[8];
      svst1(pt, len, byte_count);
      svst1(pt, val, sepint_value);
      for (int k = 0; k < 8; ++k) {
        std::memcpy(ptr, &val[k], sizeof(uint32_t));
        ptr += len[k];
        if (len[k] == 5) {
          *(ptr - 1) = static_cast<uint8_t>(remain_bits[k] >> 28);
        }
      }
      const size_t used = static_cast<size_t>(ptr - batch_start);
      out.append(used);
      written += static_cast<uint32_t>(used);
      continue;
    }

    // All elements fit in 1..4 bytes: pack two encoded varints per u64.
    svuint32_t tag_value = svtbl(tag_table, byte_count);
    svuint32_t sepint_value = svorr_x(pt, tag_value, data_value);
    svuint64_t ext_mask = svreinterpret_u64_u32(svtbl(ext_table, byte_count));
    svuint64_t merge_value = svreinterpret_u64_u32(sepint_value);
    svuint64_t connect_value = svbext(merge_value, ext_mask);
    svuint32_t add_pair = svaddp_x(pt, byte_count, svdup_n_u32(0));
    svuint64_t pair_len = svreinterpret_u64_u32(add_pair);

    uint64_t len[4];
    svst1(pt, len, pair_len);
    uint64_t val[4];
    svst1(pt, val, connect_value);
    for (int k = 0; k < 4; ++k) {
      std::memcpy(ptr, &val[k], sizeof(uint64_t));
      ptr += len[k];
    }
    const size_t used = static_cast<size_t>(ptr - batch_start);
    out.append(used);
    written += static_cast<uint32_t>(used);
  }

  // Scalar tail.
  if (i < size) {
    if (std::is_same<T, int32_t>::value) {
      written += varintEncodeScalar32(
          reinterpret_cast<const int32_t*>(loc + i), size - i, out);
    } else {
      for (; i < size; ++i) {
        written += apache::thrift::util::writeVarint(out, loc[i]);
      }
    }
  }
  return written;
}

template <class T>
uint32_t varintEncode64SveKernel(
    const T* loc, uint32_t size, folly::io::QueueAppender& out) {
  static_assert(
      std::is_same<T, int64_t>::value || std::is_same<T, uint64_t>::value,
      "T must be int64_t or uint64_t");

  uint32_t written = 0;
  svbool_t pt = svptrue_b8();
  svuint64_t dep_mask = svdup_u64(0x7F7F7F7F7F7F7F7FULL);
  alignas(16) static const uint64_t TAG_TABLE1[4] = {
      0ULL, 0x80ULL, 0x8080ULL, 0x808080ULL};
  alignas(16) static const uint64_t TAG_TABLE2[4] = {
      0x80808080ULL,
      0x8080808080ULL,
      0x808080808080ULL,
      0x80808080808080ULL};
  svuint64_t tag_table1 = svld1(pt, TAG_TABLE1);
  svuint64_t tag_table2 = svld1(pt, TAG_TABLE2);
  svuint64x2_t tag_table = svcreate2_u64(tag_table1, tag_table2);
  const uint64_t* data = reinterpret_cast<const uint64_t*>(loc);

  constexpr size_t kBatchReserve = 4 * 10 + 8;

  uint32_t i = 0;
  for (; i + 4 <= size; i += 4) {
    out.ensure(kBatchReserve);
    uint8_t* const batch_start = out.writableData();
    uint8_t* ptr = batch_start;

    svuint64_t origin_value = svld1(pt, data + i);
    if (std::is_same<T, int64_t>::value) {
      svint64_t v_s = svreinterpret_s64_u64(origin_value);
      svuint64_t left = svlsl_n_u64_z(pt, origin_value, 1);
      svint64_t sign = svasr_n_s64_z(pt, v_s, 63);
      svuint64_t sign_u = svreinterpret_u64_s64(sign);
      origin_value = sveor_u64_z(pt, left, sign_u);
    }

    svbool_t p1 = svcmpgt_n_u64(pt, origin_value, 0x7FULL);
    if (!svptest_any(pt, p1)) {
      svuint8_t fast_index = svindex_u8(0, 8);
      svuint8_t fast_data = svreinterpret_u8_u64(origin_value);
      svuint8_t fast_lookup = svtbl(fast_data, fast_index);
      svuint32_t fast_result = svreinterpret_u32_u8(fast_lookup);
      svst1(
          svwhilelt_b32(0u, 1u),
          reinterpret_cast<uint32_t*>(ptr),
          fast_result);
      ptr += 4;
      const size_t used = static_cast<size_t>(ptr - batch_start);
      out.append(used);
      written += static_cast<uint32_t>(used);
      continue;
    }

    svuint64_t data_value = svbdep(origin_value, dep_mask);
    svuint64_t cnt_val = svorr_n_u64_x(pt, data_value, 1);
    svuint64_t lz_count = svclz_x(pt, cnt_val);
    svuint64_t bits_count = svsubr_n_u64_x(pt, lz_count, 64 + 7 - 8);
    svuint64_t byte_count = svlsr_n_u64_x(pt, bits_count, 3);

    svbool_t p9 = svcmpgt_n_u64(pt, origin_value, 0xFFFFFFFFFFFFFFULL);
    if (svptest_any(pt, p9)) {
      uint64_t remain_buffer[4];
      const uint64_t* remain_bits;
      if (std::is_same<T, int64_t>::value) {
        svst1(pt, remain_buffer, origin_value);
        remain_bits = remain_buffer;
      } else {
        remain_bits = data + i;
      }
      svuint64_t tag_value = svtbl2(tag_table, byte_count);
      svbool_t p10 = svcmpgt_n_u64(pt, origin_value, 0x7FFFFFFFFFFFFFFFULL);
      byte_count = svadd_n_u64_m(pt, byte_count, 1);
      byte_count = svsel(p9, svdup_n_u64(9), byte_count);
      tag_value =
          svsel(p9, svdup_n_u64(0x8080808080808080ULL), tag_value);
      byte_count = svsel(p10, svdup_n_u64(10), byte_count);
      svuint64_t encode_value = svorr_x(pt, tag_value, data_value);
      uint64_t len[4];
      uint64_t val[4];
      svst1(pt, len, byte_count);
      svst1(pt, val, encode_value);
      for (int k = 0; k < 4; ++k) {
        std::memcpy(ptr, &val[k], sizeof(uint64_t));
        if (len[k] == 9) {
          *(ptr + 8) = static_cast<uint8_t>(remain_bits[k] >> 56);
        } else if (len[k] == 10) {
          *(ptr + 8) = static_cast<uint8_t>(remain_bits[k] >> 56);
          *(ptr + 9) = 1;
        }
        ptr += len[k];
      }
      const size_t used = static_cast<size_t>(ptr - batch_start);
      out.append(used);
      written += static_cast<uint32_t>(used);
    } else {
      svuint64_t tag_value = svtbl2(tag_table, byte_count);
      byte_count = svadd_n_u64_m(pt, byte_count, 1);
      svuint64_t encode_value = svorr_x(pt, tag_value, data_value);
      uint64_t len[4];
      uint64_t val[4];
      svst1(pt, len, byte_count);
      svst1(pt, val, encode_value);
      for (int k = 0; k < 4; ++k) {
        std::memcpy(ptr, &val[k], sizeof(uint64_t));
        ptr += len[k];
      }
      const size_t used = static_cast<size_t>(ptr - batch_start);
      out.append(used);
      written += static_cast<uint32_t>(used);
    }
  }

  // Scalar tail.
  if (i < size) {
    if (std::is_same<T, int64_t>::value) {
      written += varintEncodeScalar64(
          reinterpret_cast<const int64_t*>(loc + i), size - i, out);
    } else {
      for (; i < size; ++i) {
        written += apache::thrift::util::writeVarint(out, loc[i]);
      }
    }
  }
  return written;
}

bool detectRuntimeSve2() {
#if defined(__linux__)
  unsigned long hwcap2 = ::getauxval(AT_HWCAP2);
  if (!(hwcap2 & HWCAP2_SVE2)) {
    return false;
  }
  if (!(hwcap2 & HWCAP2_SVEBITPERM)) {
    return false;
  }
#endif
  // VL must be exactly 256 bits (svcntw() == 8 / svcntd() == 4). The kernels
  // are hand-tuned for this layout. If the VL does not match we fall back to
  // the scalar encoder at runtime.
  return svcntw() == 8u && svcntd() == 4u;
}

#endif // THRIFT_HAS_ARM_SVE2

} // namespace

bool hasRuntimeSve2() {
#if defined(THRIFT_HAS_ARM_SVE2)
  static const bool v = detectRuntimeSve2();
  return v;
#else
  return false;
#endif
}

uint32_t dispatchVarintEncode32(
    const int32_t* data, uint32_t size, folly::io::QueueAppender& out) {
#if defined(THRIFT_HAS_ARM_SVE2)
  if (hasRuntimeSve2()) {
    return varintEncode32SveKernel<int32_t>(data, size, out);
  }
#endif
  return varintEncodeScalar32(data, size, out);
}

uint32_t dispatchVarintEncode64(
    const int64_t* data, uint32_t size, folly::io::QueueAppender& out) {
#if defined(THRIFT_HAS_ARM_SVE2)
  if (hasRuntimeSve2()) {
    return varintEncode64SveKernel<int64_t>(data, size, out);
  }
#endif
  return varintEncodeScalar64(data, size, out);
}

} // namespace compact
} // namespace detail
} // namespace thrift
} // namespace apache
