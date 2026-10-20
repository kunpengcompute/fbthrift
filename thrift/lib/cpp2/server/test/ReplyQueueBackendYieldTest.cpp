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

#include <thrift/lib/cpp2/server/detail/ReplyQueueWithBackendYield.h>

#include <chrono>
#include <functional>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include <event2/event.h>
#include <event2/event_struct.h>
#include <gtest/gtest.h>

namespace apache {
namespace thrift {
namespace {

struct Consumer {
  std::function<void(int)> consume;
  void operator()(int&& value) noexcept { consume(value); }
};
using Queue = detail::ReplyQueueWithBackendYield<int, Consumer>;

void prime(folly::EventBase& evb) {
  for (int i = 0; i < 4; ++i) {
    evb.loopOnce(EVLOOP_NONBLOCK);
  }
}

// A real libevent timer, not an EventBase internal timer: in the negative
// control it is the unrelated event that finally releases EVLOOP_ONCE.
class RescueTimer {
 public:
  RescueTimer(folly::EventBase& evb, bool internal = false) : evb_(evb) {
    event_.reset(event_new(
        evb.getLibeventBase(), -1, 0,
        [](evutil_socket_t, short, void* arg) {
          auto& self = *static_cast<RescueTimer*>(arg);
          self.fired = true;
          self.evb_.terminateLoopSoon();
          event_base_loopbreak(self.evb_.getLibeventBase());
        },
        this));
    if (!event_) {
      throw std::runtime_error("could not create rescue timer");
    }
    if (internal) {
      event_->ev_evcallback.evcb_flags |= EVLIST_INTERNAL;
    }
    timeval timeout{1, 0};
    if (event_add(event_.get(), &timeout) != 0) {
      throw std::runtime_error("could not register rescue timer");
    }
  }
  bool fired{false};

