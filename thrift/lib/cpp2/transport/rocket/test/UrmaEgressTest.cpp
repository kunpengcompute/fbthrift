/* Copyright (c) Huawei Technologies Co., Ltd. 2026. Licensed under Apache-2.0.
 */
#include <folly/ScopeGuard.h>
#include <folly/init/Init.h>
#include <folly/io/async/AsyncServerSocket.h>
#include <folly/portability/GTest.h>
#include <thrift/lib/cpp2/transport/rocket/server/RocketServerConnection.h>
#ifdef FOLLY_HAVE_URMA
#include <folly/io/async/urma/UrmaAsyncSocket.h>
#include <folly/io/async/urma/UrmaInit.h>
#endif

namespace apache::thrift::rocket {
namespace {
class Handler : public RocketServerHandler {
 public:
  void handleRequestResponseFrame(RequestResponseFrame&&,
                                  RocketServerFrameContext&&) override {}
  void handleRequestFnfFrame(RequestFnfFrame&&,
                             RocketServerFrameContext&&) override {}
  void handleRequestStreamFrame(RequestStreamFrame&&,
                                RocketServerFrameContext&&,
                                RocketStreamClientCallback*) override {}
  void handleRequestChannelFrame(RequestChannelFrame&&,
                                 RocketServerFrameContext&&,
                                 RocketSinkClientCallback*) override {}
  void connectionClosing() override {}
};

struct Completion : MessageChannel::SendCallback {
  unsigned sent{0}, failed{0};
  std::string error;
  void sendQueued() override {}
  void messageSent() override { ++sent; }
  void messageSendError(folly::exception_wrapper&& ew) override {
    ++failed;
    error = ew.what().toStdString();
  }
};

// A silent TCP peer keeps the URMA handshake pending while Rocket flushes
// its batch into the real transport. No remote peer or traffic is required.
TEST(UrmaEgress, FrameworkTimeoutAndBufferAccounting) {
#ifdef FOLLY_HAVE_URMA
  auto* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) GTEST_SKIP() << "Set FBTHRIFT_URMA_TEST_DEVICE";
  folly::UrmaConfig config;
  config.dev_name = device;
  folly::UrmaInit::init(config);
  auto finish = folly::makeGuard([] { folly::UrmaInit::fini(); });
  folly::EventBase evb;
  auto listener = folly::AsyncServerSocket::newSocket(&evb);
  listener->bind(folly::SocketAddress("127.0.0.1", 0));
  listener->listen(1);
  folly::SocketAddress address;
  listener->getAddress(&address);
  auto transport = folly::AsyncTransport::UniquePtr(
      new folly::UrmaAsyncSocket(&evb, address));
  auto* urma = static_cast<folly::UrmaAsyncSocket*>(transport.get());
  MemoryTracker ingress, egress;
  RocketServerConnection::Config cfg;
  cfg.socketWriteTimeout = std::chrono::milliseconds(173);
  cfg.writeBatchingSize = 1;
  cfg.egressBufferBackpressureThreshold = 4096;
  cfg.egressBufferBackpressureRecoveryFactor = 0.5;
  auto* connection = new RocketServerConnection(
      std::move(transport), std::make_unique<Handler>(), ingress, egress, cfg);
  folly::DelayedDestruction::DestructorGuard hold(connection);
  EXPECT_EQ(urma->getSendTimeout(), 173);
  EXPECT_FALSE(connection->areStreamsPaused());
  Completion first, second;
  auto data = folly::IOBuf::create(8192);
  data->append(2048);
  const auto firstCapacity = data->capacity();
  connection->send(std::move(data), MessageChannel::SendCallbackPtr(&first));
  evb.loopOnce(EVLOOP_NONBLOCK);
  EXPECT_EQ(urma->getAppBytesBuffered(), 2048);
  EXPECT_EQ(egress.getUsage(), firstCapacity);
  EXPECT_TRUE(connection->areStreamsPaused());
  data = folly::IOBuf::create(4096);
  data->append(1024);
  const auto totalCapacity = firstCapacity + data->capacity();
  connection->send(std::move(data), MessageChannel::SendCallbackPtr(&second));
  evb.loopOnce(EVLOOP_NONBLOCK);
  EXPECT_EQ(urma->getAppBytesBuffered(), 3072);
  EXPECT_EQ(urma->getAllocatedBytesBuffered(), totalCapacity);
  EXPECT_EQ(egress.getUsage(), totalCapacity);
  urma->closeNow();
  evb.loopOnce(EVLOOP_NONBLOCK);  // Drain Rocket's final error frame.
  EXPECT_EQ(egress.getUsage(), 0);
  EXPECT_FALSE(connection->areStreamsPaused());
  EXPECT_EQ(first.failed, 1);
  EXPECT_EQ(second.failed, 1);
  EXPECT_EQ(first.sent + second.sent, 0);
  EXPECT_TRUE(connection->getDestroyPending());
#else
  GTEST_SKIP() << "URMA support is disabled";
#endif
}

TEST(UrmaEgress, TcpStillReceivesFrameworkTimeout) {
  folly::EventBase evb;
  auto socket = folly::AsyncSocket::newSocket(&evb);
  auto* tcp = socket.get();
  MemoryTracker ingress, egress;
  RocketServerConnection::Config cfg;
  cfg.socketWriteTimeout = std::chrono::milliseconds(271);
  RocketServerConnection::UniquePtr connection(new RocketServerConnection(
      std::move(socket), std::make_unique<Handler>(), ingress, egress, cfg));
  EXPECT_EQ(tcp->getSendTimeout(), 271);
}

#ifdef FOLLY_HAVE_URMA
TEST(UrmaEgress, MemoryLimitClosesTheBusinessTransport) {
  auto* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) GTEST_SKIP() << "Set FBTHRIFT_URMA_TEST_DEVICE";
  folly::UrmaConfig config;
  config.dev_name = device;
  folly::UrmaInit::init(config);
  auto finish = folly::makeGuard([] { folly::UrmaInit::fini(); });
  folly::EventBase evb;
  auto listener = folly::AsyncServerSocket::newSocket(&evb);
  listener->bind(folly::SocketAddress("127.0.0.1", 0));
  listener->listen(1);
  folly::SocketAddress address;
  listener->getAddress(&address);
  auto transport = folly::AsyncTransport::UniquePtr(
      new folly::UrmaAsyncSocket(&evb, address));
  auto* urma = transport.get();
  MemoryTracker ingress;
  MemoryTracker egress(folly::observer::makeStaticObserver<size_t>(1024));
  RocketServerConnection::Config cfg;
  cfg.writeBatchingSize = 1;
  auto* connection = new RocketServerConnection(
      std::move(transport), std::make_unique<Handler>(), ingress, egress, cfg);
  folly::DelayedDestruction::DestructorGuard hold(connection);
  Completion callback;
  connection->send(folly::IOBuf::copyBuffer(std::string(2048, 'x')),
                   MessageChannel::SendCallbackPtr(&callback));
  evb.loopOnce(EVLOOP_NONBLOCK);
  EXPECT_FALSE(urma->good());
  EXPECT_EQ(callback.failed, 1);
  EXPECT_EQ(callback.sent, 0);
  EXPECT_EQ(egress.getUsage(), 0);
  EXPECT_TRUE(connection->getDestroyPending());
}

