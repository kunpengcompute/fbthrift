/* Copyright (c) Huawei Technologies Co., Ltd. 2026. Licensed under Apache-2.0.
 */
#include <cerrno>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <folly/Conv.h>
#include <folly/File.h>
#include <folly/ScopeGuard.h>
#include <folly/init/Init.h>
#include <folly/portability/GTest.h>
#include <folly/portability/Sockets.h>
#include <thrift/lib/cpp2/server/ThriftServer.h>
#include <thrift/lib/cpp2/util/ScopedServerThread.h>
#ifdef FOLLY_HAVE_URMA
#include <folly/io/async/urma/UrmaInit.h>
#include <thrift/lib/cpp2/server/detail/UrmaServerRuntime.h>
#endif

namespace apache::thrift {
namespace {
class NoopProcessor : public AsyncProcessor {
 public:
  void processSerializedCompressedRequestWithMetadata(
      ResponseChannelRequest::UniquePtr,
      SerializedCompressedRequest&&,
      const AsyncProcessorFactory::MethodMetadata&,
      protocol::PROTOCOL_TYPES,
      Cpp2RequestContext*,
      folly::EventBase*,
      concurrency::ThreadManager*) override {}
};

class NoopFactory : public AsyncProcessorFactory {
 public:
  std::unique_ptr<AsyncProcessor> getProcessor() override {
    return std::make_unique<NoopProcessor>();
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
  check(&folly::UrmaConfig::tx_depth, "tx_depth", existing.tx_depth + 1);
  check(
      &folly::UrmaConfig::buffer_size, "buffer_size", existing.buffer_size + 1);
  check(&folly::UrmaConfig::enable_read, "enable_read", !existing.enable_read);
  check(
      &folly::UrmaConfig::read_threshold,
      "read_threshold",
      existing.read_threshold + 1);
  check(
      &folly::UrmaConfig::handshake_timeout_ms,
      "handshake_timeout_ms",
      existing.handshake_timeout_ms + 1);
  check(
      &folly::UrmaConfig::max_pending_bytes,
      "max_pending_bytes",
      existing.max_pending_bytes + 1);
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
  one.reset();
  EXPECT_TRUE(folly::UrmaInit::isInitialized());
  two.reset();
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}

TEST(UrmaServerHardware, BorrowsAnApplicationContextWithoutFinalizingIt) {
  const char* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) {
    GTEST_SKIP() << "Set FBTHRIFT_URMA_TEST_DEVICE to run on URMA hardware";
  }
  folly::UrmaConfig config;
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

TEST(UrmaServerHardware, ShutdownSetClosesAnAcceptedUrmaHandshakeSocket) {
  const char* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) {
    GTEST_SKIP() << "Set FBTHRIFT_URMA_TEST_DEVICE to run on URMA hardware";
  }
  auto sockets = std::make_shared<folly::ShutdownSocketSet>();
  auto server = makeServer();
  server->replaceShutdownSocketSet(sockets);
  folly::UrmaConfig config;
  config.dev_name = device;
  server->setUrmaConfig(config);
  server->setUseUrma(true);
  {
    util::ScopedServerThread running(server);
    folly::File client(::socket(AF_INET, SOCK_STREAM, 0), true);
    ASSERT_GE(client.fd(), 0);
    timeval timeout{2, 0};
    ASSERT_EQ(
        ::setsockopt(
            client.fd(), SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)),
        0);
    sockaddr_storage address{};
    auto length = running.getAddress()->getAddress(&address);
    ASSERT_EQ(
        ::connect(client.fd(), reinterpret_cast<sockaddr*>(&address), length),
        0);
    char hello[4096];
    // Receiving HELLO proves the accepted socket has entered the URMA path.
    ASSERT_GT(::recv(client.fd(), hello, sizeof(hello), 0), 0);
    sockets->shutdownAll();
    ssize_t received;
    do {
      // TCP may split HELLO across reads; drain any remainder before EOF.
      received = ::recv(client.fd(), hello, sizeof(hello), 0);
    } while (received > 0);
    EXPECT_TRUE(received == 0 || (received < 0 && errno == ECONNRESET))
        << "Accepted URMA socket was not shut down, errno=" << errno;
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
