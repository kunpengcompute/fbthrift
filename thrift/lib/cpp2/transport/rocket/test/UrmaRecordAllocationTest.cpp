/* Copyright (c) Huawei Technologies Co., Ltd. 2026. Licensed under Apache-2.0.
 */
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>
#include <folly/init/Init.h>
#include <folly/io/Cursor.h>
#include <thrift/lib/cpp2/transport/rocket/client/RequestContextQueue.h>
#include <thrift/lib/cpp2/transport/rocket/client/RocketClient.h>
#ifdef FOLLY_HAVE_URMA
#include <folly/io/async/urma/UrmaRecordWriter.h>
#endif

namespace {
thread_local bool failNextAllocation = false;
void require(bool value, const char* message) {
  if (!value) {
    throw std::runtime_error(message);
  }
}
#ifdef FOLLY_HAVE_URMA
class AllocationFaultTransport : public folly::AsyncSocket,
                                 public folly::UrmaRecordWriter {
 public:
  explicit AllocationFaultTransport(folly::EventBase* evb) : AsyncSocket(evb) {}
  void setReadCB(ReadCallback*) override {}
  bool supportsUrmaRecords() const override {
    failNextAllocation = true;
    return true;
  }
  void writeChainWithRecords(
      WriteCallback*,
      std::unique_ptr<folly::IOBuf>&&,
      std::vector<size_t>,
      folly::WriteFlags) noexcept override {
    // Metadata allocation must fail before reaching transport submission.
    std::abort();
  }
};
class Completion
    : public apache::thrift::rocket::RocketClient::RequestFnfCallback {
 public:
  Completion(int& calls, int& errors) : calls_(calls), errors_(errors) {}
  void onWrite(folly::Try<void> result) noexcept override {
    ++calls_;
    errors_ += result.hasException();
  }

 private:
  int& calls_;
  int& errors_;
};

void checkRocketClose() {
  using namespace apache::thrift::rocket;
  folly::EventBase evb;
  auto client = RocketClient::create(
      evb,
      folly::AsyncTransport::UniquePtr(new AllocationFaultTransport(&evb)),
      nullptr);
  int calls = 0, errors = 0;
  for (int i = 0; i < 2; ++i) {
    client->sendRequestFnf(
        Payload::makeFromData(folly::IOBuf::copyBuffer("record")),
        std::make_unique<Completion>(calls, errors));
  }
  for (int i = 0; i < 10; ++i) {
    evb.loopOnce(EVLOOP_NONBLOCK);
  }
  require(!client->isAlive(), "allocation failure did not close Rocket client");
  require(calls == 2 && errors == 2, "Rocket did not fail each request once");
  client.reset();
  require(calls == 2 && errors == 2, "Rocket destruction repeated a callback");
  std::cout
      << "PASS Rocket metadata failure closes with exactly-once callbacks\n";
}
#endif
} // namespace

// Keep the one-shot allocator override in its own test executable.
void* operator new(std::size_t size) {
  if (failNextAllocation) {
    failNextAllocation = false;
    throw std::bad_alloc();
  }
  if (auto* p = std::malloc(size ? size : 1)) {
    return p;
  }
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) {
  return ::operator new(size);
}
void operator delete(void* p) noexcept {
  std::free(p);
}
void operator delete[](void* p) noexcept {
  std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
  std::free(p);
}
void operator delete[](void* p, std::size_t) noexcept {
  std::free(p);
}

int main(int argc, char** argv) {
  folly::Init init(&argc, &argv);
  using namespace apache::thrift::rocket;
  try {
    for (bool close : {false, true}) {
      RequestContextQueue queue;
      auto makeFrame = [](uint32_t id, const char* value) {
        return RequestFnfFrame(
            StreamId{id},
            Payload::makeFromData(folly::IOBuf::copyBuffer(value)));
      };
      auto expected = makeFrame(1, "first-record").serialize();
      expected->prependChain(makeFrame(3, "second-record-longer").serialize());
      folly::io::Cursor expectedCursor(expected.get());
      auto expectedBytes =
          expectedCursor.readFixedString(expected->computeChainDataLength());
      RequestContext first(makeFrame(1, "first-record"), queue);
      RequestContext second(makeFrame(3, "second-record-longer"), queue);
      queue.enqueueScheduledWrite(first);
      queue.enqueueScheduledWrite(second);
      std::vector<size_t> lengths;
      bool threw = false;
      failNextAllocation = true;
      try {
        queue.getNextScheduledWritesBatch(&lengths);
      } catch (const std::bad_alloc&) {
        threw = true;
      }
      failNextAllocation = false;
      require(threw, "record allocation failure was not injected");
      require(
          first.state() == RequestContext::State::WRITE_SCHEDULED &&
              second.state() == RequestContext::State::WRITE_SCHEDULED,
          "allocation failure changed request state");
      require(lengths.empty(), "partial record metadata escaped");
      if (close) {
        auto error = folly::make_exception_wrapper<std::bad_alloc>();
        queue.failAllScheduledWrites(error);
        require(
            first.state() == RequestContext::State::COMPLETE &&
                second.state() == RequestContext::State::COMPLETE,
            "close did not complete scheduled requests");
        require(
            first.waitForWriteToComplete().hasException() &&
                second.waitForWriteToComplete().hasException(),
            "close did not report failure to requests");
        queue.failAllScheduledWrites(
            error); // Already drained; no second completion.
        require(
            !queue.getNextScheduledWritesBatch(&lengths), "queue not drained");
      } else {
        auto data = queue.getNextScheduledWritesBatch(&lengths);
        queue.markNextSendingBatchAsSent(
            [&](RequestContext& req) { queue.markAsResponded(req); });
        require(data && lengths.size() == 2, "retry lost records");
        folly::io::Cursor actual(data.get());
        require(
            actual.readFixedString(data->computeChainDataLength()) ==
                expectedBytes,
            "retry changed record bytes/order");
        require(
            !first.waitForWriteToComplete().hasException() &&
                !second.waitForWriteToComplete().hasException(),
            "retry did not complete requests");
      }
      std::cout << "PASS record allocation failure: "
                << (close ? "close" : "retry") << '\n';
    }
#ifdef FOLLY_HAVE_URMA
    checkRocketClose();
#endif
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
