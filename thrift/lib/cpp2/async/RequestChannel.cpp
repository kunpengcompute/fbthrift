/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <thrift/lib/cpp2/async/RequestChannel.h>
#include <folly/ThreadLocal.h>
#include <folly/stats/Histogram.h>
#include <thrift/lib/cpp/transport/LatencyStatsFlags.h>

namespace apache {
namespace thrift {

namespace {
    constexpr int64_t kBucketSize = 10;
    constexpr int64_t kMin = 0;
    constexpr int64_t kMax = 100000;

    struct RequestSerializationTag {};

    folly::ThreadLocal<folly::Histogram<int64_t>, RequestSerializationTag> g_request_serialization_hist{
        []() { return new folly::Histogram<int64_t>(kBucketSize, kMin, kMax); }
    };

    double computeAvg(folly::ThreadLocal<folly::Histogram<int64_t>, RequestSerializationTag>& tl) {
        folly::Histogram<int64_t> result(kBucketSize, kMin, kMax);
        for (auto& item : tl.accessAllThreads()) {
            result.merge(item);
        }
        uint64_t totalCount = result.computeTotalCount();
        if (totalCount == 0) return 0.0;
        int64_t totalSum = 0;
        for (size_t i = 0; i < result.getNumBuckets(); ++i) {
            totalSum += result.getBucketByIndex(i).sum;
        }
        return static_cast<double>(totalSum) / static_cast<double>(totalCount);
    }

    double getPercentile(folly::ThreadLocal<folly::Histogram<int64_t>, RequestSerializationTag>& tl, double pct) {
        folly::Histogram<int64_t> result(kBucketSize, kMin, kMax);
        for (auto& item : tl.accessAllThreads()) {
            result.merge(item);
        }
        return static_cast<double>(result.getPercentileEstimate(pct));
    }