void testTimeout(bool restart) {
  auto* device = std::getenv("FBTHRIFT_URMA_TEST_DEVICE");
  if (!device || !*device) GTEST_SKIP() << "Set FBTHRIFT_URMA_TEST_DEVICE";
  folly::UrmaConfig config;
  config.dev_name = device;
  config.handshake_timeout_ms = 5000;
  folly::UrmaInit::init(config);
  {
    folly::EventBase evb;
    // The TCP peer accepts at kernel level but does not answer URMA HELLO.
    auto listener = folly::AsyncServerSocket::newSocket(&evb);
    listener->bind(folly::SocketAddress("127.0.0.1", 0));
    listener->listen(1);
    folly::SocketAddress address;
    listener->getAddress(&address);
    auto transport = folly::AsyncTransport::UniquePtr(
        new folly::UrmaAsyncSocket(&evb, address));
    auto* urma = transport.get();
    MemoryTracker ingress, egress;
    RocketServerConnection::Config cfg;
    cfg.socketWriteTimeout = std::chrono::milliseconds(173);
    cfg.writeBatchingSize = 1;
    auto* connection = new RocketServerConnection(std::move(transport),
                                                  std::make_unique<Handler>(),
                                                  ingress, egress, cfg);
    folly::DelayedDestruction::DestructorGuard hold(connection);
    Completion callback;
    auto start = std::chrono::steady_clock::now();
    connection->send(folly::IOBuf::copyBuffer(std::string(2048, 'x')),
                     MessageChannel::SendCallbackPtr(&callback));
    if (restart) evb.runAfterDelay([&] { urma->setSendTimeout(173); }, 100);
    while (!callback.failed &&
           std::chrono::steady_clock::now() - start < std::chrono::seconds(2))
      evb.loopOnce();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - start)
                       .count();
    EXPECT_EQ(callback.failed, 1);
    EXPECT_NE(callback.error.find("URMA send timeout"), std::string::npos);
    EXPECT_GE(elapsed, restart ? 270 : 170);
    EXPECT_LT(elapsed, 1000);
    EXPECT_EQ(egress.getUsage(), 0);
    EXPECT_TRUE(connection->getDestroyPending());
    std::cout << "Configured timeout=173ms, restart=" << restart
              << ", observed=" << elapsed << "ms\n";
  }
  folly::UrmaInit::fini();
}
TEST(UrmaEgressHardware, FrameworkTimeoutExpiresBeforeHandshakeTimeout) {
  testTimeout(false);
}
TEST(UrmaEgressHardware, ChangingTimeoutRestartsPendingWriteDeadline) {
  testTimeout(true);
}
#endif
}  // namespace
}  // namespace apache::thrift::rocket

int main(int argc, char** argv) {
  testing::InitGoogleTest(&argc, argv);
  folly::Init init(&argc, &argv);
  return RUN_ALL_TESTS();
}
