/* Copyright (c) Huawei Technologies Co., Ltd. 2026. Licensed under Apache-2.0. */
#pragma once

#ifdef FOLLY_HAVE_URMA
#include <memory>
#include <folly/io/async/AsyncSocket.h>
#include <folly/io/async/urma/UrmaConfig.h>

namespace apache::thrift::detail {

// One Folly URMA context is shared by all servers in a process. Active servers
// must request the same configuration; a pre-existing application context is
// borrowed and is never finalized by ThriftServer.
class UrmaServerRuntime {
 public:
  explicit UrmaServerRuntime(const folly::UrmaConfig& config);
  ~UrmaServerRuntime();
  UrmaServerRuntime(const UrmaServerRuntime&) = delete;
  UrmaServerRuntime& operator=(const UrmaServerRuntime&) = delete;
};

folly::AsyncTransport::UniquePtr makeUrmaServerTransport(
    folly::AsyncSocket::UniquePtr socket,
    std::shared_ptr<UrmaServerRuntime> runtime);

} // namespace apache::thrift::detail
#endif
