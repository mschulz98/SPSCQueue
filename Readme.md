# Lock-free SPSC Queue

This project is an exploration of throughput-focused performance optimizations on a lock-free single-producer single-consumer queue in C++.
With the applied optimizations (memory ordering, memory layout, cache alignment, index caching, exploratory batching strategy) throughput was improved by up to 40x over a baseline implementation.

This project draws inspiration from [1]. Optimizations described there are marked with *.

For clarity and ease of use, each optimization stage is available as a standalone header in the versions directory and can be selected with compile time definitions.

### Benchmark setup
 - CPU: Intel Core i7-6700HQ
 - OS: Ubuntu 24.04
 - compile: `g++ main.cpp -o main -O3 -DNDEBUG -DSTAGE=<version number> -DRECORD_SIZE=<record size>`
 - run: `taskset --cpu-list 2,3 ./main`

### Benchmark results
Measuring throughput. Each version includes preceding versions.
Benchmarks include result validation.

#### Record size: 8 bytes
| Version | Variant | Throughput (M op/s) | Speedup | Total speedup |
| :---: | :---: | ---: | ---: | ---: |
| 0 | - | 12.2 | - | - |
| 1 | - | 69.9 | 5.73 | 5.73 |
| 2 | A | 61.4 | 0.88 | 5.03 |
| 2 | B | 95.1 | 1.36 | 7.80 |
| 2 | avg | 78.3 | 1.12 | 6.42 |
| 3 | - | 97.6 | 1.03 | 8.00 |
| 4 | - | 385 | 3.94 | 31.56 |
| 5 | - | 434 | 1.13 | 35.57 |
| 6 | - | 489 | 1.13 | 40.08 |
| 7 | - | 518 | 1.06 | 42.46 |
| 8 | - | 520 | 1.00 | 42.64 |


#### Record size: 64B
| Version | Variant | Throughput (M op/s) | Speedup | Total speedup |
| :---: | :---: | ---: | ---: | ---: |
| 0 | - | 12.3 | - | - |
| 1 | - | 22.6 | 1.84 | 1.84 |
| 2 | A | 39.0 | 1.73 | 3.17 |
| 2 | B | 24.8 | 1.10 | 2.02 |
| 2 | avg | 31.9 | 1.41 | 2.59 |
| 3 | - | 29.4 | 0.92 | 2.39 |
| 4 | - | 118 | 4.01 | 9.59 |
| 5 | - | 117 | 0.99 | 9.51 |
| 6 | - | - | - | - |
| 7 | - | 141 | 1.21 | 11.46 |
| 8 | - | 142 | 1.01 | 11.54 |


#### Record size: 256 bytes
| Version | Variant | Throughput (M op/s) | Speedup | Total speedup |
| :---: | :---: | ---: | ---: | ---: |
| 0 | - | 10.7 | - | - |
| 1 | - | 14.3 | 1.34 | 1.34 |
| 2 | A | 18.5 | 1.29 | 1.73 |
| 2 | B | 18.9 | 1.32 | 1.77 |
| 2 | avg | 18.7 | 1.31 | 1.75 |
| 3 | - | 18.8 | 1.01 | 1.76 |
| 4 | - | 30.2 | 1.61 | 2.82 |
| 5 | - | 28.9 | 0.96 | 2.71 |
| 6 | - | - | - | - |
| 7 | - | 44.3 | 1.53 | 4.14 |
| 8 | - | 44.5 | 1.00 | 4.16 |

## Optimizations

### 0. Baseline*
Baseline implementation of a lock-free SPSC queue.

### 1. Acquire/Release*
Introduces acquire/release semantics for accessing indices.

### 2. Control block cache alignment*
Indices sharing a cache line causes false sharing. This is avoided by
aligning the atomic indices (or the control blocks owning them) to the
cache line size.

Variants (see next section for an explanation):
 - A (64 mod 128)
 - B ( 0 mod 128)

### 3. Queue alignment
Note: this was originally developed after some of the later optimizations
but was pulled ahead for benchmark consistency.

