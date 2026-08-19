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

#include <folly/init/Init.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <folly/io/IOBufQueue.h>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/EventBase.h>
#include <folly/lang/Bits.h>
#include <folly/portability/GTest.h>
#include <wangle/channel/Handler.h>

#include <thrift/lib/cpp/transport/THeader.h>
#include <thrift/lib/cpp2/async/FramingHandler.h>
#include <thrift/lib/cpp2/async/HeaderClientChannel.h>
#include <thrift/lib/cpp2/async/HeaderServerChannel.h>

#include <cstring>
#include <stdexcept>

using namespace apache::thrift;
using namespace apache::thrift::transport;
using namespace folly;

static std::unique_ptr<IOBuf> makeHeaderMsg(
    size_t sz, THeader::StringToStringMap& h) {
  THeader hdr;
  auto buf = IOBuf::create(sz);
  if (sz > 0)
    buf->writableData()[0] = 0x80;
  std::memset(buf->writableData() + 1, 'X', sz > 1 ? sz - 1 : 0);
  buf->append(sz);
  return hdr.addHeader(std::move(buf), h);
}

static std::shared_ptr<AsyncTransport> makeTransport() {
  static EventBase evb;
  return AsyncSocket::newSocket(&evb);
}

// HeaderServerChannel::ServerFramingHandler::removeFrame

TEST(ServerFramingHandlerTest, RemoveFrame) {
  auto transport = makeTransport();
  auto channel = HeaderServerChannel::newChannel(transport);
  HeaderServerChannel::ServerFramingHandler handler(*channel);
  THeader::StringToStringMap ph;
  IOBufQueue q;
  q.append(makeHeaderMsg(100, ph));
  auto [buf, rem, hdr, fl] = handler.removeFrame(&q);
  ASSERT_NE(nullptr, buf);
  EXPECT_GT(fl, 0u);
}

TEST(ServerFramingHandlerTest, RemoveFrameEmpty) {
  auto transport = makeTransport();
  auto channel = HeaderServerChannel::newChannel(transport);
  HeaderServerChannel::ServerFramingHandler handler(*channel);
  IOBufQueue q;
  auto [buf, rem, hdr, fl] = handler.removeFrame(&q);
  EXPECT_EQ(nullptr, buf);
  EXPECT_EQ(0u, fl);
}

TEST(ServerFramingHandlerTest, RemoveFrameNull) {
  auto transport = makeTransport();
  auto channel = HeaderServerChannel::newChannel(transport);
  HeaderServerChannel::ServerFramingHandler handler(*channel);
  auto [buf, rem, hdr, fl] = handler.removeFrame(nullptr);
  EXPECT_EQ(nullptr, buf);
  EXPECT_EQ(0u, fl);
}

// HeaderClientChannel::ClientFramingHandler::removeFrame

TEST(ClientFramingHandlerTest, RemoveFrameValidMessage) {
  static EventBase evb;
  folly::AsyncTransport::UniquePtr transport(
      folly::AsyncSocket::newSocket(&evb));
  auto channel = HeaderClientChannel::newChannel(
      HeaderClientChannel::WithoutRocketUpgrade{}, std::move(transport));
  HeaderClientChannel::ClientFramingHandler handler(*channel);
  THeader::StringToStringMap ph;
  IOBufQueue q;
  q.append(makeHeaderMsg(100, ph));
  auto [buf, rem, hdr, fl] = handler.removeFrame(&q);
  ASSERT_NE(nullptr, buf);
  EXPECT_GT(fl, 0u);
}

TEST(ClientFramingHandlerTest, RemoveFrameEmpty) {
  static EventBase evb;
  folly::AsyncTransport::UniquePtr transport(
      folly::AsyncSocket::newSocket(&evb));
  auto channel = HeaderClientChannel::newChannel(
      HeaderClientChannel::WithoutRocketUpgrade{}, std::move(transport));
  HeaderClientChannel::ClientFramingHandler handler(*channel);
  IOBufQueue q;
  auto [buf, rem, hdr, fl] = handler.removeFrame(&q);
  EXPECT_EQ(nullptr, buf);
  EXPECT_EQ(0u, fl);
}

TEST(ClientFramingHandlerTest, RemoveFrameNull) {
  static EventBase evb;
  folly::AsyncTransport::UniquePtr transport(
      folly::AsyncSocket::newSocket(&evb));
  auto channel = HeaderClientChannel::newChannel(
      HeaderClientChannel::WithoutRocketUpgrade{}, std::move(transport));
  HeaderClientChannel::ClientFramingHandler handler(*channel);
  auto [buf, rem, hdr, fl] = handler.removeFrame(nullptr);
  EXPECT_EQ(nullptr, buf);
  EXPECT_EQ(0u, fl);
}

