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

#include <thrift/lib/cpp2/server/IOWorkerContext.h>

#include <folly/portability/GFlags.h>
#include <thrift/lib/cpp2/server/detail/ReplyQueueWithBackendYield.h>

FOLLY_GFLAGS_DEFINE_bool(
    thrift_reply_queue_backend_yield,
    true,
    "Return from a libevent backend after an internal reply notification; "
    "configure before creating IO workers (false restores the old behavior)");

namespace apache {
namespace thrift {

void IOWorkerContext::init(folly::EventBase& eventBase) {
  eventBase_ = &eventBase;
  replyQueue_ = std::make_unique<
      detail::ReplyQueueWithBackendYield<ReplyInfo, ReplyInfoConsumer>>(
      eventBase, ReplyInfoConsumer(), FLAGS_thrift_reply_queue_backend_yield);
  replyQueue_->setMaxReadAtOnce(0);
  eventBase.runInEventBaseThread(
      [queue = replyQueue_.get(), &evb = eventBase, alive = alive_] {
        auto aliveLocked = alive->rlock();
        if (*aliveLocked) {
          queue->startConsumingInternal(&evb);
        }
      });
}

} // namespace thrift
} // namespace apache
