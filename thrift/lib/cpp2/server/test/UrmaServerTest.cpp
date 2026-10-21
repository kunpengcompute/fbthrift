/* Copyright (c) Huawei Technologies Co., Ltd. 2026. Licensed under Apache-2.0. */
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <string>
#include <folly/init/Init.h>
#include <folly/portability/GTest.h>
#include <thrift/lib/cpp2/server/ThriftServer.h>
#include <thrift/lib/cpp2/util/ScopedServerThread.h>
#ifdef FOLLY_HAVE_URMA
#include <folly/io/async/urma/UrmaInit.h>
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
TEST(UrmaServer, RejectsIncompatibleSocketModeBeforeInitializingHardware) {
  auto server = makeServer();
  server->setUseUrma(true);
  server->setPreferIoUring(true);
  EXPECT_THROW(server->setup(), std::invalid_argument);
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}

TEST(UrmaServer, DoesNotBypassAnExplicitTlsRequirement) {
  auto server = makeServer();
  server->setUseUrma(true);
  server->setSSLPolicy(SSLPolicy::REQUIRED);
  EXPECT_THROW(server->setup(), std::invalid_argument);
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}

TEST(UrmaServer, RequiresExplicitDevice) {
  auto server = makeServer();
  server->setUseUrma(true);
  EXPECT_THROW(server->setup(), std::invalid_argument);
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
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
  auto server = makeServer();
  server->setUseUrma(true);
  server->setUrmaConfig(folly::UrmaInit::getConfig());
  { util::ScopedServerThread running(server); }
  EXPECT_TRUE(folly::UrmaInit::isInitialized());
  folly::UrmaInit::fini();
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}

TEST(UrmaServerHardware, RejectsConflictingConfigWithoutStoppingExistingServer) {
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
    auto second = makeServer();
    second->setUseUrma(true);
    ++config.rx_depth;
    second->setUrmaConfig(config);
    EXPECT_THROW(second->setup(), std::invalid_argument);
    EXPECT_TRUE(folly::UrmaInit::isInitialized());
  }
  EXPECT_FALSE(folly::UrmaInit::isInitialized());
}

TEST(UrmaServerHardware, BindFailureReleasesTheNewContext) {
  const char* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) {
    GTEST_SKIP() << "Set FBTHRIFT_URMA_TEST_DEVICE to run on URMA hardware";
  }
  auto tcp = makeServer();
  util::ScopedServerThread occupied(tcp);
  auto urma = makeServer();
  urma->setAddress(*occupied.getAddress());
  urma->setUseUrma(true);
  folly::UrmaConfig config;
  config.dev_name = device;
  urma->setUrmaConfig(config);
  EXPECT_THROW(urma->setup(), std::exception);
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
