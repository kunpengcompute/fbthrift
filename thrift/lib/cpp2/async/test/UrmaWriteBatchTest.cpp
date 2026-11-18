/* Copyright (c) Huawei Technologies Co., Ltd. 2026. Licensed under Apache-2.0.
 */
#include <folly/io/Cursor.h>
#include <folly/portability/GTest.h>
#include <thrift/lib/cpp2/async/TAsyncTransportHandler.h>
#include <thrift/lib/cpp2/async/Cpp2Channel.h>
#include <thrift/lib/cpp2/server/peeking/PeekingManager.h>
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

class FailingFraming final : public FramingHandler {
 public:
  bool fail{false};
  std::unique_ptr<folly::IOBuf> addFrame(
      std::unique_ptr<folly::IOBuf> data, transport::THeader*) override {
    if (fail) throw std::bad_alloc();
    return data;
  }
  std::tuple<std::unique_ptr<folly::IOBuf>, size_t,
             std::unique_ptr<transport::THeader>>
  removeFrame(folly::IOBufQueue*) override { return {nullptr, 0, nullptr}; }
};
struct CountingSendCallback : MessageChannel::SendCallback {
  size_t sent{0}, failed{0};
  bool allocationError{false};
  void sendQueued() override {}
  void messageSent() override { ++sent; }
  void messageSendError(folly::exception_wrapper&& e) override {
    ++failed;
    allocationError = e.is_compatible_with<std::bad_alloc>();
  }
};

TEST(UrmaHeaderFailure, FramingAllocationFailsEachCallbackOnce) {
  CountingSendCallback first, second;
  folly::EventBase evb;
  std::shared_ptr<RecordingTransport> transport(
      new RecordingTransport(&evb), folly::DelayedDestruction::Destructor());
  auto framing = std::make_unique<FailingFraming>();
  auto* fail = framing.get();
  auto channel = Cpp2Channel::newChannel(transport, std::move(framing));
  channel->sendMessage(&first, folly::IOBuf::copyBuffer("first"), nullptr);
  fail->fail = true;
  EXPECT_NO_THROW(channel->sendMessage(
      &second, folly::IOBuf::copyBuffer("second"), nullptr));
  for (int i = 0; i < 4; ++i) evb.loopOnce(EVLOOP_NONBLOCK);
  EXPECT_EQ(first.failed, 1);
  EXPECT_EQ(second.failed, 1);
  EXPECT_TRUE(second.allocationError);
  EXPECT_EQ(first.sent + second.sent, 0);
  EXPECT_TRUE(transport->bytes.empty());
  EXPECT_FALSE(transport->good());
  channel.reset();
  EXPECT_EQ(first.failed, 1);
  EXPECT_EQ(second.failed, 1);
}

TEST(TcpHeaderFailure, FramingExceptionPreservesOtherWriteCallbacks) {
  CountingSendCallback first, rejected, last;
  folly::EventBase evb;
  std::shared_ptr<RecordingTransport> transport(
      new RecordingTransport(&evb), folly::DelayedDestruction::Destructor());
  transport->recordsEnabled = false;
  auto framing = std::make_unique<FailingFraming>();
  auto* encoder = framing.get();
  auto channel = Cpp2Channel::newChannel(transport, std::move(framing));
  channel->sendMessage(&first, folly::IOBuf::copyBuffer("first"), nullptr);
  encoder->fail = true;
  EXPECT_THROW(channel->sendMessage(
      &rejected, folly::IOBuf::copyBuffer("rejected"), nullptr), std::bad_alloc);
  encoder->fail = false;
  channel->sendMessage(&last, folly::IOBuf::copyBuffer("last"), nullptr);
  for (int i = 0; i < 4; ++i) evb.loopOnce(EVLOOP_NONBLOCK);
  EXPECT_TRUE(transport->good());
  EXPECT_EQ(transport->bytes, (std::vector<std::string>{"firstlast"}));
  channel.reset();
  EXPECT_EQ(first.sent, 1);
  EXPECT_EQ(last.sent, 1);
  EXPECT_EQ(rejected.sent, 0);
  EXPECT_EQ(first.failed + rejected.failed + last.failed, 0);
}

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