Benchmarks with aligned control blocks showed large performance inconsistencies.
Further testing (see 2A/B) revealed that a queue at address 64 mod 128
performed worse than a queue at address 0 mod 128.
A 0 mod 128 address can be forced by aligning the queue to 128 bytes.

### 4. Index caching*
Reading the index that can be modified by the other thread can be expensive.
In the current implementation, such reads occur at least once per operation.
By caching its value, the frequency of these reads can be reduced significantly.
Using the cached values is safe to do as the indices just increase monotonically.

### 5. Memory layout optimization
Variables can be thought of as being 'owned' by one thread / operation. An operation may only modify values it owns. Reading non-owned values is
permitted but can be expensive.

Under SPSC assumptions, an operation can be directly mapped to one thread it belongs to. Variables owned by the same operation/thread can be stored in the same cache line without introducing false or true sharing. Shared variables that are constant during operation (like the pointer) can be duplicated so that each cache line has its own copy.

This achieves a reduction from 384 bytes (5 cache lines) down to only
128 bytes (2 cache lines) assuming a default or any other empty allocator.

### 6. Producer/consumer static balancing
In section 4, throughput was increased by reducing the frequency of reads from the non-owned index. But when the producer and consumer threads work at different speeds, the faster side will still need to update its limit very frequently once it has caught up to the slower one.

This stage explores a proof of concept of balancing as a tool to stop one thread from catching up to the other too quickly.

The below output showcases a typical result of a test using the same logic that was used in section 5, but with some counters added to measure the number of stalls and batch sizes.
```
429184549 records/s
validated:true
Producer @ 0x7ffc6e0e9800
1330853 updates
199649 stalls
Average batch: 751
Consumer @ 0x7ffc6e0e9840
72207 updates
15817 stalls
Average batch: 13849
Buffer   @ 0x7752fd9bf010
```
This suggests that the producer was faster than the consumer, resulting in smaller batch sizes for push operations.

Results here are highly sensitive to changes in runtime cost of production / consumption of records. As seen in the example below, removing the validation logic from the consumer thread tips the balance to the other side and drastically reduces throughput, even though it is doing less work overall.
```
294152253 records/s
validated:false
Producer @ 0x7fffa431be80
2793750 updates
2104567 stalls
Average batch: 3579
Consumer @ 0x7fffa431bec0
17438705 updates
7132567 stalls
Average batch: 573
Buffer   @ 0x71d42bae9010
```

A static balance was approximated by introducing a spin delay on the faster side. This lead to an improvement in throughput over the unbalanced case.

### 7. Producer/consumer dynamic balancing
Static balancing being sensitive to small changes in throughput on either the producer or consumer side is unlikely to be useful for less predictable, real-world loads.

A naive adaptive balancing strategy adjusts the spin delay based on measured batch sizes. This keeps batch sizes large while nearly eliminating stalls compared to previous runs.
```
518860582 records/s
validated:true
Producer @ 0x7ffe33857400
574517 updates
380 stalls
Delay: 0
Average batch: 17405
Consumer @ 0x7ffe33857440
649884 updates
98 stalls
Delay: 32
Average batch: 15387
Buffer   @ 0x7b99eeaef010
```
In its current implementation, there is no guarantee that the delay is zero on one side, leaving room for improvements. Additionally, convergence could be optimized to be faster, which may be important for dynamic workloads.

#### A note on balancing
This project focuses on maximizing throughput. While maximum delay and delay threshold can be tuned, the delays introduced for balancing could have an adverse effect on latency that has not been measured yet.

### 8. Memory order tweaks
Relaxed memory ordering is applicable to loads of the owned indices in push/pop operations. No observable effect on performance in this case.

### Future considerations
 - improve benchmark methodology (warmup, core isolation, multiple/longer runs)
 - dynamic balancing improvements (convergence, consistency, optimality)
 - alternative batching strategies
 - latency analysis
 - evaluation on different architectures / more modern CPUs

### References
[1] Christopher Fretz: Beyond Sequential Consistency: Unlocking Hidden Performance Gains, CppCon 2025
