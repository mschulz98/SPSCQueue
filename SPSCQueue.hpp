#pragma once

#include <atomic>
#include <cassert>
#include <cstddef>
#include <iostream>
#include <memory>

#ifdef __x86_64__
#include <emmintrin.h>
#endif

#ifdef __cpp_lib_hardware_interference_size
#include <new>
static constexpr std::size_t cache_align = std::hardware_destructive_interference_size;
#else
static constexpr std::size_t cache_align = 64;
#endif

static constexpr std::size_t queue_align = 2 * cache_align;

/**
 * @brief A single-producer, single-consumer queue.
 * @tparam T Record type. POD types recommended, but it is safe to use non-trivially
 * destructible types as long as they're copyable.
 * @tparam Alloc Allocator. Invoked once to create the buffer and once to free it, resizing is not supported.
 * @tparam N Queue capacity in number of elements. Must be a power of 2.
 */
template<typename T, std::size_t N, typename Alloc=std::allocator<T>>
class alignas(queue_align) SPSCQueue
{
  static_assert(N && !(N & (N - 1)), "Size N must be a power of 2.");

  static constexpr std::size_t delay_incr_threshold = N / 2 - N / 32;
  static constexpr std::size_t max_delay = 500;

private:
  struct alignas(cache_align) ControlBlock
  {
    std::atomic<std::size_t> index { 0 };
    mutable std::size_t cached_limit { 0 };
    mutable std::size_t update_counter { 0 };
    mutable std::size_t stall_counter { 0 };
    mutable std::size_t last_batch_end { 0 };
    mutable std::size_t delay { 0 };
    T *data;
  };

  ControlBlock head;
  ControlBlock tail;

  [[no_unique_address]] Alloc allocator;

  static inline std::size_t map_index(std::size_t index);
  static inline void wait(std::size_t iterations);
  static inline void update_delay(std::size_t &delay, std::size_t last_batch);
  static inline void print_blk_meta(const ControlBlock &block, std::string block_name);

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

  /**
   * @brief Pushes an item onto the queue. Waits if there is no available space.
   * @param val 
   */
  void push(const T &val);

  /**
   * @brief Pops an item from the queue and returns it. Waits if the queue is empty.
   * @return 
   */
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
  std::size_t index = head.index.load(std::memory_order_relaxed);

  while (push_blocked(index))
  {
    ++head.stall_counter;
  }

  head.data[map_index(index)] = val;
  head.index.store(index + 1, std::memory_order_release);
}

template<typename T, std::size_t N, typename Alloc>
T SPSCQueue<T, N, Alloc>::pop()
{
  std::size_t index = tail.index.load(std::memory_order_relaxed);

  while (pop_blocked(index))
  {
    ++tail.stall_counter;
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
void SPSCQueue<T, N, Alloc>::wait(std::size_t iterations)
{
  for (std::size_t i = 0; i < iterations; ++i)
  {
    #ifdef __x86_64__
    _mm_pause();
    #elif defined(__arm__)
    __yield();
    #endif
  }
}

template<typename T, std::size_t N, typename Alloc>
void SPSCQueue<T, N, Alloc>::update_delay(std::size_t &delay, std::size_t last_batch)
{
  if (last_batch < delay_incr_threshold)
  {
    delay += (delay < max_delay);
  }
  else
  {
    delay -= (delay > 0);
  }
}

template<typename T, std::size_t N, typename Alloc>
void SPSCQueue<T, N, Alloc>::print_blk_meta(const ControlBlock &block, std::string block_name)
{
  std::cout << block_name << " @ " << &block << "\n"
            << block.update_counter << " updates\n"
            << block.stall_counter << " stalls\n"
            << block.delay << " delay iterations\n"
            << "Average batch size: " << static_cast<std::size_t>(block.index / std::max(1lu, block.update_counter)) << "\n";
}

template<typename T, std::size_t N, typename Alloc>
bool SPSCQueue<T, N, Alloc>::push_blocked(std::size_t index) const
{
  if (index - head.cached_limit == N)
  {
    update_delay(head.delay, index - head.last_batch_end);
    head.last_batch_end = index;
    wait(head.delay);

    std::size_t prev_cached = head.cached_limit;
    head.cached_limit = tail.index.load(std::memory_order_acquire);
    ++head.update_counter;
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
    update_delay(tail.delay, index - tail.last_batch_end);
    tail.last_batch_end = index;
    wait(tail.delay);

    tail.cached_limit = head.index.load(std::memory_order_acquire);
    ++tail.update_counter;
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
  print_blk_meta(head, "Producer");
  print_blk_meta(tail, "Consumer");
  std::cout << "\nBuffer   @ " << head.data << std::endl;
}
