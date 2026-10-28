/* Copyright (c) Huawei Technologies Co., Ltd. 2026. Licensed under Apache-2.0.
 */
#include <thrift/lib/cpp2/server/detail/UrmaServerRuntime.h>

#ifdef FOLLY_HAVE_URMA
#include <mutex>
#include <stdexcept>
#include <utility>
#include <folly/Conv.h>
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

void requireSameConfig(
    const folly::UrmaConfig& existing,
    const folly::UrmaConfig& requested,
    const char* context) {
  auto check =
      [&](const char* field, const auto& current, const auto& desired) {
        if (current != desired) {
          throw std::invalid_argument(folly::to<std::string>(
              context,
              ": ",
              field,
              " (existing=",
              current,
              ", requested=",
              desired,
              ")"));
        }
      };
  check("dev_name", existing.dev_name, requested.dev_name);
  check("rx_depth", existing.rx_depth, requested.rx_depth);
  check("tx_depth", existing.tx_depth, requested.tx_depth);
  check("buffer_size", existing.buffer_size, requested.buffer_size);
  check("enable_read", existing.enable_read, requested.enable_read);
  check("read_threshold", existing.read_threshold, requested.read_threshold);
  check(
      "handshake_timeout_ms",
      existing.handshake_timeout_ms,
      requested.handshake_timeout_ms);
  check(
      "max_pending_bytes",
      existing.max_pending_bytes,
      requested.max_pending_bytes);
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
  if (shared.users) {
    requireSameConfig(
        shared.requested,
        config,
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
  if (!shared.users && !shared.owned) {
    requireSameConfig(
        folly::UrmaInit::getConfig(),
        config,
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
