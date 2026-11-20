/* Copyright (c) Huawei Technologies Co., Ltd. 2026. Licensed under Apache-2.0.
 */
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <thread>
#include <folly/Conv.h>
#include <folly/ScopeGuard.h>
#include <folly/init/Init.h>
#include <folly/portability/GTest.h>
#include <folly/portability/Sockets.h>
#include <thrift/lib/cpp2/server/ThriftServer.h>
#include <thrift/lib/cpp/transport/THeader.h>
#include <thrift/lib/cpp2/protocol/BinaryProtocol.h>
#include <thrift/lib/cpp2/util/ScopedServerThread.h>
#ifdef FOLLY_HAVE_URMA
#include <folly/io/async/urma/UrmaInit.h>
#include <folly/io/async/urma/UrmaAsyncSocket.h>
#include <folly/io/async/urma/UrmaProtocol.h>
#include <thrift/lib/cpp2/server/detail/UrmaServerRuntime.h>
#endif

namespace apache::thrift {
namespace {
class NoopProcessor : public AsyncProcessor {
 public:
  explicit NoopProcessor(std::shared_ptr<std::atomic<size_t>> calls) : calls_(std::move(calls)) {}
  void processSerializedCompressedRequestWithMetadata(
      ResponseChannelRequest::UniquePtr,
      SerializedCompressedRequest&&,
      const AsyncProcessorFactory::MethodMetadata&,
      protocol::PROTOCOL_TYPES,
      Cpp2RequestContext*,
      folly::EventBase*,
      concurrency::ThreadManager*) override { ++*calls_; }
 private:
  std::shared_ptr<std::atomic<size_t>> calls_;
};

class NoopFactory : public AsyncProcessorFactory {
 public:
  std::shared_ptr<std::atomic<size_t>> calls = std::make_shared<std::atomic<size_t>>(0);
  std::unique_ptr<AsyncProcessor> getProcessor() override {
    return std::make_unique<NoopProcessor>(calls);
  }
  std::vector<ServiceHandlerBase*> getServiceHandlers() override { return {}; }
};

std::shared_ptr<ThriftServer> makeServer() {
  auto server = std::make_shared<ThriftServer>();
  server->setProcessorFactory(std::make_shared<NoopFactory>());
  server->setAddress("127.0.0.1", 0);
  server->setNumIOWorkerThreads(1);
  server->setNumCPUWorkerThreads(1);
  return server;
}

TEST(UrmaServer, TcpRemainsTheDefault) {
  auto server = makeServer();
  EXPECT_FALSE(server->getUseUrma());
  util::ScopedServerThread running(server);
  EXPECT_GT(running.getAddress()->getPort(), 0);
#ifdef FOLLY_HAVE_URMA
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
  EXPECT_FALSE(folly::UrmaInit::poolIOBufAllocationEnabled());
  auto buffer = folly::IOBuf::copyBuffer("tcp");
  EXPECT_FALSE(folly::UrmaInit::ownsPoolBuffer(*buffer));
  buffer->reserve(32, 8192);
  EXPECT_FALSE(folly::UrmaInit::ownsPoolBuffer(*buffer));
  EXPECT_EQ(buffer->length(), 3);
  EXPECT_EQ(std::string(reinterpret_cast<const char*>(buffer->data()), 3), "tcp");
#endif
}

#ifdef FOLLY_HAVE_URMA
void expectInvalidConfiguration(ThriftServer& server, const char* message) {
  try {
    server.setup();
    FAIL() << "Expected the specified configuration conflict";
  } catch (const std::invalid_argument& error) {
    EXPECT_NE(std::string(error.what()).find(message), std::string::npos);
  }
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}

void configurePlaceholderDevice(ThriftServer& server) {
  folly::UrmaConfig config;
  config.dev_name = "fbthrift-configuration-test-device";
  config.registered_pool.enabled = true;
  server.setUrmaConfig(config);
}

void expectConfigurationConflicts(
    const folly::UrmaConfig& existing, const char* context) {
  auto check = [&](auto member, const char* field, auto value) {
    SCOPED_TRACE(field);
    auto requested = existing;
    requested.*member = value;
    try {
      auto runtime = std::make_shared<detail::UrmaServerRuntime>(requested);
      FAIL() << "Expected the conflicting configuration to be rejected";
    } catch (const std::invalid_argument& error) {
      EXPECT_EQ(
          error.what(),
          folly::to<std::string>(
              context,
              ": ",
              field,
              " (existing=",
              existing.*member,
              ", requested=",
              value,
              ")"));
    }
    EXPECT_TRUE(folly::UrmaInit::isInitialized());
  };
  check(&folly::UrmaConfig::dev_name, "dev_name", existing.dev_name + "-other");
  check(&folly::UrmaConfig::rx_depth, "rx_depth", existing.rx_depth + 1);
  check(&folly::UrmaConfig::rx_target, "rx_target", existing.rx_target + 1);
  check(&folly::UrmaConfig::max_send_sge, "max_send_sge", existing.max_send_sge + 1);
  check(&folly::UrmaConfig::tx_depth, "tx_depth", existing.tx_depth + 1);
  check(
      &folly::UrmaConfig::buffer_size, "buffer_size", existing.buffer_size + 1);
  check(&folly::UrmaConfig::enable_read, "enable_read", !existing.enable_read);
  check(
      &folly::UrmaConfig::read_threshold,
      "read_threshold",
      existing.read_threshold + 1);
  check(&folly::UrmaConfig::read_window, "read_window", existing.read_window + 1);
  check(
      &folly::UrmaConfig::offer_window, "offer_window", existing.offer_window + 1);
  check(
      &folly::UrmaConfig::handshake_timeout_ms,
      "handshake_timeout_ms",
      existing.handshake_timeout_ms + 1);
  check(
      &folly::UrmaConfig::max_pending_bytes,
      "max_pending_bytes",
      existing.max_pending_bytes + 1);
  auto differentPool = existing;
  differentPool.registered_pool.pool_max_size += 4096;
  try {
    auto runtime = std::make_shared<detail::UrmaServerRuntime>(differentPool);
    FAIL() << "Expected different registered-memory budgets to be rejected";
  } catch (const std::invalid_argument& error) {
    EXPECT_EQ(std::string(error.what()), folly::to<std::string>(
        context, ": registered_pool configuration differs"));
  }
  // Rejected attempts must leave the shared user count and borrowed context
  // usable, including after the acquireContext()/rollback path.
  auto compatible = std::make_shared<detail::UrmaServerRuntime>(existing);
}

TEST(UrmaServer, RejectsIncompatibleSocketModeBeforeInitializingHardware) {
  auto server = makeServer();
  server->setUseUrma(true);
  configurePlaceholderDevice(*server);
  server->setPreferIoUring(true);
  expectInvalidConfiguration(*server, "does not support io_uring sockets");
}

TEST(UrmaServer, DoesNotBypassAnExplicitTlsRequirement) {
  auto server = makeServer();
  server->setUseUrma(true);
  configurePlaceholderDevice(*server);
  server->setSSLPolicy(SSLPolicy::REQUIRED);
  expectInvalidConfiguration(*server, "does not support TLS");
}

TEST(UrmaServer, RequiresExplicitDevice) {
  auto server = makeServer();
  server->setUseUrma(true);
  expectInvalidConfiguration(*server, "requires an explicit device name");
}

TEST(UrmaServer, InitializationFailureDoesNotLeaveAContext) {
  auto server = makeServer();
  server->setUseUrma(true);
  folly::UrmaConfig config;
  config.registered_pool.enabled = true;
  config.dev_name = "fbthrift-no-such-urma-device";
  server->setUrmaConfig(config);
  EXPECT_THROW(server->setup(), std::runtime_error);
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}

TEST(UrmaServerHardware, SharedContextOutlivesTheFirstServer) {
  const char* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) {
    GTEST_SKIP() << "Set FBTHRIFT_URMA_TEST_DEVICE to run on URMA hardware";
  }
  folly::UrmaConfig config;
  config.registered_pool.enabled = true;
  config.dev_name = device;
  auto first = makeServer();
  auto second = makeServer();
  for (const auto& server : {first, second}) {
    server->setUseUrma(true);
    server->setUrmaConfig(config);
  }
  auto one = std::make_unique<util::ScopedServerThread>(first);
  auto two = std::make_unique<util::ScopedServerThread>(second);
  EXPECT_TRUE(folly::UrmaInit::isInitialized());
  EXPECT_TRUE(folly::UrmaInit::poolIOBufAllocationEnabled());
  {
    auto buffer = folly::IOBuf::copyBuffer("urma");
    EXPECT_TRUE(folly::UrmaInit::ownsPoolBuffer(*buffer));
    buffer->reserve(32, 8192);
    EXPECT_TRUE(folly::UrmaInit::ownsPoolBuffer(*buffer));
  }
  one.reset();
  EXPECT_TRUE(folly::UrmaInit::isInitialized());
  EXPECT_TRUE(folly::UrmaInit::poolIOBufAllocationEnabled());
  two.reset();
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
  EXPECT_FALSE(folly::UrmaInit::poolIOBufAllocationEnabled());
  auto native = folly::IOBuf::copyBuffer("tcp");
  EXPECT_FALSE(folly::UrmaInit::ownsPoolBuffer(*native));
}

TEST(UrmaServerHardware, BorrowsAnApplicationContextWithoutFinalizingIt) {
  const char* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) {
    GTEST_SKIP() << "Set FBTHRIFT_URMA_TEST_DEVICE to run on URMA hardware";
  }
  folly::UrmaConfig config;
  config.registered_pool.enabled = true;
  config.dev_name = device;
  folly::UrmaInit::init(config);
  auto cleanup = folly::makeGuard([] { folly::UrmaInit::fini(); });
  auto server = makeServer();
  server->setUseUrma(true);
  server->setUrmaConfig(folly::UrmaInit::getConfig());
  { util::ScopedServerThread running(server); }
  EXPECT_TRUE(folly::UrmaInit::isInitialized());
  folly::UrmaInit::fini();
  cleanup.dismiss();
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}

