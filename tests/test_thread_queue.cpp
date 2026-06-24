#include "util/ThreadQueue.hpp"

#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using llmcli::ThreadQueue;
using namespace std::chrono_literals;

TEST_CASE("items pop in FIFO order", "[threadqueue]") {
  ThreadQueue<int> q;
  for (int i = 0; i < 5; ++i) q.push(i);

  for (int i = 0; i < 5; ++i) {
    auto v = q.pop();
    REQUIRE(v.has_value());
    CHECK(*v == i);
  }
}

TEST_CASE("producer/consumer transfer every item across threads",
          "[threadqueue]") {
  ThreadQueue<int> q;
  constexpr int N = 1000;

  std::thread producer([&] {
    for (int i = 0; i < N; ++i) q.push(i);
    q.shutdown();
  });

  std::vector<int> received;
  while (auto v = q.pop()) received.push_back(*v);

  producer.join();

  REQUIRE(received.size() == N);
  for (int i = 0; i < N; ++i) CHECK(received[i] == i);
}

TEST_CASE("blocking pop wakes when an item is pushed", "[threadqueue]") {
  ThreadQueue<int> q;
  std::atomic<bool> got{false};

  std::thread consumer([&] {
    auto v = q.pop();  // blocks until push
    if (v && *v == 99) got = true;
  });

  std::this_thread::sleep_for(50ms);  // let the consumer block
  CHECK_FALSE(got.load());
  q.push(99);
  consumer.join();
  CHECK(got.load());
}

TEST_CASE("shutdown unblocks a waiting consumer with nullopt",
          "[threadqueue]") {
  ThreadQueue<int> q;
  std::atomic<bool> returned_empty{false};

  std::thread consumer([&] {
    auto v = q.pop();  // blocks on empty queue
    if (!v.has_value()) returned_empty = true;
  });

  std::this_thread::sleep_for(50ms);
  q.shutdown();
  consumer.join();
  CHECK(returned_empty.load());
}

TEST_CASE("shutdown still drains queued items before ending", "[threadqueue]") {
  ThreadQueue<int> q;
  q.push(1);
  q.push(2);
  q.shutdown();

  CHECK(q.pop() == 1);
  CHECK(q.pop() == 2);
  CHECK_FALSE(q.pop().has_value());  // now drained + closed
}

TEST_CASE("try_pop is non-blocking", "[threadqueue]") {
  ThreadQueue<int> q;
  int out = -1;
  CHECK_FALSE(q.try_pop(out));  // empty
  q.push(7);
  REQUIRE(q.try_pop(out));
  CHECK(out == 7);
}
