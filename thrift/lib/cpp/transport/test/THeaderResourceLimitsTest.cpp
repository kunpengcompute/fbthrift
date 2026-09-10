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

#include <thrift/lib/cpp/transport/THeader.h>

#include <zstd.h>
#include <folly/io/IOBufQueue.h>
#include <folly/portability/GTest.h>
#include <thrift/lib/cpp/TApplicationException.h>

namespace apache::thrift::transport {
namespace {
using folly::IOBuf;
using Headers = THeader::StringToStringMap;
using Limits = THeader::ReadLimits;
using Pairs = std::vector<std::pair<std::string, std::string>>;

void varint(std::string& out, uint32_t value) {
  while (value >= 128) {
    out.push_back((value & 127) | 128);
    value >>= 7;
  }
  out.push_back(value);
}

void info(std::string& out, uint32_t type, const Pairs& pairs) {
  if (pairs.empty()) {
    return;
  }
  varint(out, type);
  varint(out, pairs.size());
  for (const auto& pair : pairs) {
    varint(out, pair.first.size());
    out += pair.first;
    varint(out, pair.second.size());
    out += pair.second;
  }
}

std::unique_ptr<IOBuf> rawFrame(
    std::string metadata, std::string payload = "x") {
  metadata.resize((metadata.size() + 3) & ~size_t(3), '\0');
  std::string frame(10, '\0');
  frame[0] = 0x0f;
  frame[1] = 0xff;
  frame[8] = (metadata.size() / 4) >> 8;
  frame[9] = metadata.size() / 4;
  frame += metadata;
  frame += payload;
  return IOBuf::copyBuffer(frame);
}

std::unique_ptr<IOBuf> frame(
    const Pairs& persistent = {},
    const Pairs& request = {},
    const std::vector<uint16_t>& transforms = {},
    std::string payload = "x") {
  std::string metadata;
  varint(metadata, 0); // binary protocol
  varint(metadata, transforms.size());
  for (auto transform : transforms) {
    varint(metadata, transform);
  }
  info(metadata, 2, persistent);
  info(metadata, 1, request);
  return rawFrame(metadata, std::move(payload));
}

std::unique_ptr<IOBuf> compressed(std::string data, uint16_t transform) {
  std::vector<uint16_t> transforms{transform};
  return THeader::transform(IOBuf::copyBuffer(data), transforms);
}

std::unique_ptr<IOBuf> zstdUnknownSize(const std::string& data) {
  std::unique_ptr<ZSTD_CCtx, decltype(&ZSTD_freeCCtx)> ctx(
      ZSTD_createCCtx(), &ZSTD_freeCCtx);
  if (!ctx ||
      ZSTD_isError(
          ZSTD_CCtx_setParameter(ctx.get(), ZSTD_c_contentSizeFlag, 0))) {
    throw std::runtime_error("Cannot initialize test compressor");
  }
  std::string output(ZSTD_compressBound(data.size()), '\0');
  const auto size = ZSTD_compress2(
      ctx.get(), output.data(), output.size(), data.data(), data.size());
  if (ZSTD_isError(size)) {
    throw std::runtime_error(ZSTD_getErrorName(size));
  }
  output.resize(size);
  return IOBuf::copyBuffer(output);
}

class HeaderDecompressionLimitsTest : public testing::TestWithParam<uint16_t> {
};

TEST_P(HeaderDecompressionLimitsTest, OutputBoundary) {
  Limits limits;
  limits.maxUncompressedBytes = 1024;
  std::vector<uint16_t> transforms{GetParam()};
  for (size_t size : {0, 1023, 1024, 1025}) {
    SCOPED_TRACE(size);
    auto input = compressed(std::string(size, 'a'), GetParam());
    if (size <= 1024) {
      auto output = THeader::untransform(std::move(input), transforms, limits);
      EXPECT_EQ(output->computeChainDataLength(), size);
      EXPECT_EQ(output->to<std::string>(), std::string(size, 'a'));
    } else {
      EXPECT_THROW(
          THeader::untransform(std::move(input), transforms, limits),
          TTransportException);
    }
  }
}

TEST_P(HeaderDecompressionLimitsTest, ChainedInputAndOutput) {
  std::string data(50000, 'x');
  for (size_t i = 0; i < data.size(); ++i) {
    data[i] = static_cast<char>((i * 37) % 251);
  }
  auto bytes = compressed(data, GetParam())->to<std::string>();
  folly::IOBufQueue chain;
  chain.append(IOBuf::create(0));
  for (auto byte : bytes) {
    chain.append(IOBuf::copyBuffer(&byte, 1));
    chain.append(IOBuf::create(0));
  }
  Limits limits;
  limits.maxUncompressedBytes = data.size();
  std::vector<uint16_t> transforms{GetParam()};
  auto output = THeader::untransform(chain.move(), transforms, limits);
  EXPECT_EQ(output->to<std::string>(), data);
}

TEST_P(HeaderDecompressionLimitsTest, TruncatedAndTrailingDataRejected) {
  auto bytes =
      compressed(std::string(2048, 'a'), GetParam())->to<std::string>();
  std::vector<uint16_t> transforms{GetParam()};
  for (size_t size : {size_t(0), size_t(1), bytes.size() - 1}) {
    EXPECT_THROW(
        THeader::untransform(IOBuf::copyBuffer(bytes.data(), size), transforms),
        TApplicationException);
  }
  bytes.push_back('x');
  EXPECT_THROW(
      THeader::untransform(IOBuf::copyBuffer(bytes), transforms),
      TApplicationException);
}

TEST_P(HeaderDecompressionLimitsTest, RepeatedOversizedRequests) {
  auto bytes = compressed(std::string(1 << 20, 'a'), GetParam());
  std::vector<uint16_t> transforms{GetParam()};
  Limits limits;
  limits.maxUncompressedBytes = 128;
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_THROW(
        THeader::untransform(bytes->clone(), transforms, limits),
        TTransportException);
  }
  EXPECT_EQ(
      THeader::untransform(compressed("ok", GetParam()), transforms, limits)
          ->to<std::string>(),
      "ok");
}

TEST_P(HeaderDecompressionLimitsTest, WorkCountsInputAndOutput) {
  auto input = compressed(std::string(1024, 'x'), GetParam());
  const auto exactWork = input->computeChainDataLength() + 1024;
  std::vector<uint16_t> transforms{GetParam()};
  Limits limits;
  limits.maxDecompressionWorkBytes = exactWork;
  EXPECT_EQ(
      THeader::untransform(input->clone(), transforms, limits)
          ->computeChainDataLength(),
      1024);
  --limits.maxDecompressionWorkBytes;
  EXPECT_THROW(
      THeader::untransform(input->clone(), transforms, limits),
      TTransportException);
  limits.maxDecompressionWorkBytes = input->computeChainDataLength() - 1;
  EXPECT_THROW(
      THeader::untransform(input->clone(), transforms, limits),
      TTransportException);
}

INSTANTIATE_TEST_SUITE_P(
    Codecs,
    HeaderDecompressionLimitsTest,
    testing::Values(THeader::ZLIB_TRANSFORM, THeader::ZSTD_TRANSFORM));

TEST(THeaderResourceLimitsTest, UnknownZstdLengthBoundary) {
  Limits limits;
  limits.maxUncompressedBytes = 1024;
  std::vector<uint16_t> transforms{THeader::ZSTD_TRANSFORM};
  for (size_t size : {1023, 1024, 1025}) {
    auto input = zstdUnknownSize(std::string(size, 'x'));
    EXPECT_EQ(
        ZSTD_getFrameContentSize(input->data(), input->length()),
        ZSTD_CONTENTSIZE_UNKNOWN);
    if (size <= 1024) {
      EXPECT_EQ(
          THeader::untransform(std::move(input), transforms, limits)
              ->computeChainDataLength(),
          size);
    } else {
      EXPECT_THROW(
          THeader::untransform(std::move(input), transforms, limits),
          TTransportException);
    }
  }
}

TEST(THeaderResourceLimitsTest, ZstdWindowLimitIndependentOfOutputLimit) {
  auto input = zstdUnknownSize(std::string(1 << 20, 'x'));
  Limits limits;
  limits.zstdWindowLogMax = 10;
  std::vector<uint16_t> transforms{THeader::ZSTD_TRANSFORM};
  EXPECT_THROW(
      THeader::untransform(input->clone(), transforms, limits),
      TApplicationException);
  limits.zstdWindowLogMax = 23;
  EXPECT_EQ(
      THeader::untransform(input->clone(), transforms, limits)
          ->computeChainDataLength(),
      1 << 20);
}

TEST(THeaderResourceLimitsTest, LayerCountAndCumulativeWork) {
  auto inner = compressed(std::string(1024, 'x'), THeader::ZLIB_TRANSFORM);
  auto outer = compressed(inner->to<std::string>(), THeader::ZSTD_TRANSFORM);
  std::vector<uint16_t> transforms{
      THeader::ZLIB_TRANSFORM, THeader::ZSTD_TRANSFORM};
  Limits limits;
  limits.maxTransforms = 1;
  EXPECT_THROW(
      THeader::untransform(outer->clone(), transforms, limits),
      TTransportException);
  limits.maxTransforms = 2;
  limits.maxDecompressionWorkBytes = outer->computeChainDataLength() +
      2 * inner->computeChainDataLength() + 1024;
  EXPECT_EQ(
      THeader::untransform(outer->clone(), transforms, limits)
          ->computeChainDataLength(),
      1024);
  --limits.maxDecompressionWorkBytes;
  EXPECT_THROW(
      THeader::untransform(outer->clone(), transforms, limits),
      TTransportException);
}

TEST(THeaderResourceLimitsTest, IntermediateOutputAlsoLimited) {
  // Compressing a one-byte payload makes an intermediate stream larger than
  // the final result. A final-output-only limit would incorrectly allow this.
  auto inner = compressed("x", THeader::ZLIB_TRANSFORM);
  auto outer = compressed(inner->to<std::string>(), THeader::ZSTD_TRANSFORM);
  std::vector<uint16_t> transforms{
      THeader::ZLIB_TRANSFORM, THeader::ZSTD_TRANSFORM};
  Limits limits;
  limits.maxUncompressedBytes = 1;
  EXPECT_THROW(
      THeader::untransform(std::move(outer), transforms, limits),
      TTransportException);
}

TEST(THeaderResourceLimitsTest, PlainPayloadAndInvalidConfiguration) {
  Limits limits;
  limits.maxUncompressedBytes = 1;
  std::vector<uint16_t> transforms;
  EXPECT_THROW(
      THeader::untransform(IOBuf::copyBuffer("xx"), transforms, limits),
      TTransportException);
  THeader header;
  limits.maxUncompressedBytes = 0;
  EXPECT_THROW(header.setReadLimits(limits), std::invalid_argument);
  limits = Limits{};
  limits.zstdWindowLogMax = 0;
  EXPECT_THROW(header.setReadLimits(limits), std::invalid_argument);
}

TEST(THeaderResourceLimitsTest, PersistentByteBoundaryAndAtomicFailure) {
  for (size_t bytes : {7, 8, 9}) {
    THeader header;
    Limits limits;
    limits.maxPersistentHeaderBytes = 8;
    header.setReadLimits(limits);
    Headers persistent;
    auto input = frame({{"k", std::string(bytes - 1, 'v')}});
    if (bytes <= 8) {
      EXPECT_NO_THROW(header.readHeaderFormat(std::move(input), persistent));
      EXPECT_EQ(persistent.at("k").size(), bytes - 1);
    } else {
      EXPECT_THROW(
          header.readHeaderFormat(std::move(input), persistent),
          TTransportException);
      EXPECT_TRUE(persistent.empty());
    }
  }
}

TEST(THeaderResourceLimitsTest, RepeatedNewKeysHitEntryLimit) {
  THeader header;
  Limits limits;
  limits.maxPersistentHeaders = 2;
  header.setReadLimits(limits);
  Headers persistent;
  header.readHeaderFormat(frame({{"a", "1"}}), persistent);
  header.readHeaderFormat(frame({{"b", "2"}}), persistent);
  const auto before = persistent;
  for (size_t i = 0; i < 100; ++i) {
    EXPECT_THROW(
        header.readHeaderFormat(frame({{std::to_string(i), "3"}}), persistent),
        TTransportException);
    EXPECT_EQ(persistent, before);
  }
}

TEST(THeaderResourceLimitsTest, ReplacementAccountsForOldValue) {
  THeader header;
  Limits limits;
  limits.maxPersistentHeaders = 1;
  limits.maxPersistentHeaderBytes = 8;
  header.setReadLimits(limits);
  Headers persistent;
  for (const auto& value : {"1234567", "1", "1234567"}) {
    EXPECT_NO_THROW(header.readHeaderFormat(frame({{"k", value}}), persistent));
    EXPECT_EQ(persistent.size(), 1);
    EXPECT_EQ(persistent.at("k"), value);
  }
  EXPECT_THROW(
      header.readHeaderFormat(frame({{"k", "12345678"}}), persistent),
      TTransportException);
  EXPECT_EQ(persistent.at("k"), "1234567");
}

TEST(THeaderResourceLimitsTest, PersistentBytesAccumulateAcrossFrames) {
  THeader header;
  Limits limits;
  limits.maxPersistentHeaderBytes = 6;
  header.setReadLimits(limits);
  Headers persistent;
  header.readHeaderFormat(frame({{"a", "11"}}), persistent);
  header.readHeaderFormat(frame({{"b", "22"}}), persistent);
  const auto before = persistent;
  EXPECT_THROW(
      header.readHeaderFormat(frame({{"c", "3"}}), persistent),
      TTransportException);
  EXPECT_EQ(persistent, before);
}

TEST(THeaderResourceLimitsTest, RequestHeaderByteBoundary) {
  for (size_t bytes : {3, 4, 5}) {
    THeader header;
    Limits limits;
    limits.maxReadHeaderBytes = 4;
    header.setReadLimits(limits);
    Headers persistent;
    auto input = frame({}, {{"k", std::string(bytes - 1, 'v')}});
    if (bytes <= 4) {
      EXPECT_NO_THROW(header.readHeaderFormat(std::move(input), persistent));
      EXPECT_EQ(header.getHeaders().at("k").size(), bytes - 1);
    } else {
      EXPECT_THROW(
          header.readHeaderFormat(std::move(input), persistent),
          TTransportException);
      EXPECT_TRUE(header.getHeaders().empty());
    }
  }
}

TEST(THeaderResourceLimitsTest, ZeroHeaderQuotasAndDuplicateKeys) {
  THeader header;
  Limits limits;
  limits.maxPersistentHeaders = 0;
  limits.maxReadHeaders = 0;
  header.setReadLimits(limits);
  Headers persistent;
  EXPECT_NO_THROW(header.readHeaderFormat(frame(), persistent));
  EXPECT_THROW(
      header.readHeaderFormat(frame({{"", ""}}), persistent),
      TTransportException);
  EXPECT_THROW(
      header.readHeaderFormat(frame({}, {{"", ""}}), persistent),
      TTransportException);
  limits.maxPersistentHeaders = 1;
  limits.maxReadHeaders = 1;
  header.setReadLimits(limits);
  header.readHeaderFormat(frame({{"k", "old"}, {"k", "new"}}), persistent);
  EXPECT_EQ(persistent, (Headers{{"k", "new"}}));
}

TEST(THeaderResourceLimitsTest, FailedFrameDoesNotPartiallyReplaceState) {
  THeader header;
  Limits limits;
  limits.maxPersistentHeaders = 2;
  header.setReadLimits(limits);
  Headers persistent{{"old", "original"}};
  const auto before = persistent;
  EXPECT_THROW(
      header.readHeaderFormat(
          frame({{"old", "replaced"}, {"new", "1"}, {"overflow", "2"}}),
          persistent),
      TTransportException);
  EXPECT_EQ(persistent, before);
  EXPECT_TRUE(header.getHeaders().empty());
}

TEST(THeaderResourceLimitsTest, RequestOverridesPersistentWithoutDeletingIt) {
  THeader header;
  Headers persistent;
  header.readHeaderFormat(frame({{"key", "persistent"}}), persistent);
  header.readHeaderFormat(frame({}, {{"key", "request"}}), persistent);
  EXPECT_EQ(header.getHeaders().at("key"), "request");
  EXPECT_EQ(persistent.at("key"), "persistent");
  header.readHeaderFormat(frame(), persistent);
  EXPECT_EQ(header.getHeaders().at("key"), "persistent");
}

TEST(THeaderResourceLimitsTest, MergedHeadersHaveByteAndEntryBudgets) {
  for (bool bytes : {false, true}) {
    THeader header;
    Limits limits;
    limits.maxReadHeaders = bytes ? 10 : 1;
    limits.maxReadHeaderBytes = bytes ? 3 : 100;
    header.setReadLimits(limits);
    Headers persistent;
    header.readHeaderFormat(frame({{"a", "1"}}), persistent);
    const auto before = persistent;
    EXPECT_THROW(
        header.readHeaderFormat(frame({}, {{"b", "2"}}), persistent),
        TTransportException);
    EXPECT_EQ(persistent, before);
    // Duplicate key occupies one merged entry, not two.
    EXPECT_NO_THROW(
        header.readHeaderFormat(frame({}, {{"a", "2"}}), persistent));
  }
}

TEST(THeaderResourceLimitsTest, DeclaredHeaderBoundaryCannotReadPayload) {
  THeader header;
  Headers persistent{{"old", "value"}};
  const auto before = persistent;
  // Metadata ends after the key's length; payload contains enough bytes to
  // satisfy the old unbounded cursor, but is not part of the header region.
  std::string metadata;
  varint(metadata, 0);
  varint(metadata, 0);
  varint(metadata, 2);
  varint(metadata, 1);
  varint(metadata, 100);
  EXPECT_THROW(
      header.readHeaderFormat(
          rawFrame(metadata, std::string(200, 'x')), persistent),
      TTransportException);
  EXPECT_EQ(persistent, before);
}

TEST(THeaderResourceLimitsTest, TruncatedVarintAndHugeCountDoNotCommit) {
  THeader header;
  Headers persistent{{"old", "value"}};
  const auto before = persistent;
  // Exactly four metadata bytes: the next varint may not consume payload.
  const std::string partial{"\0\0\2\x80", 4};
  EXPECT_THROW(
      header.readHeaderFormat(rawFrame(partial), persistent), std::exception);
  std::string huge{"\0\0\2", 3};
  varint(huge, 0xffffffff);
  EXPECT_THROW(
      header.readHeaderFormat(rawFrame(huge), persistent), TTransportException);
  EXPECT_EQ(persistent, before);
}

TEST(
    THeaderResourceLimitsTest,
    FailedDecompressionDoesNotCommitPersistentState) {
  THeader header;
  Headers persistent{{"old", "value"}};
  const auto before = persistent;
  EXPECT_THROW(
      header.readHeaderFormat(
          frame({{"new", "value"}}, {}, {THeader::ZLIB_TRANSFORM}, "invalid"),
          persistent),
      TApplicationException);
  EXPECT_EQ(persistent, before);
  Limits limits;
  limits.maxUncompressedBytes = 16;
  header.setReadLimits(limits);
  auto payload = compressed(std::string(1024, 'x'), THeader::ZLIB_TRANSFORM);
  EXPECT_THROW(
      header.readHeaderFormat(
          frame(
              {{"new", "value"}},
              {},
              {THeader::ZLIB_TRANSFORM},
              payload->to<std::string>()),
          persistent),
      TTransportException);
  EXPECT_EQ(persistent, before);
}

TEST(THeaderResourceLimitsTest, MetadataAndTransformCountAreCheckedFirst) {
  THeader header;
  Limits limits;
  limits.maxHeaderBytes = 4;
  header.setReadLimits(limits);
  Headers persistent;
  EXPECT_THROW(
      header.readHeaderFormat(frame({{"long-key", "value"}}), persistent),
      TTransportException);
  limits = Limits{};
  limits.maxTransforms = 1;
  header.setReadLimits(limits);
  // Only the count is supplied: the quota must reject before reading IDs.
  const std::string metadata{"\0\2", 2};
  EXPECT_THROW(
      header.readHeaderFormat(rawFrame(metadata), persistent),
      TTransportException);
  EXPECT_TRUE(persistent.empty());
}

TEST(THeaderResourceLimitsTest, FragmentedMetadataPreservesState) {
  auto bytes =
      frame({{"a", "persistent"}}, {{"a", "request"}})->to<std::string>();
  folly::IOBufQueue queue;
  for (auto byte : bytes) {
    queue.append(IOBuf::copyBuffer(&byte, 1));
  }
  THeader header;
  Headers persistent;
  EXPECT_EQ(
      header.readHeaderFormat(queue.move(), persistent)->to<std::string>(),
      "x");
  EXPECT_EQ(persistent.at("a"), "persistent");
  EXPECT_EQ(header.getHeaders().at("a"), "request");
}

} // namespace
} // namespace apache::thrift::transport