TEST(
    UrmaServerHardware, RejectsConflictingConfigWithoutStoppingExistingServer) {
  const char* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) {
    GTEST_SKIP() << "Set FBTHRIFT_URMA_TEST_DEVICE to run on URMA hardware";
  }
  folly::UrmaConfig config;
  config.registered_pool.enabled = true;
  config.dev_name = device;
  auto first = makeServer();
  first->setUseUrma(true);
  first->setUrmaConfig(config);
  {
    util::ScopedServerThread running(first);
    expectConfigurationConflicts(
        config, "URMA servers in one process must use the same configuration");
    auto second = makeServer();
    second->setUseUrma(true);
    ++config.rx_depth;
    second->setUrmaConfig(config);
    try {
      second->setup();
      FAIL() << "Expected the second server's configuration to be rejected";
    } catch (const std::invalid_argument& error) {
      EXPECT_NE(
          std::string(error.what())
              .find(folly::to<std::string>(
                  "rx_depth (existing=",
                  config.rx_depth - 1,
                  ", requested=",
                  config.rx_depth,
                  ")")),
          std::string::npos);
    }
    EXPECT_TRUE(folly::UrmaInit::isInitialized());
  }
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}

TEST(UrmaServerHardware, ReportsConflictsWithoutFinalizingBorrowedContext) {
  const char* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) {
    GTEST_SKIP() << "Set FBTHRIFT_URMA_TEST_DEVICE to run on URMA hardware";
  }
  folly::UrmaConfig config;
  config.registered_pool.enabled = true;
  config.dev_name = device;
  folly::UrmaInit::init(config);
  auto cleanup = folly::makeGuard([] { folly::UrmaInit::fini(); });
  expectConfigurationConflicts(
      folly::UrmaInit::getConfig(),
      "ThriftServer URMA config differs from the existing Folly context");
  EXPECT_TRUE(folly::UrmaInit::isInitialized());
  folly::UrmaInit::fini();
  cleanup.dismiss();
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}

