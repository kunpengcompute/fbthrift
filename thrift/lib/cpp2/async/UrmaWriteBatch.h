/* Copyright (c) Huawei Technologies Co., Ltd. 2026. Licensed under Apache-2.0.
 */
#pragma once

#include <memory>
#include <utility>
#include <vector>
#include <folly/io/async/AsyncTransport.h>
#ifdef FOLLY_HAVE_URMA
#include <folly/io/async/urma/UrmaAsyncSocket.h>
#endif
#include <wangle/channel/OutputBufferingHandler.h>

namespace apache::thrift::detail {

inline bool usesUrmaRecords(folly::AsyncTransport* transport) {
#ifdef FOLLY_HAVE_URMA
  // Do not unwrap arbitrary transports: bypassing a TLS/transforming wrapper
  // would also bypass its write processing. Such wrappers retain normal writes.
  auto* writer = dynamic_cast<folly::UrmaRecordWriter*>(transport);
  return writer && writer->supportsUrmaRecords();
#else
  (void)transport;
  return false;
#endif
}

inline void writeWithUrmaRecords(
    folly::AsyncTransport* transport,
    folly::AsyncTransport::WriteCallback* callback,
    std::unique_ptr<folly::IOBuf> data,
    std::vector<size_t> lengths,
    folly::WriteFlags flags = folly::WriteFlags::NONE) {
#ifdef FOLLY_HAVE_URMA
  if (auto* urma = dynamic_cast<folly::UrmaRecordWriter*>(transport);
      urma && urma->supportsUrmaRecords()) {
    urma->writeChainWithRecords(
        callback, std::move(data), std::move(lengths), flags);
    return;
  }
#endif
  transport->writeChain(callback, std::move(data), flags);
}

// The batching handler records serialized lengths before concatenating IOBufs.
// TAsyncTransportHandler moves 'flushing' into the SAME synchronous fireWrite.
// Metadata never travels on the wire or survives separately from its batch.
struct UrmaWriteBatchState {
  std::vector<size_t> pending;
  std::vector<size_t> flushing;
};

class UrmaOutputBufferingHandler final : public wangle::OutputBufferingHandler {
 public:
  explicit UrmaOutputBufferingHandler(
      std::shared_ptr<UrmaWriteBatchState> state)
      : state_(std::move(state)) {}

  folly::Future<folly::Unit> write(
      Context* ctx, std::unique_ptr<folly::IOBuf> data) override {
    CHECK(data);
    // cleanUp() in the base is non-virtual; an empty sends_ also resets
    // metadata.
    if (!sends_) {
      state_->pending.clear();
    }
    if (queueSends_) {
      state_->pending.push_back(data->computeChainDataLength());
    } else {
      state_->flushing = {data->computeChainDataLength()};
    }
    return wangle::OutputBufferingHandler::write(ctx, std::move(data));
  }

  void runLoopCallback() noexcept override {
    auto state = state_; // A completed callback may destroy the pipeline.
    state->flushing = std::exchange(state->pending, {});
    wangle::OutputBufferingHandler::runLoopCallback();
    state->flushing.clear();
  }

  folly::Future<folly::Unit> close(Context* ctx) override {
    state_->pending.clear();
    state_->flushing.clear();
    return wangle::OutputBufferingHandler::close(ctx);
  }

 private:
  std::shared_ptr<UrmaWriteBatchState> state_;
};
} // namespace apache::thrift::detail
