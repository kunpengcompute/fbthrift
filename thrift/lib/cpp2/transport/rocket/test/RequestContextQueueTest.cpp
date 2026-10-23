/* Copyright (c) Huawei Technologies Co., Ltd. 2026. Licensed under Apache-2.0.
 */
#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>
#include <folly/io/Cursor.h>
#include <folly/portability/GTest.h>
#include <thrift/lib/cpp2/transport/rocket/client/RequestContextQueue.h>

namespace apache::thrift::rocket {
namespace {

// SEND capacity reserves 128 B from a 4 KiB block. The independently calibrated
// READ threshold is larger: a record between these limits still uses SEND.
constexpr size_t kTestSendPayloadCapacity = 4096 - 128;
constexpr size_t kTestReadThreshold = 4352;
static_assert(kTestSendPayloadCapacity < kTestReadThreshold);

RequestFnfFrame request(uint32_t id, size_t bytes, size_t part) {
  if (!part) {
    throw std::invalid_argument("node partition size must be positive");
  }
  std::unique_ptr<folly::IOBuf> data;
  while (bytes) {
    const auto size = std::min(bytes, part);
    auto node = folly::IOBuf::copyBuffer(std::string(size, 'x'));
    if (data) {
      data->prependChain(std::move(node));
    } else {
      data = std::move(node);
    }
    bytes -= size;
  }
  return RequestFnfFrame(StreamId{id}, Payload::makeFromData(std::move(data)));
}

void finishBatch(RequestContextQueue& queue) {
  queue.markNextSendingBatchAsSent(
      [&](RequestContext& req) { queue.markAsResponded(req); });
}

std::string bytes(const folly::IOBuf& data) {
  folly::io::Cursor cursor(&data);
  return cursor.readFixedString(data.computeChainDataLength());
}

void expectRecords(
    const std::unique_ptr<folly::IOBuf>& data,
    const std::vector<size_t>& lengths,
    const std::vector<std::string>& expected) {
  ASSERT_TRUE(data);
  ASSERT_EQ(lengths.size(), expected.size());
  const auto actual = bytes(*data);
  size_t offset = 0;
  for (size_t i = 0; i < expected.size(); ++i) {
    ASSERT_EQ(lengths[i], expected[i].size()) << "record " << i;
    EXPECT_EQ(actual.substr(offset, lengths[i]), expected[i]) << "record " << i;
    offset += lengths[i];
  }
  EXPECT_EQ(offset, actual.size());
}

TEST(UrmaRequestRecords, SmallRequestsRemainSeparateAcrossNodePartitions) {
  for (const size_t part : {size_t(1), size_t(7), size_t(1024)}) {
    RequestContextQueue queue;
    RequestContext first(request(1, 3000, part), queue);
    RequestContext second(request(3, 2500, part), queue);
    queue.enqueueScheduledWrite(first);
    queue.enqueueScheduledWrite(second);
    std::vector<size_t> lengths{
        999}; // Reusing output must not retain old data.
    auto data = queue.getNextScheduledWritesBatch(&lengths);
    finishBatch(queue);
    ASSERT_TRUE(data);
    ASSERT_EQ(lengths.size(), 2);
    EXPECT_LT(lengths[0], kTestSendPayloadCapacity);
    EXPECT_GT(lengths[0], lengths[1]);
    expectRecords(
        data,
        lengths,
        {bytes(*request(1, 3000, part).serialize()),
         bytes(*request(3, 2500, part).serialize())});
    EXPECT_GT(data->computeChainDataLength(), kTestReadThreshold);
    EXPECT_EQ(
        std::accumulate(lengths.begin(), lengths.end(), size_t(0)),
        data->computeChainDataLength());
  }
}

TEST(UrmaRequestRecords, SetupDoesNotEnlargeTheFirstSmallRequest) {
  RequestContextQueue queue;
  // Both records fit SEND, but their combined bytes exceed the READ threshold.
  constexpr size_t kSetupPayloadSize = 1500;
  SetupFrame setup(
      Payload::makeFromData(
          folly::IOBuf::copyBuffer(std::string(kSetupPayloadSize, 's'))),
      false);
  SetupFrame expectedSetup(
      Payload::makeFromData(
          folly::IOBuf::copyBuffer(std::string(kSetupPayloadSize, 's'))),
      false);
  auto setupBytes = bytes(*std::move(expectedSetup).serialize());
  RequestContext first(request(1, 3000, 7), queue, &setup);
  queue.enqueueScheduledWrite(first);
  std::vector<size_t> lengths;
  auto data = queue.getNextScheduledWritesBatch(&lengths);
  finishBatch(queue);
  ASSERT_TRUE(data);
  ASSERT_EQ(lengths.size(), 2);
  EXPECT_LT(lengths[0], kTestSendPayloadCapacity);
  EXPECT_LT(lengths[1], kTestSendPayloadCapacity);
  EXPECT_GT(data->computeChainDataLength(), kTestReadThreshold);
  EXPECT_EQ(lengths[0] + lengths[1], data->computeChainDataLength());
  expectRecords(
      data, lengths, {setupBytes, bytes(*request(1, 3000, 7).serialize())});
}

TEST(UrmaRequestRecords, MixedSmallLargeSmallRecordsPreserveEveryByte) {
  for (const size_t part : {size_t(1), size_t(7), size_t(1024), size_t(8192)}) {
    RequestContextQueue queue;
    RequestContext first(request(1, 1000, part), queue);
    RequestContext second(request(3, 8192, part), queue);
    RequestContext third(request(5, 700, part), queue);
    queue.enqueueScheduledWrite(first);
    queue.enqueueScheduledWrite(second);
    queue.enqueueScheduledWrite(third);
    std::vector<size_t> lengths;
    auto data = queue.getNextScheduledWritesBatch(&lengths);
    finishBatch(queue);
    expectRecords(
        data,
        lengths,
        {bytes(*request(1, 1000, part).serialize()),
         bytes(*request(3, 8192, part).serialize()),
         bytes(*request(5, 700, part).serialize())});
  }
}

TEST(UrmaRequestRecords, ZeroNodePartitionIsRejected) {
  EXPECT_THROW(request(1, 1000, 0), std::invalid_argument);
}

TEST(UrmaRequestRecords, EmptyQueueClearsRecords) {
  RequestContextQueue queue;
  std::vector<size_t> lengths{100};
  EXPECT_FALSE(queue.getNextScheduledWritesBatch(&lengths));
  EXPECT_TRUE(lengths.empty());
}

TEST(UrmaRequestRecords, OrdinaryBatchingNeedsNoRecordOutput) {
  RequestContextQueue queue;
  RequestContext first(request(1, 8192, 7), queue);
  queue.enqueueScheduledWrite(first);
  auto data = queue.getNextScheduledWritesBatch();
  finishBatch(queue);
  ASSERT_TRUE(data);
  EXPECT_GT(data->computeChainDataLength(), 8192);
}

} // namespace
} // namespace apache::thrift::rocket
