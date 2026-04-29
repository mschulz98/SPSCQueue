#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

#ifndef STAGE
#define STAGE 8
#include "SPSCQueue.hpp"
#else
#define CAT_IMPL(a, b) a ## b
#define CAT(a, b) CAT_IMPL(a, b)
#define STR_IMPL(x) #x
#define STR(x) STR_IMPL(x)
#include STR(CAT(versions/SPSCQueue_, STAGE).hpp)
#undef STR
#undef STR_IMPL
#undef CAT
#undef CAT_IMPL
#endif

#ifndef RECORD_SIZE
#define RECORD_SIZE (8)
#endif

#ifndef BUFFER_SIZE
#define BUFFER_SIZE (1024 * 32)
#endif

using u64 = uint64_t;

#if STAGE < 4 || RECORD_SIZE > 64
u64 count = 1'000'000'000;
#else
u64 count = 10'000'000'000;
#endif

u64 sum = 0;

template<std::size_t N>
struct alignas(N) Record
{
  u64 value;
};

template<typename Queue>
void produce(Queue &queue)
{
  using ValueType = typename Queue::ValueType;

  ValueType record;
  record.value = 0;
  while (record.value < count)
  {
    queue.push(record);
    ++record.value;
  }
}

template<typename Queue>
void consume(Queue &queue)
{
  u64 i = 0;
  while (i < count)
  {
    sum += (queue.pop().value == i);
    ++i;
  }
}

template<typename Queue>
void consume_nv(Queue &queue)
{
  u64 i = 0;
  while (i < count)
  {
    queue.pop();
    ++i;
  }
}

template<typename Queue>
void benchmark(Queue &queue)
{
  sum = 0;

  auto start = std::chrono::steady_clock::now();

  std::thread producer{ produce<Queue>, std::ref(queue) };
  std::thread consumer{ consume<Queue>, std::ref(queue) };

  producer.join();
  consumer.join();

  auto end = std::chrono::steady_clock::now();
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
  if (elapsed_ms)
  {
    std::cout << count * 1000 / elapsed_ms << " records/s" << "\n";
  }
  else
  {
    std::cout << "Could not determine throughput.\n";
  }
  std::cout << "validated:" << ((sum == count) ? "true" : "false") << std::endl;
}

int main()
{
  using Queue = SPSCQueue<Record<RECORD_SIZE>, BUFFER_SIZE>;

  struct
  {
    Queue q1;
    // int x; // keep or comment out to change address parity
    Queue q2;
  } queues;

  benchmark(queues.q1);
  queues.q1.print_metadata();

  // benchmark(queues.q2);
  // queues.q2.print_metadata();

  return 0;
}