// FramingHandler::read — covers refreshReadBuffer + avgRequestSize_ sliding
// window

using FramedMessage =
    std::pair<std::unique_ptr<IOBuf>, std::unique_ptr<THeader>>;
using OutboundFramedMessage = std::pair<std::unique_ptr<IOBuf>, THeader*>;

class RecordingHandler : public wangle::Handler<
                             FramedMessage,
                             FramedMessage,
                             std::unique_ptr<IOBuf>,
                             OutboundFramedMessage> {
 public:
  std::vector<size_t> frameLengths;
  size_t exceptionCount{0};
  void read(
      Context* ctx,
      std::pair<std::unique_ptr<IOBuf>, std::unique_ptr<THeader>> msg)
      override {
    frameLengths.push_back(msg.first->computeChainDataLength());
    ctx->fireRead(std::move(msg));
  }

  folly::Future<folly::Unit> write(
      Context* ctx, std::unique_ptr<IOBuf> msg) override {
    return ctx->fireWrite(std::make_pair(std::move(msg), nullptr));
  }

  void readException(Context*, folly::exception_wrapper) override {
    ++exceptionCount;
  }
};

class CloseRecordingHandler
    : public wangle::HandlerAdapter<IOBufQueue&, std::unique_ptr<IOBuf>> {
 public:
  size_t closeCount{0};
  std::vector<size_t> writtenLengths;

  folly::Future<folly::Unit> close(Context* ctx) override {
    ++closeCount;
    return ctx->fireClose();
  }

  folly::Future<folly::Unit> write(
      Context*, std::unique_ptr<IOBuf> msg) override {
    writtenLengths.push_back(msg->computeChainDataLength());
    return folly::makeFuture(folly::Unit{});
  }
};

class MockFramingHandler : public FramingHandler {
 public:
  bool throwOnRemove{false};
  bool emitZeroLengthFrame{false};

  std::tuple<std::unique_ptr<IOBuf>, size_t, std::unique_ptr<THeader>, size_t>
  removeFrame(IOBufQueue* q) override {
    if (throwOnRemove) {
      throw std::runtime_error("test framing failure");
    }
    if (emitZeroLengthFrame) {
      emitZeroLengthFrame = false;
      return {IOBuf::create(0), 0, nullptr, 0};
    }
    if (!q || !q->front() || q->front()->empty()) {
      return {nullptr, 0, nullptr, 0};
    }

    // Simulate framed protocol: first 4 bytes = frame length
    size_t total = q->front()->computeChainDataLength();
    if (total < 4) {
      return {nullptr, 4 - total, nullptr, 0};
    }

    folly::io::Cursor c(q->front());
    uint32_t frameLen = c.readBE<uint32_t>();
    size_t frameTotal = 4 + frameLen;

    if (total < frameTotal) {
      return {nullptr, frameTotal - total, nullptr, 0};
    }

    q->trimStart(4);
    auto buf = q->split(frameLen);
    return {std::move(buf), 0, nullptr, frameTotal};
  }

  std::unique_ptr<IOBuf> addFrame(
      std::unique_ptr<IOBuf> buf, THeader*) override {
    return buf;
  }

  uint16_t getProtocolId() { return 0; }
  CLIENT_TYPE getClientType() { return THRIFT_HEADER_CLIENT_TYPE; }
};

static std::unique_ptr<IOBuf> makeFramedMsg(size_t payloadSize) {
  auto buf = IOBuf::create(4 + payloadSize);
  uint32_t nbo = folly::Endian::big(uint32_t(payloadSize));
  std::memcpy(buf->writableData(), &nbo, 4);
  std::memset(buf->writableData() + 4, 'X', payloadSize);
  buf->append(4 + payloadSize);
  return buf;
}

static std::unique_ptr<IOBuf> makePartialFramedMsg(
    size_t declaredPayloadSize, size_t availablePayloadSize, size_t capacity) {
  auto buf = IOBuf::create(capacity);
  uint32_t nbo = folly::Endian::big(uint32_t(declaredPayloadSize));
  std::memcpy(buf->writableData(), &nbo, 4);
  std::memset(buf->writableData() + 4, 'X', availablePayloadSize);
  buf->append(4 + availablePayloadSize);
  return buf;
}