TEST(UrmaServerHardware, BindFailureReleasesTheNewContext) {
  const char* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) {
    GTEST_SKIP() << "Set FBTHRIFT_URMA_TEST_DEVICE to run on URMA hardware";
  }
  auto tcp = makeServer();
  util::ScopedServerThread occupied(tcp);
  ASSERT_GT(occupied.getAddress()->getPort(), 0);
  auto urma = makeServer();
  urma->setAddress(*occupied.getAddress());
  urma->setUseUrma(true);
  folly::UrmaConfig config;
  config.registered_pool.enabled = true;
  config.dev_name = device;
  urma->setUrmaConfig(config);
  try {
    urma->setup();
    FAIL() << "Expected the occupied listening address to reject bind";
  } catch (const std::system_error& error) {
    EXPECT_EQ(error.code().value(), EADDRINUSE);
    EXPECT_NE(
        std::string(error.what()).find("failed to bind"), std::string::npos);
  }
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}

TEST(UrmaServerHardware, StrictSslWithoutCredentialsDoesNotEnableTls) {
  const char* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) {
    GTEST_SKIP() << "Set FBTHRIFT_URMA_TEST_DEVICE to run on URMA hardware";
  }
  auto server = makeServer();
  folly::UrmaConfig config;
  config.registered_pool.enabled = true;
  config.dev_name = device;
  server->setUrmaConfig(config);
  server->setUseUrma(true);
  server->setSSLPolicy(SSLPolicy::DISABLED);
  server->setStrictSSL(true);
  {
    util::ScopedServerThread running(server);
    EXPECT_GT(running.getAddress()->getPort(), 0);
    EXPECT_TRUE(folly::UrmaInit::isInitialized());
  }
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}