 private:
  folly::EventBase& evb_;
  std::unique_ptr<event, decltype(&event_free)> event_{nullptr, event_free};
};

void checkFlushOrdering(bool enabled) {
  folly::EventBase evb;
  std::vector<int> order;
  bool flushBeforeRescue = false;
  RescueTimer rescue(evb);
  Queue queue(evb, Consumer{[&](int value) {
                order.push_back(value);
                evb.runInLoop([&] {
                  flushBeforeRescue = !rescue.fired;
                  order.push_back(2);
                  evb.terminateLoopSoon();
                });
              }}, enabled);
  queue.startConsumingInternal(&evb);
  prime(evb);
  std::thread producer([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue.putMessage(1);
  });
  evb.loopForever();
  producer.join();
  queue.stopConsuming();
  EXPECT_EQ(order, (std::vector<int>{1, 2}));
  EXPECT_EQ(flushBeforeRescue, enabled);
  EXPECT_EQ(rescue.fired, !enabled);
}

TEST(ReplyQueueBackendYield, FlushRunsBeforeUnrelatedEvent) {
  checkFlushOrdering(true);
}

TEST(ReplyQueueBackendYield, DisabledReproducesUnrelatedEventDependency) {
  checkFlushOrdering(false);
}

TEST(ReplyQueueBackendYield, SeparateBatchesRearmWithoutStoppingEventBase) {
  folly::EventBase evb;
  RescueTimer rescue(evb);
  std::vector<int> consumed;
  Queue queue(evb, Consumer{[&](int value) {
                consumed.push_back(value);
                if (consumed.size() == 2) {
                  evb.runInLoop([&] { evb.terminateLoopSoon(); });
                }
              }}, true);
  queue.startConsumingInternal(&evb);
  prime(evb);
  std::thread producer([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue.putMessage(1);
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    queue.putMessage(2);
  });
  evb.loopForever();
  producer.join();
  EXPECT_FALSE(rescue.fired);
  EXPECT_EQ(consumed, (std::vector<int>{1, 2}));
}

TEST(ReplyQueueBackendYield, ConcurrentProducersAndBoundedBatches) {
  folly::EventBase evb;
  RescueTimer rescue(evb);
  constexpr int kProducers = 4;
  constexpr int kPerProducer = 500;
  std::vector<int> seen(kProducers * kPerProducer);
  int flushed = 0;
  Queue queue(evb, Consumer{[&](int value) {
                ++seen.at(value);
                evb.runInLoop([&] {
                  if (++flushed == kProducers * kPerProducer) {
                    evb.terminateLoopSoon();
                  }
                });
              }}, true);
  queue.setMaxReadAtOnce(32);
  queue.startConsumingInternal(&evb);
  prime(evb);
  std::vector<std::thread> producers;
  for (int p = 0; p < kProducers; ++p) {
    producers.emplace_back([&, p] {
      for (int i = 0; i < kPerProducer; ++i) {
        queue.putMessage(p * kPerProducer + i);
      }
    });
  }
  evb.loopForever();
  for (auto& producer : producers) {
    producer.join();
  }
  EXPECT_FALSE(rescue.fired);
  EXPECT_EQ(flushed, kProducers * kPerProducer);
  for (int count : seen) {
    EXPECT_EQ(count, 1);
  }
}

TEST(ReplyQueueBackendYield, QueuedBeforeStartAndRepeatedStopStart) {
  folly::EventBase evb;
  int consumed = 0;
  Queue queue(evb, Consumer{[&](int) { ++consumed; }}, true);
  // Internal events alone are deliberately skipped by a naturally exiting
  // EventBase; represent the server's listening socket with an ordinary event.
  RescueTimer rescue(evb);
  for (int round = 0; round < 50; ++round) {
    for (int i = 0; i < 100; ++i) {
      queue.putMessage(i);
    }
    queue.startConsumingInternal(&evb);
    for (int spin = 0; spin < 1000 && consumed < (round + 1) * 100;
         ++spin) {
      evb.loopOnce(EVLOOP_NONBLOCK);
    }
    EXPECT_EQ(consumed, (round + 1) * 100);
    queue.stopConsuming();
    prime(evb); // A stopped queue must have cancelled its pending rearm.
  }
}

TEST(ReplyQueueBackendYield, InternalRegistrationStillAllowsNaturalExit) {
  folly::EventBase evb;
  Queue queue(evb, Consumer{[](int) {}}, true);
  queue.startConsumingInternal(&evb);
  prime(evb);
  // An ordinary rescue timer would itself prevent natural exit.
  RescueTimer rescue(evb, true);
  evb.loop();
  EXPECT_FALSE(rescue.fired);
}

TEST(ReplyQueueBackendYield, DeferredDestructionCancelsRearm) {
  folly::EventBase evb;
  RescueTimer rescue(evb);
  int consumed = 0;
  std::unique_ptr<Queue::Base> queue;
  queue = std::make_unique<Queue>(evb, Consumer{[&](int) {
    ++consumed;
    evb.runInLoop([&] {
      queue.reset();
      evb.terminateLoopSoon();
    });
  }}, true);
  queue->startConsumingInternal(&evb);
  prime(evb);
  queue->putMessage(1);
  evb.loopForever();
  prime(evb);
  EXPECT_FALSE(rescue.fired);
  EXPECT_EQ(consumed, 1);
  EXPECT_EQ(queue, nullptr);
}

// A counting backend makes the safety gate observable without requiring kernel
// io_uring support on every build machine. The opaque variant advertises no
// libevent base, like IoUringBackend, while forwarding event operations here.
class CountingBackend : public folly::EventBaseBackendBase {
 public:
  explicit CountingBackend(bool opaque) : opaque_(opaque) {}
  event_base* getEventBase() override { return opaque_ ? nullptr : base_.get(); }
  int eb_event_base_loop(int flags) override {
    return event_base_loop(base_.get(), flags);
  }
  int eb_event_base_loopbreak() override {
    ++breaks;
    return event_base_loopbreak(base_.get());
  }
  int eb_event_add(Event& ev, const timeval* timeout) override {
    // EventHandler has already applied EVLIST_INTERNAL. Libevent's base_set
    // rejects that flag, so temporarily remove it while binding our hidden
    // base, then restore the registration semantics before adding the event.
    auto flags = folly::event_ref_flags(ev.getEvent());
    const auto internal = flags & EVLIST_INTERNAL;
    flags &= ~EVLIST_INTERNAL;
    const int result = event_base_set(base_.get(), ev.getEvent());
    flags |= internal;
    if (result != 0) {
      return result;
    }
    return event_add(ev.getEvent(), timeout);
  }
  int eb_event_del(Event& ev) override { return event_del(ev.getEvent()); }
  bool eb_event_active(Event& ev, int result) override {
    event_active(ev.getEvent(), result, 0);
    return true;
  }
  int breaks{0};

 private:
  bool opaque_;
  std::unique_ptr<event_base, decltype(&event_base_free)> base_{
      event_base_new(), event_base_free};
};

TEST(ReplyQueueBackendYield, ManualExecuteAndDrainDoNotBreakBackend) {
  CountingBackend* backend = nullptr;
  folly::EventBase::Options options;
  options.setBackendFactory([&] {
    auto result = std::make_unique<CountingBackend>(false);
    backend = result.get();
    return result;
  });
  folly::EventBase evb(options);
  int consumed = 0;
  Queue queue(evb, Consumer{[&](int) { ++consumed; }}, true);
  queue.startConsumingInternal(&evb);
  prime(evb);
  const int previous = backend->breaks;
  queue.putMessage(1);
  queue.execute();
  queue.putMessage(2);
  queue.drain();
  EXPECT_EQ(consumed, 2);
  EXPECT_EQ(backend->breaks, previous);
}

TEST(ReplyQueueBackendYield, OpaqueBackendIsNotBroken) {
  CountingBackend* backend = nullptr;
  folly::EventBase::Options options;
  options.setBackendFactory([&] {
    auto result = std::make_unique<CountingBackend>(true);
    backend = result.get();
    return result;
  });
  folly::EventBase evb(options);
  int consumed = 0;
  Queue queue(evb, Consumer{[&](int) { ++consumed; }}, true);
  queue.startConsuming(&evb);
  queue.putMessage(1);
  prime(evb);
  EXPECT_EQ(consumed, 1);
  EXPECT_EQ(backend->breaks, 0);
}

} // namespace
} // namespace thrift
} // namespace apache
