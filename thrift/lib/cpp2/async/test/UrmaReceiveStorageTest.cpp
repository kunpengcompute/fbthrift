/* Copyright (c) Huawei Technologies Co., Ltd. 2026. Licensed under Apache-2.0.
 */
#include <folly/init/Init.h>
#include <folly/portability/GTest.h>
#ifdef FOLLY_HAVE_URMA
#include <dlfcn.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/io/async/urma/UrmaAsyncSocket.h>
#include <folly/io/async/urma/UrmaCleanup.h>
#include <folly/io/async/urma/UrmaInit.h>
#include <folly/io/async/urma/UrmaIoContext.h>
#include <thrift/lib/cpp/transport/THeader.h>
#include <thrift/lib/cpp2/async/TAsyncTransportHandler.h>
#include <thrift/lib/cpp2/protocol/BinaryProtocol.h>
#include <thrift/lib/cpp2/transport/rocket/framing/Frames.h>
#include <thrift/lib/cpp2/transport/rocket/framing/Parser.h>
#include <wangle/channel/StaticPipeline.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <map>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using namespace folly;
using namespace apache::thrift;
using namespace std::chrono_literals;
constexpr size_t kBody = 32768, kMessages = 8;
struct DmaRange {
  uintptr_t start;
  size_t size;
  bool read;
  bool contains(uintptr_t p, size_t n) const {
    return p >= start && p - start <= size && n <= size - (p - start);
  }
};
std::mutex traceMutex;
using Key = std::pair<urma_jfc_t*, uint64_t>;
std::map<Key, DmaRange> postedRx, postedRead;
std::vector<DmaRange> completed, protectedStorage, serializedSources;
size_t replacements = 0, offers = 0;
void check(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template <class F>
void pump(EventBase& evb, F done) {
  const auto deadline = std::chrono::steady_clock::now() + 10s;
  while (!done() && std::chrono::steady_clock::now() < deadline) {
    evb.loopOnce(EVLOOP_NONBLOCK);
    std::this_thread::sleep_for(100us);
  }
  check(done(), "functional transfer did not complete");
}
uint8_t pattern(size_t i, size_t message) {
  return uint8_t((i * 31 + message + 20) % 251);
}
void verifyStorage(const IOBuf& chain, bool expectRead) {
  std::lock_guard<std::mutex> lock(traceMutex);
  const IOBuf* p = &chain;
  do {
    if (p->length()) {
      const auto address = reinterpret_cast<uintptr_t>(p->data());
      check(std::any_of(completed.begin(), completed.end(), [&](const DmaRange& r) {
        return r.read == expectRead && r.contains(address, p->length());
      }), "protocol payload does not reference the actual completed DMA destination");
      auto block = UrmaInit::lookupSegment(p->data(), p->length());
      check(bool(block), "protocol payload lost its registered allocation");
      protectedStorage.push_back(
          {reinterpret_cast<uintptr_t>(block.data()), block.capacity(), expectRead});
    }
    p = p->next();
  } while (p != &chain);
}
struct Capture {
  bool read;
  size_t bodyBytes;
  size_t count{0};
  std::vector<std::unique_ptr<IOBuf>> retained;
  Capture(bool value, size_t bytes) : read(value), bodyBytes(bytes) {}
  void accept(std::unique_ptr<IOBuf> body) {
    check(body != nullptr, "empty protocol body");
    verifyStorage(*body, read);
    BinaryProtocolReader protocol;
    protocol.setInput(body.get());
    std::unique_ptr<IOBuf> shared;
    protocol.readBinary(shared);
    verifyStorage(*shared, read);
    check(shared->computeChainDataLength() == bodyBytes, "binary IOBuf length changed");
    protocol.setInput(body.get());
    std::string ordinary;
    protocol.readBinary(ordinary);
    check(ordinary.size() == bodyBytes &&
              !UrmaInit::lookupSegment(ordinary.data(), ordinary.size()),
          "std::string boundary was incorrectly described as shared storage");
    for (size_t i = 0; i < bodyBytes; ++i)
      check(uint8_t(ordinary[i]) == pattern(i, count), "binary body corrupted");
    retained.push_back(shared->clone());
    ++count;
  }
  void verifyRetained() const {
    for (size_t message = 0; message < retained.size(); ++message) {
      size_t i = 0;
      for (auto range : *retained[message])
        for (uint8_t byte : range)
          check(byte == pattern(i++, message), "held body overwritten after refill/close");
      check(i == bodyBytes, "held body length changed");
    }
  }
};
class HeaderSink : public wangle::BytesToBytesHandler {
 public:
  explicit HeaderSink(Capture& capture) : capture_(capture) {}
  void read(Context*, IOBufQueue& q) override {
    size_t needed = 0;
    while (auto body = header_.removeHeader(&q, needed, headers_))
      capture_.accept(std::move(body));
  }
 private:
  Capture& capture_;
  transport::THeader header_;
  transport::THeader::StringToStringMap headers_;
};
struct RocketOwner : DelayedDestruction {
  Capture& capture;
  bool error{false};
  size_t charged{0};
  explicit RocketOwner(Capture& value) : capture(value) {}
  void handleFrame(std::unique_ptr<IOBuf> buffer) {
    rocket::RequestFnfFrame frame(std::move(buffer));
    capture.accept(std::move(frame.payload()).data());
  }
  void close(exception_wrapper) noexcept { error = true; }
  void scheduleTimeout(HHWheelTimer::Callback*, const std::chrono::milliseconds&) {}
  bool incMemoryUsage(uint32_t n) { charged += n; return true; }
  void decMemoryUsage(uint32_t n) { charged -= n; }
  std::unique_ptr<IOBuf> customAlloc(size_t) { return nullptr; }
};
struct Listener : AsyncServerSocket::AcceptCallback {
  EventBase& evb;
  std::shared_ptr<UrmaAsyncSocket> socket;
  bool error{false};
  explicit Listener(EventBase& value) : evb(value) {}
  void connectionAccepted(NetworkSocket fd, const SocketAddress&, AcceptInfo) noexcept override {
    try {
      socket = {new UrmaAsyncSocket(&evb, fd.toFd()), DelayedDestruction::Destructor()};
    } catch (...) { error = true; }
  }
  void acceptError(const std::exception&) noexcept override { error = true; }
};
struct Writes : AsyncTransport::WriteCallback {
  size_t done{0}, errors{0};
  void writeSuccess() noexcept override { ++done; }
  void writeErr(size_t, const AsyncSocketException&) noexcept override { ++errors; }
};

void exercise(bool useRocket, bool useRead, size_t bodyBytes = kBody) {
  {
    std::lock_guard<std::mutex> lock(traceMutex);
    postedRx.clear(); postedRead.clear(); completed.clear();
    protectedStorage.clear(); serializedSources.clear();
    replacements = offers = 0;
  }
  UrmaConfig config;
  config.dev_name = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  config.enable_read = useRead;
  config.rx_depth = 4; // Eight frames force actual slot reuse while bodies are held.
  UrmaInit::init(config);
  Capture capture(useRead, bodyBytes);
  {
    EventBase evb;
    Listener accepted(evb);
    auto listener = AsyncServerSocket::newSocket(&evb);
    listener->bind(SocketAddress("127.0.0.1", 0));
    listener->listen(4);
    listener->addAcceptCallback(&accepted, &evb);
    listener->startAccepting();
    SocketAddress address;
    listener->getAddress(&address);
    std::shared_ptr<UrmaAsyncSocket> client(
        new UrmaAsyncSocket(&evb, address), DelayedDestruction::Destructor());
    pump(evb, [&] { return accepted.socket && accepted.socket->good() && client->good(); });
    check(!accepted.error, "accept failed");
    using Pipeline = wangle::StaticPipeline<IOBufQueue&, std::unique_ptr<IOBuf>,
        TAsyncTransportHandler, HeaderSink>;
    std::shared_ptr<Pipeline> pipeline;
    RocketOwner rocketOwner(capture);
    rocket::Parser<RocketOwner> parser(rocketOwner);
    if (useRocket) {
      accepted.socket->setReadCB(&parser);
    } else {
      pipeline = Pipeline::create(TAsyncTransportHandler(accepted.socket),
                                  std::make_shared<HeaderSink>(capture));
      pipeline->getHandler<TAsyncTransportHandler>(0)->attachReadCallback();
    }
    Writes writes;
    for (size_t message = 0; message < kMessages; ++message) {
      // Serialize into a preallocated IOBuf through the real protocol writer.
      // The application string is copied by serialization; transport must
      // preserve the resulting registered storage after this boundary.
      std::string input(bodyBytes, '\0');
      for (size_t i = 0; i < bodyBytes; ++i) input[i] = char(pattern(i, message));
      IOBufQueue output(IOBufQueue::cacheChainLength());
      output.append(IOBuf::create(bodyBytes + 4));
      BinaryProtocolWriter protocol;
      protocol.setOutput(&output);
      protocol.writeBinary(StringPiece(input));
      auto body = output.move();
      check(UrmaInit::ownsPoolBuffer(*body), "serializer output missed registered TLS");
      check(!body->isChained() && body->length() == bodyBytes + 4,
            "preallocated serialized body was unexpectedly partitioned");
      {
        std::lock_guard<std::mutex> lock(traceMutex);
        serializedSources.push_back({reinterpret_cast<uintptr_t>(body->data()), body->length(), true});
      }
      std::unique_ptr<IOBuf> frame;
      if (useRocket) {
        frame = rocket::RequestFnfFrame(rocket::StreamId{uint32_t(message * 2 + 1)},
                  rocket::Payload::makeFromData(std::move(body))).serialize();
      } else {
        transport::THeader header;
        transport::THeader::StringToStringMap headers;
        frame = header.addHeader(std::move(body), headers);
      }
      client->writeChain(&writes, std::move(frame));
    }
    pump(evb, [&] { return writes.done == kMessages && capture.count == kMessages; });
    check(!writes.errors && !rocketOwner.error && !rocketOwner.charged, "framework callback/error accounting");
    const auto tx = client->getCopyStats(), rx = accepted.socket->getCopyStats();
    check(!tx.copied_send_bytes && !rx.copied_send_bytes && !rx.copied_read_target_bytes,
          "registered functional path performed a transport payload copy");
    check(useRead ? (offers == kMessages && rx.read_wr_submitted == kMessages &&
                     tx.direct_read_source_bytes == rx.transferred_read_target_bytes)
                  : (!offers && !rx.read_wr_submitted), "READ toggle or byte accounting mismatch");
    check(replacements > config.rx_depth && !UrmaInit::poolStats().exported_bytes,
          "RX slots did not refill or sources still exported after FREE");
    std::cout << (useRocket ? "Rocket" : "Header")
              << (useRead ? " READ" : " SEND") << " body_bytes=" << bodyBytes
              << " messages=" << capture.count
              << " copied_send_bytes=" << tx.copied_send_bytes << '/' << rx.copied_send_bytes
              << " copied_receive_bytes=" << rx.copied_read_target_bytes
              << " read_count=" << rx.read_wr_submitted
              << " read_bytes=" << tx.direct_read_source_bytes << '/' << rx.transferred_read_target_bytes
              << " slot_replacements=" << replacements << '\n';
    capture.verifyRetained();
    accepted.socket->setReadCB(nullptr);
    pipeline.reset();
    client->closeNow(); accepted.socket->closeNow();
    pump(evb, [] { return urma_detail::UrmaIoContext::activeRoutes() == 0; });
    listener->removeAcceptCallback(&accepted, &evb);
    listener->stopAccepting();
    evb.loopOnce(EVLOOP_NONBLOCK);
  }
  UrmaInit::trimPool();
  UrmaInit::fini();
  check(UrmaInit::isInitialized(), "shutdown released protocol-owned receive storage");
  capture.verifyRetained();
  std::thread release([held = std::move(capture.retained)]() mutable { held.clear(); });
  release.join();
  // Context teardown completes inside a queued task. Wait for the task's
  // removal as well; isInitialized() may turn false before pending_ drops.
  check(urma_detail::waitForCleanups(5s) && !UrmaInit::isInitialized() &&
            !UrmaInit::pendingCleanupCount(),
        "late cross-thread return leaked pool/context");
  std::cout << "PASS " << (useRocket ? "Rocket" : "Header")
            << (useRead ? " READ" : " SEND")
            << " DMA address -> protocol -> IOBuf binary identity; slot replacement; "
               "held clone after close/fini; std::string copies; final release\n";
}
} // namespace

