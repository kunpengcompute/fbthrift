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

#pragma once

#include <utility>

#include <folly/io/async/EventBaseAtomicNotificationQueue.h>

namespace apache {
namespace thrift {
namespace detail {

// An internal ReplyQueue does not keep an EventBase alive. With libevent,
// however, processing only an internal event need not finish EVLOOP_ONCE.
// Return to EventBase after that notification so the flush and rearm loop
// callbacks scheduled by execute() can run without an unrelated event.
// This adapter is bound to its worker EventBase for its entire lifetime.
template <typename Task, typename Consumer>
class ReplyQueueWithBackendYield final
    : public folly::EventBaseAtomicNotificationQueue<Task, Consumer> {
 public:
  using Base = folly::EventBaseAtomicNotificationQueue<Task, Consumer>;

  ReplyQueueWithBackendYield(
      folly::EventBase& eventBase, Consumer&& consumer, bool enabled)
      : Base(std::move(consumer)),
        backend_(enabled && eventBase.getLibeventBase()
                ? eventBase.getBackend()
                : nullptr) {}

 private:
  void handlerReady(uint16_t) noexcept override {
    // Keep the backend before invoking user code. IOWorkerContext retains its
    // existing deferred queue destruction and EventBase lifetime guarantees.
    auto* backend = backend_;
    Base::execute();
    if (backend) {
      // Does not set EventBase::stop_ and does not change internal registration.
      // Once per notification, not once per reply. Manual execute()/drain()
      // must not leave a break request for a future backend invocation.
      backend->eb_event_base_loopbreak();
    }
  }

  folly::EventBaseBackendBase* const backend_;
};

} // namespace detail
} // namespace thrift
} // namespace apache