static auto makeReadPipeline(
    const std::shared_ptr<MockFramingHandler>& mockHandler,
    const std::shared_ptr<RecordingHandler>& recorder) {
  auto pipeline =
      wangle::Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  pipeline->addBack(mockHandler);
  pipeline->addBack(recorder);
  pipeline->finalize();
  return pipeline;
}

TEST(FramingHandlerReadTest, ReadWithFullFrame) {
  auto mockHandler = std::make_shared<MockFramingHandler>();
  auto recorder = std::make_shared<RecordingHandler>();
  auto pipeline =
      wangle::Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  pipeline->addBack(mockHandler);
  pipeline->addBack(recorder);
  pipeline->finalize();

  IOBufQueue q;
  q.append(makeFramedMsg(100));
  pipeline->read(q);
  EXPECT_EQ(1u, recorder->frameLengths.size());
  EXPECT_EQ(100u, recorder->frameLengths[0]);
  auto [readSize, allocationSize] = pipeline->getReadBufferSettings();
  EXPECT_EQ(2048u, readSize);
  EXPECT_EQ(2048u, allocationSize);
}

TEST(FramingHandlerReadTest, ReadWithPartialFrame) {
  auto mockHandler = std::make_shared<MockFramingHandler>();
  auto recorder = std::make_shared<RecordingHandler>();
  auto pipeline =
      wangle::Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  pipeline->addBack(mockHandler);
  pipeline->addBack(recorder);
  pipeline->finalize();

  // Feed only 2 bytes of a 4-byte frame header — triggers partial frame path
  IOBufQueue q;
  auto partial = IOBuf::create(2);
  uint32_t nbo = folly::Endian::big(uint32_t(100));
  std::memcpy(partial->writableData(), &nbo, 2);
  partial->append(2);
  q.append(std::move(partial));
  pipeline->read(q);
  EXPECT_EQ(0u, recorder->frameLengths.size()); // no full frame received
  auto [readSize, allocationSize] = pipeline->getReadBufferSettings();
  EXPECT_EQ(2048u, readSize);
  EXPECT_EQ(2u, allocationSize);
}

TEST(FramingHandlerReadTest, ReadMultipleFramesEMA) {
  auto mockHandler = std::make_shared<MockFramingHandler>();
  auto recorder = std::make_shared<RecordingHandler>();
  auto pipeline =
      wangle::Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  pipeline->addBack(mockHandler);
  pipeline->addBack(recorder);
  pipeline->finalize();

  // Feed 3 frames of different sizes in one queue — triggers EMA sliding window
  IOBufQueue q;
  q.append(makeFramedMsg(4000));
  q.append(makeFramedMsg(8000));
  q.append(makeFramedMsg(2000));
  pipeline->read(q);
  EXPECT_EQ(3u, recorder->frameLengths.size());
  EXPECT_EQ(4000u, recorder->frameLengths[0]);
  EXPECT_EQ(8000u, recorder->frameLengths[1]);
  EXPECT_EQ(2000u, recorder->frameLengths[2]);
  // EMA frame lengths: 4004 -> 4404 -> 4164; read size = 4164 * 16.
  auto [readSize, allocationSize] = pipeline->getReadBufferSettings();
  EXPECT_EQ(66624u, readSize);
  EXPECT_EQ(66624u, allocationSize);
}

TEST(FramingHandlerReadTest, StrictReadBufferSize) {
  auto mockHandler = std::make_shared<MockFramingHandler>();
  auto recorder = std::make_shared<RecordingHandler>();
  auto pipeline = makeReadPipeline(mockHandler, recorder);
  mockHandler->setReadBufferSize(1234, true);

  IOBufQueue q;
  pipeline->read(q);
  auto [readSize, allocationSize] = pipeline->getReadBufferSettings();
  EXPECT_EQ(1234u, readSize);
  EXPECT_EQ(1234u, allocationSize);
}

TEST(FramingHandlerReadTest, AdaptiveIntermediateReadBufferSize) {
  auto mockHandler = std::make_shared<MockFramingHandler>();
  auto recorder = std::make_shared<RecordingHandler>();
  auto pipeline = makeReadPipeline(mockHandler, recorder);

  IOBufQueue q;
  q.append(makeFramedMsg(4000));
  pipeline->read(q);
  auto [readSize, allocationSize] = pipeline->getReadBufferSettings();
  EXPECT_EQ((4000u + 4u) * 16u, readSize);
  EXPECT_EQ(readSize, allocationSize);
}