// Observe real SDK buffers/completions; never replace DMA or manufacture CQEs.
extern "C" urma_status_t urma_post_jfr_wr(urma_jfr_t* q, urma_jfr_wr_t* wr, urma_jfr_wr_t** bad) {
  static auto real = reinterpret_cast<decltype(&urma_post_jfr_wr)>(dlsym(RTLD_NEXT, "urma_post_jfr_wr"));
  std::lock_guard<std::mutex> lock(traceMutex);
  for (const auto& range : protectedStorage)
    check(!range.contains(wr->src.sge[0].addr, 1), "reposted a block still owned by protocol/business IOBuf");
  const auto result = real(q, wr, bad);
  if (result == URMA_SUCCESS) {
    const Key key{q->jfr_cfg.jfc, wr->user_ctx};
    replacements += postedRx.count(key);
    postedRx[key] = {wr->src.sge[0].addr, wr->src.sge[0].len, false};
  }
  return result;
}
extern "C" urma_status_t urma_post_jfs_wr(urma_jfs_t* q, urma_jfs_wr_t* wr, urma_jfs_wr_t** bad) {
  static auto real = reinterpret_cast<decltype(&urma_post_jfs_wr)>(dlsym(RTLD_NEXT, "urma_post_jfs_wr"));
  std::lock_guard<std::mutex> lock(traceMutex);
  const auto result = real(q, wr, bad);
  if (result == URMA_SUCCESS && wr->opcode == URMA_OPC_READ)
    postedRead[{q->jfs_cfg.jfc, wr->user_ctx}] = {wr->rw.dst.sge[0].addr, wr->rw.dst.sge[0].len, true};
  if (result == URMA_SUCCESS && wr->opcode == URMA_OPC_SEND_IMM && wr->send.src.sge[0].len == 32) {
    const size_t bytes = folly::urma_detail::getInteger(reinterpret_cast<const uint8_t*>(wr->send.src.sge[0].addr) + 24, 4);
    check(std::any_of(serializedSources.begin(), serializedSources.end(), [&](const DmaRange& range) {
      return range.contains(wr->send.imm_data, bytes);
    }), "PULL source differs from original registered serializer output");
    ++offers;
  }
  return result;
}
extern "C" int urma_poll_jfc(urma_jfc_t* cq, int count, urma_cr_t* out) {
  static auto real = reinterpret_cast<decltype(&urma_poll_jfc)>(dlsym(RTLD_NEXT, "urma_poll_jfc"));
  const int n = real(cq, count, out);
  std::lock_guard<std::mutex> lock(traceMutex);
  for (int i = 0; i < n; ++i) {
    const auto& c = out[i];
    if (c.status != URMA_CR_SUCCESS) continue;
    const Key key{cq, c.user_ctx};
    auto rx = postedRx.find(key);
    if (rx != postedRx.end() && c.opcode == URMA_CR_OPC_SEND && c.completion_len >= 24)
      completed.push_back({rx->second.start + 24, c.completion_len - 24, false});
    auto read = postedRead.find(key);
    if (read != postedRead.end()) {
      completed.push_back(read->second);
      postedRead.erase(read);
    }
  }
  return n;
}

TEST(UrmaReceiveStorage, ActualDmaStorageSurvivesHeaderAndRocketParsing) {
  if (!std::getenv("FBTHRIFT_URMA_TEST_DEVICE")) GTEST_SKIP();
  for (bool rocket : {false, true})
    for (bool read : {false, true}) exercise(rocket, read);
  // Exercise the large allocation class with real DMA, retaining all eight
  // destinations simultaneously. This is bounded integrity, not a rate test.
  for (bool rocket : {false, true}) exercise(rocket, true, 8 * 1024 * 1024);
}
#endif
int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  folly::Init init(&argc, &argv);
  return RUN_ALL_TESTS();
}