    void clearHist(folly::ThreadLocal<folly::Histogram<int64_t>, RequestSerializationTag>& tl) {
        for (auto& item : tl.accessAllThreads()) {
            item.clear();
        }
    }
}

void recordRequestSerializationLatency(int64_t latencyUs) {
    if (!apache::thrift::isLatencyStatsEnabled()) {
        return;
    }
    g_request_serialization_hist->addValue(latencyUs);
}

double getRequestSerializationAvg() {
    return computeAvg(g_request_serialization_hist);
}

double getRequestSerializationP50() {
    return getPercentile(g_request_serialization_hist, 0.5);
}

double getRequestSerializationP90() {
    return getPercentile(g_request_serialization_hist, 0.9);
}

double getRequestSerializationP99() {
    return getPercentile(g_request_serialization_hist, 0.99);
}

double getRequestSerializationP999() {
    return getPercentile(g_request_serialization_hist, 0.999);
}

void resetRequestSerializationStats() {
    clearHist(g_request_serialization_hist);
}


void RequestChannel::sendRequestResponse(
    const RpcOptions& rpcOptions,
    MethodMetadata&& metadata,
    SerializedRequest&& request,
    std::shared_ptr<transport::THeader> header,
    RequestClientCallback::Ptr clientCallback) {
  sendRequestResponse(
      folly::copy(rpcOptions),
      std::move(metadata),
      std::move(request),
      std::move(header),
      std::move(clientCallback));
}

void RequestChannel::sendRequestNoResponse(
    const RpcOptions& rpcOptions,
    MethodMetadata&& metadata,
    SerializedRequest&& request,
    std::shared_ptr<transport::THeader> header,
    RequestClientCallback::Ptr clientCallback) {
  sendRequestNoResponse(
      folly::copy(rpcOptions),
      std::move(metadata),
      std::move(request),
      std::move(header),
      std::move(clientCallback));
}

void RequestChannel::sendRequestStream(
    const RpcOptions& rpcOptions,
    MethodMetadata&& metadata,
    SerializedRequest&& request,
    std::shared_ptr<transport::THeader> header,
    StreamClientCallback* clientCallback) {
  sendRequestStream(
      folly::copy(rpcOptions),
      std::move(metadata),
      std::move(request),
      std::move(header),
      clientCallback);
}

void RequestChannel::sendRequestSink(
    const RpcOptions& rpcOptions,
    MethodMetadata&& metadata,
    SerializedRequest&& request,
    std::shared_ptr<transport::THeader> header,
    SinkClientCallback* clientCallback) {
  sendRequestSink(
      folly::copy(rpcOptions),
      std::move(metadata),
      std::move(request),
      std::move(header),
      clientCallback);
}

void RequestChannel::sendRequestResponse(
    RpcOptions&& rpcOptions,
    MethodMetadata&& metadata,
    SerializedRequest&& request,
    std::shared_ptr<transport::THeader> header,
    RequestClientCallback::Ptr clientCallback) {
  sendRequestResponse(
      rpcOptions,
      std::move(metadata),
      std::move(request),
      std::move(header),
      std::move(clientCallback));
}

void RequestChannel::sendRequestNoResponse(
    RpcOptions&& rpcOptions,
    MethodMetadata&& metadata,
    SerializedRequest&& request,
    std::shared_ptr<transport::THeader> header,
    RequestClientCallback::Ptr clientCallback) {
  sendRequestNoResponse(
      rpcOptions,
      std::move(metadata),
      std::move(request),
      std::move(header),
      std::move(clientCallback));
}

void RequestChannel::sendRequestStream(
    RpcOptions&& rpcOptions,
    MethodMetadata&& metadata,
    SerializedRequest&& request,
    std::shared_ptr<transport::THeader> header,
    StreamClientCallback* clientCallback) {
  sendRequestStream(
      rpcOptions,
      std::move(metadata),
      std::move(request),
      std::move(header),
      clientCallback);
}

void RequestChannel::sendRequestSink(
    RpcOptions&& rpcOptions,
    MethodMetadata&& metadata,
    SerializedRequest&& request,
    std::shared_ptr<transport::THeader> header,
    SinkClientCallback* clientCallback) {
  sendRequestSink(
      rpcOptions,
      std::move(metadata),
      std::move(request),
      std::move(header),
      clientCallback);
}

void RequestChannel::terminateInteraction(InteractionId) {
  folly::terminate_with<std::runtime_error>(
      "This channel doesn't support interactions");
}
InteractionId RequestChannel::createInteraction(ManagedStringView&& name) {
  static std::atomic<int64_t> nextId{0};
  int64_t id = 1 + nextId.fetch_add(1, std::memory_order_relaxed);
  return registerInteraction(std::move(name), id);
}
InteractionId RequestChannel::registerInteraction(
    ManagedStringView&&, int64_t) {
  folly::terminate_with<std::runtime_error>(
      "This channel doesn't support interactions");
}
InteractionId RequestChannel::createInteractionId(int64_t id) {
  return InteractionId(id);
}
void RequestChannel::releaseInteractionId(InteractionId&& id) {
  id.release();
}

uint64_t RequestChannel::getChecksumSamplingRate() const {
  return checksumSamplingRate_;
}
void RequestChannel::setChecksumSamplingRate(uint64_t samplingRate) {
  checksumSamplingRate_ = samplingRate;
}

template <typename ClientBridgePtr>
class RequestClientCallbackWrapper
    : public FirstResponseClientCallback<ClientBridgePtr> {
 public:
  explicit RequestClientCallbackWrapper(
      RequestClientCallback::Ptr requestCallback)
      : requestCallback_(std::move(requestCallback)) {}
  RequestClientCallbackWrapper(
      RequestClientCallback::Ptr requestCallback,
      const BufferOptions& bufferOptions)
      : requestCallback_(std::move(requestCallback)),
        bufferOptions_(bufferOptions) {}

  void onFirstResponse(
      FirstResponsePayload&& firstResponse,
      ClientBridgePtr clientBridge) override {
    auto tHeader = std::make_unique<transport::THeader>();
    tHeader->setClientType(THRIFT_ROCKET_CLIENT_TYPE);
    apache::thrift::detail::fillTHeaderFromResponseRpcMetadata(
        firstResponse.metadata, *tHeader);
    requestCallback_.release()->onResponse(ClientReceiveState::create(
        std::move(firstResponse.payload),
        std::move(tHeader),
        std::move(clientBridge),
        bufferOptions_));
    delete this;
  }

  void onFirstResponseError(folly::exception_wrapper ew) override {
    requestCallback_.release()->onResponseError(std::move(ew));
    delete this;
  }

 private:
  RequestClientCallback::Ptr requestCallback_;
  BufferOptions bufferOptions_;
};

StreamClientCallback* createStreamClientCallback(
    RequestClientCallback::Ptr requestCallback,
    const BufferOptions& bufferOptions) {
  DCHECK(requestCallback->isInlineSafe())
      << "Streaming methods do not support the callback client method flavor. "
         "Use co_, sync_, or semifuture_ instead.";

  return apache::thrift::detail::ClientStreamBridge::create(
      new RequestClientCallbackWrapper<
          apache::thrift::detail::ClientStreamBridge::ClientPtr>(
          std::move(requestCallback), bufferOptions));
}

SinkClientCallback* createSinkClientCallback(
    RequestClientCallback::Ptr requestCallback) {
  DCHECK(requestCallback->isInlineSafe())
      << "Sink methods do not support the callback client method flavor. "
         "Use co_, sync_, or semifuture_ instead.";

  return apache::thrift::detail::ClientSinkBridge::create(
      new RequestClientCallbackWrapper<
          apache::thrift::detail::ClientSinkBridge::ClientPtr>(
          std::move(requestCallback)));
}

template class ClientBatonCallback<true, true>;
template class ClientBatonCallback<true, false>;
template class ClientBatonCallback<false, true>;
template class ClientBatonCallback<false, false>;

} // namespace thrift
} // namespace apache
