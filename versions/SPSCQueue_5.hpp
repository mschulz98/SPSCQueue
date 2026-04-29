#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>
#include <thread>

#ifndef SPIN_LIMIT
#define SPIN_LIMIT 16
#endif
#ifndef SPIN_WAIT_US
#define SPIN_WAIT_US 50
#endif

#ifdef __cpp_lib_hardware_interference_size
#include <new>
static constexpr std::size_t cache_align = std::hardware_destructive_interference_size;
#else
static constexpr std::size_t cache_align = 64;
#endif

static constexpr std::size_t queue_align = 2 * cache_align;

template<typename T, std::size_t N, typename Alloc=std::allocator<T>>
class alignas(queue_align) SPSCQueue
{
  static_assert(N && !(N & (N - 1)), "Size N must be a power of 2.");

public:
  struct alignas(cache_align) ControlBlock
  {
    std::atomic<std::size_t> index { 0 };
    mutable std::size_t cached_limit { 0 };
    T *data;
  };

  ControlBlock head;
  ControlBlock tail;

  [[no_unique_address]] Alloc allocator;

  static inline std::size_t map_index(std::size_t index);
  static inline void sleep(std::size_t &spin_counter);

  bool push_blocked(std::size_t index) const;
  bool pop_blocked(std::size_t index) const;

public:
  using ValueType = T;

  SPSCQueue();
  ~SPSCQueue();

  SPSCQueue(const SPSCQueue &) = delete;
  SPSCQueue &operator=(const SPSCQueue &) = delete;

  SPSCQueue(SPSCQueue &&) = delete;
  SPSCQueue &operator=(SPSCQueue &&) = delete;

  void push(const T &val);
  T pop();

  void print_metadata() const;
};

template<typename T, std::size_t N, typename Alloc>
SPSCQueue<T, N, Alloc>::SPSCQueue()
{
  head.data = allocator.allocate(N);
  tail.data = head.data;
}

template<typename T, std::size_t N, typename Alloc>
SPSCQueue<T, N, Alloc>::~SPSCQueue()
{
  while (head.index - tail.index)
  {
    pop();
  }

  allocator.deallocate(head.data, N);
}

template<typename T, std::size_t N, typename Alloc>
void SPSCQueue<T, N, Alloc>::push(const T &val)
{
  std::size_t spin_counter = 0;
  std::size_t index = head.index.load(std::memory_order_acquire);

  while (push_blocked(index))
  {
    sleep(spin_counter);
  }

  head.data[map_index(index)] = val;
  head.index.store(index + 1, std::memory_order_release);
}

template<typename T, std::size_t N, typename Alloc>
T SPSCQueue<T, N, Alloc>::pop()
{
  std::size_t spin_counter = 0;
  std::size_t index = tail.index.load(std::memory_order_acquire);

  while (pop_blocked(index))
  {
    sleep(spin_counter);
  }

  // with compiler optimizations enabled, reference/dtor
  // should be optimized out for trivially destructible types
  T &ref = tail.data[map_index(index)];
  T value = ref;
  ref.~T();

  tail.index.store(index + 1, std::memory_order_release);
  return value;
}

template<typename T, std::size_t N, typename Alloc>
std::size_t SPSCQueue<T, N, Alloc>::map_index(std::size_t i)
{
  return i % N;
}

template<typename T, std::size_t N, typename Alloc>
void SPSCQueue<T, N, Alloc>::sleep(std::size_t &spin_counter)
{
  if (spin_counter < SPIN_LIMIT)
  {
    ++spin_counter;
  }
  else
  {
    std::this_thread::sleep_for(std::chrono::microseconds(SPIN_WAIT_US));
  }
}

template<typename T, std::size_t N, typename Alloc>
bool SPSCQueue<T, N, Alloc>::push_blocked(std::size_t index) const
{
  if (index - head.cached_limit == N)
  {
    std::size_t prev_cached = head.cached_limit;
    head.cached_limit = tail.index.load(std::memory_order_acquire);
    return prev_cached == head.cached_limit;
  }
  else
  {
    return false;
  }
}

template<typename T, std::size_t N, typename Alloc>
bool SPSCQueue<T, N, Alloc>::pop_blocked(std::size_t index) const
{
  if (index == tail.cached_limit)
  {
    tail.cached_limit = head.index.load(std::memory_order_acquire);
    return index == tail.cached_limit;
  }
  else
  {
    return false;
  }
}

template<typename T, std::size_t N, typename Alloc>
void SPSCQueue<T, N, Alloc>::print_metadata() const
{
  std::cout << "Producer @ " << &head << "\n"
            << "Consumer @ " << &tail << "\n"
            << "Buffer   @ " << head.data << std::endl;
}