template <typename Predicate>
bool waitFor(folly::EventBase& evb, Predicate ready) {
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
  while (!ready() && std::chrono::steady_clock::now() < deadline) {
    evb.loopOnce(EVLOOP_NONBLOCK);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  return ready();
}

std::unique_ptr<folly::IOBuf> makeProbeFrame() {
  folly::IOBufQueue output(folly::IOBufQueue::cacheChainLength());
  BinaryProtocolWriter writer;
  writer.setOutput(&output);
  writer.writeMessageBegin("probe", MessageType::T_ONEWAY, 1);
  writer.writeStructBegin("args");
  writer.writeFieldStop();
  writer.writeStructEnd();
  writer.writeMessageEnd();
  transport::THeader header;
  header.setProtocolId(protocol::T_BINARY_PROTOCOL);
  transport::THeader::StringToStringMap headers;
  auto frame = header.addHeader(output.move(), headers);
  frame->coalesce();
  return frame;
}

TEST(UrmaServerHardware, MixedTcpAndUrmaClients) {
  const char* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) GTEST_SKIP();
  auto server = makeServer();
  auto factory = std::make_shared<NoopFactory>();
  server->setProcessorFactory(factory);
  folly::UrmaConfig config;
  config.dev_name = device;
  server->setUseUrma(true);
  server->setUrmaConfig(config);
  {
    util::ScopedServerThread running(server);
    folly::EventBase evb;
    auto tcp = folly::AsyncSocket::newSocket(&evb, *running.getAddress());
    auto urma = folly::UrmaAsyncSocket::newSocket(&evb, *running.getAddress());
    ASSERT_TRUE(waitFor(evb, [&] {
      return static_cast<folly::UrmaAsyncSocket*>(urma.get())->isUrmaReady();
    }));
    auto frame = makeProbeFrame();
    // An incomplete prefix must not be consumed or misclassified.
    tcp->write(nullptr, frame->data(), 2);
    for (int i = 0; i < 5; ++i) evb.loopOnce(EVLOOP_NONBLOCK);
    EXPECT_EQ(factory->calls->load(), 0);
    urma->writeChain(nullptr, frame->clone());
    ASSERT_TRUE(waitFor(evb, [&] { return factory->calls->load() == 1; }));
    tcp->write(nullptr, frame->data() + 2, frame->length() - 2);
    ASSERT_TRUE(waitFor(evb, [&] { return factory->calls->load() == 2; }));
    // A mixed listener can read past the first TCP frame while identifying
    // the protocol. Both frames must reach the Header transport.
    auto pipelined = folly::AsyncSocket::newSocket(&evb, *running.getAddress());
    frame->appendToChain(frame->clone());
    pipelined->writeChain(nullptr, std::move(frame));
    ASSERT_TRUE(waitFor(evb, [&] { return factory->calls->load() == 4; }));
    tcp->closeNow();
    urma->closeNow();
  }
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}

struct CloseObserver : folly::AsyncTransport::ReadCallback {
  char bytes[128]{};
  bool closed{false};
  void getReadBuffer(void** p, size_t* n) override { *p = bytes; *n = sizeof(bytes); }
  void readDataAvailable(size_t) noexcept override {}
  void readEOF() noexcept override { closed = true; }
  void readErr(const folly::AsyncSocketException&) noexcept override { closed = true; }
};

TEST(UrmaServerHardware, HandshakeDeadlineOnlyAppliesToUrmaPrefixes) {
  const char* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) GTEST_SKIP();
  auto server = makeServer();
  auto factory = std::make_shared<NoopFactory>();
  server->setProcessorFactory(factory);
  folly::UrmaConfig config;
  config.dev_name = device;
  config.handshake_timeout_ms = 500;
  server->setUseUrma(true);
  server->setUrmaConfig(config);
  {
    util::ScopedServerThread running(server);
    folly::EventBase evb;
    auto idle = folly::AsyncSocket::newSocket(&evb, *running.getAddress());
    auto partial = folly::AsyncSocket::newSocket(&evb, *running.getAddress());
    auto frame = makeProbeFrame();
    partial->write(nullptr, frame->data(), 2);
    CloseObserver reader;
    auto truncated = folly::AsyncSocket::newSocket(&evb, *running.getAddress());
    ASSERT_TRUE(waitFor(evb, [&] { return !truncated->connecting(); }));
    ASSERT_TRUE(truncated->good());
    truncated->setReadCB(&reader);
    const auto sent = std::chrono::steady_clock::now();
    truncated->write(nullptr, "UR", 2);
    ASSERT_TRUE(waitFor(evb, [&] { return reader.closed; }));
    EXPECT_GE(std::chrono::steady_clock::now() - sent,
              std::chrono::milliseconds(config.handshake_timeout_ms) / 2);
    // The truncated URMA prefix timed out. Both pre-existing TCP clients
    // must still be able to deliver their original frames.
    idle->writeChain(nullptr, frame->clone());
    partial->write(nullptr, frame->data() + 2, frame->length() - 2);
    ASSERT_TRUE(waitFor(evb, [&] { return factory->calls->load() == 2; }));
  }
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}