TEST(FramingHandlerReadTest, AdaptiveReadBufferSizeClampedToMaximum) {
  auto mockHandler = std::make_shared<MockFramingHandler>();
  auto recorder = std::make_shared<RecordingHandler>();
  auto pipeline = makeReadPipeline(mockHandler, recorder);

  IOBufQueue q;
  q.append(makeFramedMsg(40000));
  pipeline->read(q);
  auto [readSize, allocationSize] = pipeline->getReadBufferSettings();
  EXPECT_EQ(524288u, readSize);
  EXPECT_EQ(524288u, allocationSize);
}

TEST(FramingHandlerReadTest, PartialBodyUsesRemainingBytes) {
  auto mockHandler = std::make_shared<MockFramingHandler>();
  auto recorder = std::make_shared<RecordingHandler>();
  auto pipeline = makeReadPipeline(mockHandler, recorder);

  IOBufQueue q;
  q.append(makePartialFramedMsg(100, 10, 14));
  pipeline->read(q);
  auto [readSize, allocationSize] = pipeline->getReadBufferSettings();
  EXPECT_EQ(2048u, readSize);
  EXPECT_EQ(90u, allocationSize);
}

TEST(FramingHandlerReadTest, PartialBodyUsesLargerTailroom) {
  auto mockHandler = std::make_shared<MockFramingHandler>();
  auto recorder = std::make_shared<RecordingHandler>();
  auto pipeline = makeReadPipeline(mockHandler, recorder);

  IOBufQueue q;
  q.append(makePartialFramedMsg(100, 10, 256));
  ASSERT_EQ(242u, q.tailroom());
  pipeline->read(q);
  auto [readSize, allocationSize] = pipeline->getReadBufferSettings();
  EXPECT_EQ(2048u, readSize);
  EXPECT_EQ(242u, allocationSize);
}

TEST(FramingHandlerReadTest, ZeroLengthFrameKeepsMinimumReadBuffer) {
  auto mockHandler = std::make_shared<MockFramingHandler>();
  auto recorder = std::make_shared<RecordingHandler>();
  auto pipeline = makeReadPipeline(mockHandler, recorder);
  mockHandler->emitZeroLengthFrame = true;

  IOBufQueue q;
  pipeline->read(q);
  ASSERT_EQ(1u, recorder->frameLengths.size());
  EXPECT_EQ(0u, recorder->frameLengths[0]);
  auto [readSize, allocationSize] = pipeline->getReadBufferSettings();
  EXPECT_EQ(2048u, readSize);
  EXPECT_EQ(2048u, allocationSize);
}

TEST(FramingHandlerReadTest, RemoveFrameExceptionIsForwardedAndCloses) {
  auto closeRecorder = std::make_shared<CloseRecordingHandler>();
  auto mockHandler = std::make_shared<MockFramingHandler>();
  auto recorder = std::make_shared<RecordingHandler>();
  auto pipeline =
      wangle::Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  pipeline->addBack(closeRecorder);
  pipeline->addBack(mockHandler);
  pipeline->addBack(recorder);
  pipeline->finalize();
  mockHandler->throwOnRemove = true;

  IOBufQueue q;
  q.append(makeFramedMsg(100));
  pipeline->read(q);
  EXPECT_EQ(1u, recorder->exceptionCount);
  EXPECT_EQ(1u, closeRecorder->closeCount);
  EXPECT_TRUE(recorder->frameLengths.empty());
}

TEST(FramingHandlerWriteTest, AddedFrameIsForwardedToTransport) {
  auto transportRecorder = std::make_shared<CloseRecordingHandler>();
  auto mockHandler = std::make_shared<MockFramingHandler>();
  auto recorder = std::make_shared<RecordingHandler>();
  auto pipeline =
      wangle::Pipeline<IOBufQueue&, std::unique_ptr<IOBuf>>::create();
  pipeline->addBack(transportRecorder);
  pipeline->addBack(mockHandler);
  pipeline->addBack(recorder);
  pipeline->finalize();

  auto message = makeFramedMsg(123);
  const auto expectedLength = message->computeChainDataLength();
  pipeline->write(std::move(message)).get();

  ASSERT_EQ(1u, transportRecorder->writtenLengths.size());
  EXPECT_EQ(expectedLength, transportRecorder->writtenLengths[0]);
}

int main(int argc, char** argv) {
  folly::InitOptions opts;
  opts.use_gflags = false;
  folly::init(&argc, &argv, opts);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