TEST_F(UrmaWriteBatch, PreReceivedWrapperPreservesCapabilityAndRecords) {
  auto* inner = new RecordingTransport(&evb);
  auto wrapper = PreReceivedDataAsyncTransportWrapper::create(
      folly::AsyncTransport::UniquePtr(inner), {});
  std::shared_ptr<folly::AsyncTransport> wrapped(std::move(wrapper));
  pipeline->getHandler<TAsyncTransportHandler>(0)->setTransport(wrapped);
  EXPECT_TRUE(detail::usesUrmaRecords(wrapped.get()));
  auto first = pipeline->write(folly::IOBuf::copyBuffer("abc"));
  auto second = pipeline->write(folly::IOBuf::copyBuffer("de"));
  drain();
  EXPECT_TRUE(first.isReady() && !first.hasException());
  EXPECT_TRUE(second.isReady() && !second.hasException());
  EXPECT_EQ(inner->records, (std::vector<std::vector<size_t>>{{3, 2}}));
  EXPECT_EQ(inner->bytes, (std::vector<std::string>{"abcde"}));
  inner->recordsEnabled = false;
  EXPECT_FALSE(detail::usesUrmaRecords(wrapped.get()));
  auto ordinary = pipeline->write(folly::IOBuf::copyBuffer("fallback"));
  drain();
  EXPECT_TRUE(ordinary.isReady() && !ordinary.hasException());
  EXPECT_EQ(inner->ordinaryWrites, 1);
  EXPECT_EQ(inner->bytes.back(), "fallback");
  EXPECT_EQ(inner->records.size(), 1);
  EXPECT_TRUE(state->pending.empty() && state->flushing.empty());
}

TEST_F(UrmaWriteBatch, TransportReplacementRefreshesWriter) {
  auto* adapter = pipeline->getHandler<TAsyncTransportHandler>(0);
  std::shared_ptr<RecordingTransport> replacement(
      new RecordingTransport(&evb), folly::DelayedDestruction::Destructor());
  auto queued = pipeline->write(folly::IOBuf::copyBuffer("queued"));
  adapter->setTransport(replacement);
  drain();
  EXPECT_TRUE(queued.isReady() && !queued.hasException());
  EXPECT_TRUE(transport->bytes.empty());
  EXPECT_EQ(replacement->bytes, (std::vector<std::string>{"queued"}));
  EXPECT_EQ(replacement->records, (std::vector<std::vector<size_t>>{{6}}));

  class OrdinaryTransport final : public folly::AsyncSocket {
   public:
    explicit OrdinaryTransport(folly::EventBase* evb) : AsyncSocket(evb) {}
    bool good() const override { return true; }
    void writeChain(
        WriteCallback* cb, std::unique_ptr<folly::IOBuf>&& data,
        folly::WriteFlags) override {
      folly::io::Cursor cursor(data.get());
      bytes = cursor.readFixedString(data->computeChainDataLength());
      cb->writeSuccess();
    }
    std::string bytes;
  };
  std::shared_ptr<OrdinaryTransport> ordinary(
      new OrdinaryTransport(&evb), folly::DelayedDestruction::Destructor());
  adapter->setTransport(ordinary);
  std::weak_ptr<RecordingTransport> old = replacement;
  replacement.reset();
  EXPECT_TRUE(old.expired());
  auto fallback = pipeline->write(folly::IOBuf::copyBuffer("ordinary"));
  drain();
  EXPECT_TRUE(fallback.isReady() && !fallback.hasException());
  EXPECT_EQ(ordinary->bytes, "ordinary");
  EXPECT_TRUE(state->pending.empty() && state->flushing.empty());

  adapter->setTransport(nullptr);
  adapter->setTransport(transport);
  auto restored = pipeline->write(folly::IOBuf::copyBuffer("restored"));
  drain();
  EXPECT_TRUE(restored.isReady() && !restored.hasException());
  EXPECT_EQ(transport->records, (std::vector<std::vector<size_t>>{{8}}));
  EXPECT_EQ(transport->bytes, (std::vector<std::string>{"restored"}));
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

TEST_F(UrmaWriteBatch, OldTransportDestructionCanWriteThroughReplacement) {
  class ReentrantTransport final : public RecordingTransport {
   public:
    using RecordingTransport::RecordingTransport;
    ~ReentrantTransport() override { onDestroy(); }
    folly::Function<void()> onDestroy;
  };
  handler->queueSends_ = false;
  auto* adapter = pipeline->getHandler<TAsyncTransportHandler>(0);
  std::shared_ptr<ReentrantTransport> old(
      new ReentrantTransport(&evb), folly::DelayedDestruction::Destructor());
  folly::Future<folly::Unit> reentrant = folly::makeFuture();
  old->onDestroy = [&] {
    reentrant = pipeline->write(folly::IOBuf::copyBuffer("replacement"));
  };
  adapter->setTransport(old);
  old.reset(); // The adapter now owns the last reference.
  adapter->setTransport(transport);
  EXPECT_TRUE(reentrant.isReady() && !reentrant.hasException());
  EXPECT_EQ(transport->bytes, (std::vector<std::string>{"replacement"}));
  EXPECT_EQ(transport->records, (std::vector<std::vector<size_t>>{{11}}));
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