TEST(UrmaServerHardware, RejectsUnsupportedVersionsBeforeHandshakeTimeout) {
  const char* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) GTEST_SKIP();
  auto server = makeServer();
  folly::UrmaConfig config;
  config.dev_name = device;
  config.handshake_timeout_ms = 30000; // Each waitFor ends after four seconds.
  server->setUseUrma(true);
  server->setUrmaConfig(config);
  util::ScopedServerThread running(server);
  folly::EventBase evb;

  std::array<uint8_t, folly::urma_detail::kHelloSize> hello;
  folly::urma_detail::encodeHello(hello.data(), {});
  constexpr size_t prefixSize = 13; // Enough for the mixed-port detector.
  CloseObserver control;
  auto valid = folly::AsyncSocket::newSocket(&evb, *running.getAddress());
  valid->setReadCB(&control);
  valid->write(nullptr, hello.data(), prefixSize);
  for (size_t versionOffset : {6, 8}) {
    SCOPED_TRACE(versionOffset);
    auto invalid = hello;
    folly::urma_detail::store16(invalid.data() + versionOffset, 2);
    CloseObserver rejected;
    auto peer = folly::AsyncSocket::newSocket(&evb, *running.getAddress());
    ASSERT_TRUE(waitFor(evb, [&] { return !peer->connecting(); }));
    ASSERT_TRUE(peer->good());
    peer->setReadCB(&rejected);
    peer->write(nullptr, invalid.data(), prefixSize);
    ASSERT_TRUE(waitFor(evb, [&] { return rejected.closed; }));
    EXPECT_TRUE(valid->good());
    EXPECT_FALSE(control.closed);
  }
}

TEST(UrmaServerHardware, ShutdownSetClosesAnAcceptedUrmaHandshakeSocket) {
  const char* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) {
    GTEST_SKIP() << "Set FBTHRIFT_URMA_TEST_DEVICE to run on URMA hardware";
  }
  auto sockets = std::make_shared<folly::ShutdownSocketSet>();
  auto server = makeServer();
  server->replaceShutdownSocketSet(sockets);
  folly::UrmaConfig config;
  config.registered_pool.enabled = true;
  config.dev_name = device;
  server->setUrmaConfig(config);
  server->setUseUrma(true);
  {
    util::ScopedServerThread running(server);
    folly::EventBase evb;
    struct Reader : folly::AsyncTransport::ReadCallback {
      char byte{};
      bool closed{false};
      void getReadBuffer(void** data, size_t* size) override {
        *data = &byte;
        *size = 1;
      }
      void readDataAvailable(size_t) noexcept override {}
      void readEOF() noexcept override { closed = true; }
      void readErr(const folly::AsyncSocketException&) noexcept override {
        closed = true;
      }
    } reader;
    auto client =
        folly::UrmaAsyncSocket::newSocket(&evb, *running.getAddress());
    client->setReadCB(&reader);
    auto wait = [&](auto ready) {
      for (int i = 0; i < 1000; ++i) {
        evb.loopOnce(EVLOOP_NONBLOCK);
        if (ready())
          return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
      }
      return false;
    };
    // Complete the handshake with a real peer before testing shutdown.
    ASSERT_TRUE(wait([&] {
      return static_cast<folly::UrmaAsyncSocket*>(client.get())->isUrmaReady();
    }));
    sockets->shutdownAll();
    EXPECT_TRUE(wait([&] { return reader.closed; }))
        << "Accepted URMA socket was not shut down";
    client->setReadCB(nullptr);
    client->closeNow();
  }
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}
#else
TEST(UrmaServer, ExplicitUrmaFailsWithoutSdkSupport) {
  auto server = makeServer();
  server->setUseUrma(true);
  EXPECT_THROW(server->setup(), std::runtime_error);
}
#endif
} // namespace
} // namespace apache::thrift

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  folly::Init init(&argc, &argv);
  return RUN_ALL_TESTS();
}
