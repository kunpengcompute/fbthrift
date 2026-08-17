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

#include <array>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>

#include <folly/portability/GFlags.h>
#include <folly/portability/GTest.h>
#include <folly/synchronization/Baton.h>
#include <thrift/lib/cpp/concurrency/FunctionRunner.h>
#include <thrift/lib/cpp/concurrency/ThreadManager.h>

FOLLY_GFLAGS_DECLARE_bool(thrift_thread_manager_direct_func_enabled);

using apache::thrift::concurrency::FunctionRunner;
using apache::thrift::concurrency::PriorityThreadManager;
using apache::thrift::concurrency::Runnable;
using apache::thrift::concurrency::ThreadManager;
using namespace std::chrono_literals;

namespace {

using ManagerFactory = std::function<std::shared_ptr<ThreadManager>()>;

class ThreadManagerDirectFuncTest : public testing::Test {
 private:
  gflags::FlagSaver flagSaver_;
};

std::array<ManagerFactory, 3> managerFactories() {
  return {
      [] { return ThreadManager::newSimpleThreadManager(1); },
      [] { return ThreadManager::newPriorityQueueThreadManager(1); },
      [] {
        return PriorityThreadManager::newPriorityThreadManager(
            {{1, 1, 1, 1, 1}});
      }};
}

TEST_F(
    ThreadManagerDirectFuncTest,
    DirectPathExecutesMoveOnlyFuncAcrossManagerImplementations) {
  FLAGS_thrift_thread_manager_direct_func_enabled = true;

  for (const auto& makeManager : managerFactories()) {
    auto manager = makeManager();
    manager->start();

    std::atomic<int> observed{0};
    folly::Baton<> done;
    auto value = std::make_unique<int>(42);
    manager->add([value = std::move(value), &observed, &done]() mutable {
      observed.store(*value, std::memory_order_relaxed);
      value.reset();
      done.post();
    });

    const bool completed = done.try_wait_for(5s);
    manager->join();

    EXPECT_TRUE(completed);
    EXPECT_EQ(observed.load(std::memory_order_relaxed), 42);
  }
}

TEST_F(ThreadManagerDirectFuncTest, DisabledFlagUsesLegacyExecutionPath) {
  FLAGS_thrift_thread_manager_direct_func_enabled = false;

  auto manager = ThreadManager::newSimpleThreadManager(1);
  manager->start();

  std::atomic<int> calls{0};
  folly::Baton<> done;
  manager->add([&] {
    calls.fetch_add(1, std::memory_order_relaxed);
    done.post();
  });

  const bool completed = done.try_wait_for(5s);
  manager->join();

  EXPECT_TRUE(completed);
  EXPECT_EQ(calls.load(std::memory_order_relaxed), 1);
}

TEST_F(
    ThreadManagerDirectFuncTest,
    RemoveNextPendingLazilyMaterializesFunctionRunner) {
  FLAGS_thrift_thread_manager_direct_func_enabled = true;

  auto manager = ThreadManager::newSimpleThreadManager(1);
  manager->start();

  folly::Baton<> blockerStarted;
  folly::Baton<> unblockWorker;
  manager->add([&] {
    blockerStarted.post();
    unblockWorker.wait();
  });

  if (!blockerStarted.try_wait_for(5s)) {
    unblockWorker.post();
    manager->join();
    FAIL() << "worker did not start the blocking task";
  }

  std::atomic<int> calls{0};
  manager->add([&] { calls.fetch_add(1, std::memory_order_relaxed); });

  std::shared_ptr<Runnable> removed = manager->removeNextPending();
  unblockWorker.post();
  manager->join();

  ASSERT_NE(removed, nullptr);
  EXPECT_NE(dynamic_cast<FunctionRunner*>(removed.get()), nullptr);
  EXPECT_EQ(calls.load(std::memory_order_relaxed), 0);

  removed->run();
  EXPECT_EQ(calls.load(std::memory_order_relaxed), 1);
}

TEST_F(ThreadManagerDirectFuncTest, PriorityApisCoverDirectAndFallbackRoutes) {
  auto priorityManager =
      PriorityThreadManager::newPriorityThreadManager({{1, 1, 1, 1, 1}});
  priorityManager->start();

  folly::Baton<> priorityLegacyAdd;
  folly::Baton<> priorityDirectAddWithPriority;
  folly::Baton<> priorityLegacyAddWithPriority;

  FLAGS_thrift_thread_manager_direct_func_enabled = false;
  priorityManager->add([&] { priorityLegacyAdd.post(); });
  FLAGS_thrift_thread_manager_direct_func_enabled = true;
  priorityManager->addWithPriority(
      [&] { priorityDirectAddWithPriority.post(); }, 1);
  FLAGS_thrift_thread_manager_direct_func_enabled = false;
  priorityManager->addWithPriority(
      [&] { priorityLegacyAddWithPriority.post(); }, -1);

  EXPECT_TRUE(priorityLegacyAdd.try_wait_for(5s));
  EXPECT_TRUE(priorityDirectAddWithPriority.try_wait_for(5s));
  EXPECT_TRUE(priorityLegacyAddWithPriority.try_wait_for(5s));
  priorityManager->join();

  auto priorityQueueManager = ThreadManager::newPriorityQueueThreadManager(1);
  priorityQueueManager->start();

  folly::Baton<> priorityQueueLegacyAdd;
  folly::Baton<> priorityQueueDirectAddWithPriority;
  folly::Baton<> priorityQueueLegacyAddWithPriority;

  FLAGS_thrift_thread_manager_direct_func_enabled = false;
  priorityQueueManager->add([&] { priorityQueueLegacyAdd.post(); });
  FLAGS_thrift_thread_manager_direct_func_enabled = true;
  priorityQueueManager->addWithPriority(
      [&] { priorityQueueDirectAddWithPriority.post(); }, 1);
  FLAGS_thrift_thread_manager_direct_func_enabled = false;
  priorityQueueManager->addWithPriority(
      [&] { priorityQueueLegacyAddWithPriority.post(); }, -1);

  EXPECT_TRUE(priorityQueueLegacyAdd.try_wait_for(5s));
  EXPECT_TRUE(priorityQueueDirectAddWithPriority.try_wait_for(5s));
  EXPECT_TRUE(priorityQueueLegacyAddWithPriority.try_wait_for(5s));
  priorityQueueManager->join();
}

TEST_F(
    ThreadManagerDirectFuncTest,
    KeepAliveSourceRoutesCoverDirectAndFallbackPaths) {
  for (const auto& makeManager : managerFactories()) {
    auto manager = makeManager();
    manager->start();

    folly::Baton<> directDone;
    folly::Baton<> fallbackDone;
    {
      auto executor = manager->getKeepAlive(
          ThreadManager::ExecutionScope(
              apache::thrift::concurrency::PRIORITY::NORMAL),
          ThreadManager::Source::UPSTREAM);

      FLAGS_thrift_thread_manager_direct_func_enabled = true;
      executor->add([&] { directDone.post(); });
      FLAGS_thrift_thread_manager_direct_func_enabled = false;
      executor->add([&] { fallbackDone.post(); });
    }

    EXPECT_TRUE(directDone.try_wait_for(5s));
    EXPECT_TRUE(fallbackDone.try_wait_for(5s));
    manager->join();
  }
}

} // namespace
