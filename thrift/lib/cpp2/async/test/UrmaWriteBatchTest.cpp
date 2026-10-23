/* Copyright (c) Huawei Technologies Co., Ltd. 2026. Licensed under Apache-2.0.
 */
#include <folly/io/Cursor.h>
#include <folly/portability/GTest.h>
#include <thrift/lib/cpp2/async/TAsyncTransportHandler.h>
#include <wangle/channel/StaticPipeline.h>

#ifdef FOLLY_HAVE_URMA
namespace apache::thrift {
namespace {
class RecordingTransport : public folly::AsyncSocket,
                           public folly::UrmaRecordWriter {
 public:
  explicit RecordingTransport(folly::EventBase* evb) : AsyncSocket(evb) {}
  bool good() const override { return open; }
  void setReadCB(ReadCallback*) override {}
  void closeNow() override { open = false; }
  bool supportsUrmaRecords() const override { return recordsEnabled; }
  void writeChainWithRecords(
      WriteCallback* cb,
      std::unique_ptr<folly::IOBuf>&& data,
      std::vector<size_t> lengths,
      folly::WriteFlags) noexcept override {
    records.push_back(std::move(lengths));
    submit(cb, std::move(data));
  }
  void writeChain(
      WriteCallback* cb,
      std::unique_ptr<folly::IOBuf>&& data,
      folly::WriteFlags) override {
    ++ordinaryWrites;
    submit(cb, std::move(data));
  }
  bool open{true}, recordsEnabled{true}, reject{false};
  int ordinaryWrites{0};
  std::vector<std::vector<size_t>> records;
  std::vector<std::string> bytes;

 private:
  void submit(WriteCallback* cb, std::unique_ptr<folly::IOBuf> data) {
    folly::io::Cursor cursor(data.get());
    bytes.push_back(cursor.readFixedString(data->computeChainDataLength()));
    if (reject) {
      cb->writeErr(
          0,
          folly::AsyncSocketException(
              folly::AsyncSocketException::NOT_OPEN, "test rejected write"));
    } else {
      cb->writeSuccess();
    }
  }
};

class UrmaWriteBatch : public testing::Test {
 protected:
  using Pipeline = wangle::StaticPipeline<
      folly::IOBufQueue&,
      std::unique_ptr<folly::IOBuf>,
      TAsyncTransportHandler,
      detail::UrmaOutputBufferingHandler>;
  folly::EventBase evb;
  std::shared_ptr<RecordingTransport> transport{
      new RecordingTransport(&evb), folly::DelayedDestruction::Destructor()};
  std::shared_ptr<detail::UrmaWriteBatchState> state{
      std::make_shared<detail::UrmaWriteBatchState>()};
  std::shared_ptr<detail::UrmaOutputBufferingHandler> handler{
      std::make_shared<detail::UrmaOutputBufferingHandler>(state)};
  std::shared_ptr<Pipeline> pipeline{
      Pipeline::create(TAsyncTransportHandler(transport, state), handler)};
  void drain() {
    for (int i = 0; i < 4; ++i)
      evb.loopOnce(EVLOOP_NONBLOCK);
  }
  void TearDown() override {
    if (pipeline)
      pipeline->close();
  }
};

TEST_F(UrmaWriteBatch, QueuedBatchesMatchBytesWithoutStaleMetadata) {
  auto first = pipeline->write(folly::IOBuf::copyBuffer("abc"));
  auto chain = folly::IOBuf::copyBuffer("de");
  chain->prependChain(folly::IOBuf::copyBuffer("fghi"));
  auto second = pipeline->write(std::move(chain));
  EXPECT_TRUE(transport->bytes.empty());
  drain();
  ASSERT_EQ(transport->records.size(), 1);
  EXPECT_EQ(transport->records[0], (std::vector<size_t>{3, 6}));
  EXPECT_EQ(transport->bytes[0], "abcdefghi");
  EXPECT_TRUE(first.isReady() && second.isReady());
  EXPECT_FALSE(first.hasException() || second.hasException());
  auto third = pipeline->write(folly::IOBuf::copyBuffer("next"));
  drain();
  ASSERT_EQ(transport->records.size(), 2);
  EXPECT_EQ(transport->records[1], (std::vector<size_t>{4}));
  EXPECT_EQ(transport->bytes[1], "next");
  EXPECT_FALSE(third.hasException());
  EXPECT_TRUE(state->pending.empty() && state->flushing.empty());
}

TEST_F(UrmaWriteBatch, ImmediateAndOrdinaryTransportWrites) {
  handler->queueSends_ = false;
  auto first = pipeline->write(folly::IOBuf::copyBuffer("immediate"));
  ASSERT_EQ(transport->records.size(), 1);
  EXPECT_EQ(transport->records[0], (std::vector<size_t>{9}));
  EXPECT_TRUE(first.isReady());
  transport->recordsEnabled = false;
  auto second = pipeline->write(folly::IOBuf::copyBuffer("ordinary"));
  EXPECT_EQ(transport->ordinaryWrites, 1);
  EXPECT_EQ(transport->bytes.back(), "ordinary");
  EXPECT_TRUE(second.isReady());
  EXPECT_TRUE(state->flushing.empty());
}

TEST_F(UrmaWriteBatch, CloseCancelsPendingBytesAndRecords) {
  auto pending = pipeline->write(folly::IOBuf::copyBuffer("cancel"));
  pipeline->close();
  drain();
  EXPECT_TRUE(pending.isReady() && pending.hasException());
  EXPECT_TRUE(transport->bytes.empty());
  EXPECT_TRUE(state->pending.empty() && state->flushing.empty());
}

TEST_F(UrmaWriteBatch, CleanupDoesNotLeakLengthsIntoNextWrite) {
  auto abandoned = pipeline->write(folly::IOBuf::copyBuffer("old-data"));
  handler->cleanUp();
  auto next = pipeline->write(folly::IOBuf::copyBuffer("new"));
  drain();
  ASSERT_EQ(transport->records.size(), 1);
  EXPECT_EQ(transport->records[0], (std::vector<size_t>{3}));
  EXPECT_EQ(transport->bytes[0], "new");
  EXPECT_TRUE(abandoned.hasException());
  EXPECT_FALSE(next.hasException());
}

TEST_F(UrmaWriteBatch, CompletionCanEnqueueAnotherBatch) {
  folly::Future<folly::Unit> reentrant = folly::makeFuture();
  auto completed =
      pipeline->write(folly::IOBuf::copyBuffer("first"))
          .thenValue([&](folly::Unit) {
            reentrant = pipeline->write(folly::IOBuf::copyBuffer("second"));
          });
  drain();
  EXPECT_TRUE(completed.isReady() && reentrant.isReady());
  EXPECT_EQ(transport->bytes, (std::vector<std::string>{"first", "second"}));
  EXPECT_EQ(transport->records, (std::vector<std::vector<size_t>>{{5}, {6}}));
  EXPECT_TRUE(state->pending.empty() && state->flushing.empty());
}

TEST_F(UrmaWriteBatch, WriteFailureAndPipelineDestructionClearMetadata) {
  transport->reject = true;
  auto failed = pipeline->write(folly::IOBuf::copyBuffer("reject"));
  drain();
  EXPECT_TRUE(failed.isReady() && failed.hasException());
  EXPECT_TRUE(state->pending.empty() && state->flushing.empty());
  transport->reject = false;
  auto completed = pipeline->write(folly::IOBuf::copyBuffer("last"))
                       .thenValue([&](folly::Unit) { pipeline.reset(); });
  handler.reset(); // The pipeline is now the handler's only owner.
  drain();
  EXPECT_TRUE(completed.isReady() && !completed.hasException());
  EXPECT_FALSE(pipeline);
  EXPECT_TRUE(state->pending.empty() && state->flushing.empty());
}
} // namespace
} // namespace apache::thrift
#endif
