/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * UrmaCpp2Worker:
 *   继承 Cpp2Worker,覆盖 onDoneAcceptingConnection(virtual),
 *   accept TCP fd 后创建 UrmaAsyncSocket 并启动 URMA 握手。
 *
 *   所有 URMA 细节(urma_init / MR pool / JFR / 握手)均在 folly 层封装,
 *   本文件仅调用 folly API,不直接引用 urma_api.h。
 *
 *   流程:
 *     wangle AsyncServerSocket accept(fd)
 *     → Acceptor::connectionAccepted → acceptConnection(canAccept + socket options)
 *     → UrmaCpp2Worker::onDoneAcceptingConnection(IO 线程,裸 fd)
 *       ├─ folly::UrmaTransportWrapper::createAndOffload(evb, fd)
 *       │    └─ new UrmaAsyncSocket(evb, fd)  ← folly 内部,启动服务端握手
 *       └─ Acceptor::connectionReady(NONE)   ← 走正常探测/路由(不经 processEstablishedConnection)
 *            → onNewConnection → TransportPeekingManager
 *            → routing handlers(Rocket / Header / ...)
 */
#pragma once

#ifdef FOLLY_HAVE_URMA

#include <memory>

#include <folly/io/async/urma/UrmaTransportWrapper.h>
#include <folly/portability/Sockets.h>

#include <thrift/lib/cpp2/server/Cpp2Worker.h>
#include <thrift/lib/cpp2/server/ThriftServer.h>

namespace apache {
namespace thrift {

class UrmaCpp2Worker : public Cpp2Worker {
 public:
  static std::shared_ptr<UrmaCpp2Worker> create(
      ThriftServer* server,
      folly::EventBase* evb) {
    auto worker = std::shared_ptr<UrmaCpp2Worker>(new UrmaCpp2Worker(server));
    CHECK(!worker->accConfig_.isSSL())
        << "URMA does not support TLS; do not configure SSL with useUrma=true";
    CHECK(!worker->accConfig_.preferIoUring)
        << "URMA is incompatible with io_uring; disable preferIoUring with useUrma=true";
    worker->setFizzCertManager(nullptr);
    worker->setSSLContextManager(nullptr);
    worker->construct(server, nullptr, evb, nullptr);
    return worker;
  }

  void onDoneAcceptingConnection(
      int fd,
      const folly::SocketAddress& clientAddr,
      std::chrono::steady_clock::time_point acceptTime,
      const AcceptInfo& info,
      folly::AsyncTransport::LifecycleObserver* /*observer*/) noexcept override {
    auto transport = folly::UrmaTransportWrapper::createAndOffload(
        getEventBase(), fd);
    if (!transport) {
      LOG(ERROR) << "URMA offload failed, fd=" << fd;
      folly::netops::close(folly::NetworkSocket::fromFd(fd));
      return;
    }

    wangle::TransportInfo tinfo;
    tinfo.timeBeforeEnqueue = info.timeBeforeEnqueue;
    tinfo.secure = false;
    tinfo.acceptTime = acceptTime;
    connectionReady(
        std::move(transport),
        clientAddr,
        std::string(),
        wangle::SecureTransportType::NONE,
        tinfo);
  }

 private:
  explicit UrmaCpp2Worker(ThriftServer* server)
      : Cpp2Worker(server, {}) {}
};

/**
 * UrmaAcceptorFactory:
 *   为每个 IO 线程创建 UrmaCpp2Worker。
 */
class UrmaAcceptorFactory : public wangle::AcceptorFactory {
 public:
  explicit UrmaAcceptorFactory(ThriftServer* server) : server_(server) {}

  std::shared_ptr<wangle::Acceptor> newAcceptor(
      folly::EventBase* eventBase) override {
    return UrmaCpp2Worker::create(server_, eventBase);
  }

  private:
  ThriftServer* server_;
};

} // namespace thrift
} // namespace apache

#endif // FOLLY_HAVE_URMA
