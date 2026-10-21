/* Copyright (c) Huawei Technologies Co., Ltd. 2026. Licensed under Apache-2.0. */
#include <thrift/lib/cpp2/server/detail/UrmaServerRuntime.h>

#ifdef FOLLY_HAVE_URMA
#include <mutex>
#include <stdexcept>
#include <utility>
#include <folly/Indestructible.h>
#include <folly/ScopeGuard.h>
#include <folly/io/async/urma/UrmaAsyncSocket.h>
#include <folly/io/async/urma/UrmaInit.h>

namespace apache::thrift::detail {
namespace {
struct RuntimeState {
  std::mutex mutex;
  size_t users{0};
  bool owned{false};
  folly::UrmaConfig requested;
};

RuntimeState& state() {
  static folly::Indestructible<RuntimeState> value;
  return *value;
}

bool sameConfig(const folly::UrmaConfig& a, const folly::UrmaConfig& b) {
  return a.dev_name == b.dev_name && a.rx_depth == b.rx_depth &&
      a.tx_depth == b.tx_depth && a.buffer_size == b.buffer_size &&
      a.handshake_timeout_ms == b.handshake_timeout_ms &&
      a.max_pending_bytes == b.max_pending_bytes;
}

struct RuntimeOwner {
  explicit RuntimeOwner(std::shared_ptr<UrmaServerRuntime> value)
      : runtime(std::move(value)) {}
  std::shared_ptr<UrmaServerRuntime> runtime;
};

// Base destruction is reverse declaration order: the URMA socket releases its
// hardware resources before RuntimeOwner can finalize the shared context.
class ServerUrmaSocket final : private RuntimeOwner,
                               public folly::UrmaAsyncSocket {
 public:
  ServerUrmaSocket(
      folly::AsyncSocket::UniquePtr socket,
      std::shared_ptr<UrmaServerRuntime> runtime)
      : RuntimeOwner(std::move(runtime)),
        folly::UrmaAsyncSocket(std::move(socket)) {}
};
} // namespace

UrmaServerRuntime::UrmaServerRuntime(const folly::UrmaConfig& config) {
  auto& shared = state();
  std::lock_guard<std::mutex> lock(shared.mutex);
  if (shared.users && !sameConfig(shared.requested, config)) {
    throw std::invalid_argument(
        "URMA servers in one process must use the same configuration");
  }
  bool initializedHere = false;
  bool contextAcquired = false;
  auto rollback = folly::makeGuard([&] {
    if (contextAcquired) {
      folly::UrmaInit::releaseContext();
    }
    if (initializedHere) {
      folly::UrmaInit::fini();
    }
  });
  if (!shared.users) {
    shared.owned = !folly::UrmaInit::isInitialized();
    // Copy before initializing hardware so allocation failure cannot leak it.
    shared.requested = config;
    if (shared.owned) {
      folly::UrmaInit::init(config);
      initializedHere = true;
    }
  }
  // Keep the context alive even while this server has no connections.
  if (!folly::UrmaInit::acquireContext()) {
    throw std::runtime_error("URMA context is unavailable");
  }
  contextAcquired = true;
  // Pin a borrowed context before reading its effective configuration.
  if (!shared.users && !shared.owned &&
      !sameConfig(folly::UrmaInit::getConfig(), config)) {
    throw std::invalid_argument(
        "ThriftServer URMA config differs from the existing Folly context");
  }
  ++shared.users;
  rollback.dismiss();
}

UrmaServerRuntime::~UrmaServerRuntime() {
  auto& shared = state();
  std::lock_guard<std::mutex> lock(shared.mutex);
  folly::UrmaInit::releaseContext();
  if (--shared.users == 0 && shared.owned) {
    folly::UrmaInit::fini();
  }
}

folly::AsyncTransport::UniquePtr makeUrmaServerTransport(
    folly::AsyncSocket::UniquePtr socket,
    std::shared_ptr<UrmaServerRuntime> runtime) {
  return folly::AsyncTransport::UniquePtr(
      new ServerUrmaSocket(std::move(socket), std::move(runtime)));
}
} // namespace apache::thrift::detail
#endif
