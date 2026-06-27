/*
 * stp_thread_pool.h - A lightweight and efficient thread pool implementation.
 *
 * Copyright (C) 2026 tmgj
 * 
 * This file is part of STP.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include "stp_inplace_function.h"

#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

#if defined(_MSC_VER)
    #include <intrin.h> // MSVC
#elif defined(__x86_64__) || defined(__i386__)
    #include <immintrin.h> // GCC/Clang for x86
#endif

namespace STP
{

inline static void cpu_pause()
{
#if defined(__x86_64__) || defined(__i386__) || \
    defined(_M_X64) || defined(_M_IX86)
    _mm_pause();
#elif defined(__aarch64__) || defined(__arm__) || \
      defined(_M_ARM) || defined(_M_ARM64)
    #if defined(_MSC_VER)
        __yield();
    #else
        __asm__ volatile("yield" ::: "memory");
    #endif
#else
    #if defined(_MSC_VER)
        _ReadWriteBarrier();
    #else
        __asm__ volatile("" ::: "memory");
    #endif
#endif
}

class ThreadPool
{
    using Task = InplaceFunction<void(), 56>;
public:
    explicit ThreadPool(size_t n) : thread_count_(n)
    {
        queues_.reserve(n);
        for (size_t i = 0; i < n; ++i)
            queues_.emplace_back(std::make_unique<WorkQueue>());
        threads_.reserve(n);
        for (size_t i = 0; i < n; ++i)
            threads_.emplace_back(&ThreadPool::worker, this, i);
    }

    ~ThreadPool()
    {
        // [1] release: Ensures that all previous operations (even if none
        // currently exist) are visible to worker threads that load this flag
        // with memory_order_acquire.
        stop_.store(true, std::memory_order_release);

        // [2] release: Ensures `stop_ = true` happens-before `epoch_`
        // update, making it visible to threads awoken by this change.
        epoch_.fetch_add(1, std::memory_order_release);
        epoch_.notify_all();
        for (auto& t : threads_)
            t.join();
    }

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    template <typename F, typename... Args>
        requires std::invocable<F, Args...>
    void execute(F&& f, Args&&... args)
    {
        enqueue(std::move_only_function<void()>(
            [f  = std::forward<F>(f),
             ...a = std::forward<Args>(args)]() mutable {
                std::invoke(std::move(f), std::move(a)...);
            }));
    }

    template <typename F, typename... Args>
    auto push(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>>
    {
        using R = std::invoke_result_t<F, Args...>;
        std::packaged_task<R()> pt(
            [f  = std::forward<F>(f),
             ...a = std::forward<Args>(args)]() mutable -> R {
                return std::invoke(std::move(f), std::move(a)...);
            });
        auto future = pt.get_future();
        enqueue(std::move_only_function<void()>(std::move(pt)));
        return future;
    }

private:
    /*
     * Lock-free MPMC bounded queue (based on Dmitry Vyukov's algorithm)
     *
     * Each cell holds an atomic sequence number `seq`:
     *   seq == pos             --> Cell is writable (available for producers)
     *   seq == pos + 1         --> Cell is written, pending consumption
     *   seq == pos + kCapacity --> Cell is consumed; ready for reuse
     *
     * head_: Write cursor contended by producers
     * tail_: Read cursor contended by consumers
     */
    struct alignas(64) WorkQueue
    {
        // Must be a power of 2; scale up based on workload.
        static constexpr size_t kCapacity = 512;
        static constexpr size_t kMask     = kCapacity - 1;

        struct Cell
        {
            std::atomic<size_t> seq;
            Task data;
        };

        WorkQueue() : buf_(new Cell[kCapacity])
        {
            for (size_t i = 0; i < kCapacity; ++i)
            {
                // [3] relaxed: pure single-threaded initialisation inside
                // the constructor. The object is not yet shared with other
                // threads, so no concurrent access or synchronization/barriers
                // is needed.
                buf_[i].seq.store(i, std::memory_order_relaxed);
            }
        }

        ~WorkQueue()
        {
            delete[] buf_;
        }

        WorkQueue(const WorkQueue&)            = delete;
        WorkQueue& operator=(const WorkQueue&) = delete;

        void push(Task task)
        {
            // [4] relaxed: Just getting a starting index to attempt CAS,
            // no ordering guarantee needed. Actual data publication is
            // handled via `cell.seq`.
            size_t pos = head_.load(std::memory_order_relaxed);
            while (true)
            {
                Cell&          cell = buf_[pos & kMask];
                // [5] acquire: Synchronizes with the release store at [13]
                // If we see the cell is free (`seq == pos`), we must ensure
                // that any reading of `cell.data` by previous consumers has
                // completed before we are allowed to overwrite it.
                size_t      seq  = cell.seq.load(std::memory_order_acquire);
                std::ptrdiff_t d = static_cast<std::ptrdiff_t>(seq)
                                 - static_cast<std::ptrdiff_t>(pos);
                if (d == 0)
                {
                    // [6] relaxed: the CAS on `head_` is only used to reserve
                    // the slot index; it carries no data. Publication of
                    // `cell.data` is handled exclusively by the release store
                    // on `seq` at [7], so no ordering is needed on the CAS
                    // itself. On failure, `pos` is updated to the latest
                    //  `head_` value (relaxed is sufficient for a retry).
                    if (head_.compare_exchange_weak(
                            pos, pos + 1, std::memory_order_relaxed))
                    {
                        cell.data = std::move(task);
                        // [7] release: Publishes `cell.data` to consumers. A
                        // consumer loading `seq` with acquire (see [11])
                        // will safely observe the written task (`cell.data`).
                        // This is the sole synchronization point between
                        // producer and consumer for task payload visibility.
                        cell.seq.store(pos + 1, std::memory_order_release);
                        return;
                    }
                }
                else if (d < 0)
                {
                    // Slot still owned by a not-yet-finished consumer (queue
                    // "full" relative to this slot). Back off and reread.
                    std::this_thread::yield();
                    // [8] relaxed: Same rationale as [4].
                    pos = head_.load(std::memory_order_relaxed);
                }
                else
                {
                    // Another producer advanced `head_`; refresh.
                    // [9] relaxed: Same rationale as [4].
                    pos = head_.load(std::memory_order_relaxed);
                }
            }
        }

        bool try_pop(Task& out)
        {
            // [10] relaxed: Same rationale as [4].
            size_t pos = tail_.load(std::memory_order_relaxed);
            while (true)
            {
                Cell&          cell = buf_[pos & kMask];
                // [11] acquire: synchronises with the release store at [7].
                // If we see seq == pos + 1, we are guaranteed to safely read
                // the fully constructed task (`cell.data`).
                size_t         seq  = cell.seq.load(std::memory_order_acquire);
                std::ptrdiff_t d    = static_cast<std::ptrdiff_t>(seq)
                                    - static_cast<std::ptrdiff_t>(pos + 1);
                if (d == 0)
                {
                    // [12] relaxed: the CAS on `tail_` only exclusively
                    // claims the slot. Task visibility was already guaranteed
                    // by [11]. [13] publishes the slot's availability, so no
                    // ordering needed here.
                    if (tail_.compare_exchange_weak(
                            pos, pos + 1, std::memory_order_relaxed))
                    {
                        out = std::move(cell.data);
                        // [13] release: Notifies producers that this slot is
                        // ready for reuse. Synchronises with [5] to prevent
                        // live data from being overwritten.
                        cell.seq.store(
                            pos + kCapacity, std::memory_order_release);
                        return true;
                    }
                }
                else if (d < 0)
                {
                    return false; // queue is empty
                }
                else
                {
                    // pos is stale; refresh and retry.
                    // [14] relaxed: Same rationale as [10].
                    pos = tail_.load(std::memory_order_relaxed);
                }
            }
        }

        bool try_steal(Task& out)
        {
            return try_pop(out);
        }

    private:
        Cell* buf_ {nullptr};
        alignas(64) std::atomic<size_t> head_{0};
        alignas(64) std::atomic<size_t> tail_{0};
    };

    /*
     * ===================== Task Dispatching =====================
     *
     * Missed-wakeup prevention protocol (Dekker's algorithm)
     *
     * The classic "futex-like" pattern is used to ensure a worker that is
     * about to sleep never misses a notification from enqueue().
     *
     * enqueue side:
     *   epoch_.fetch_add(seq_cst)   <-- A
     *   waiters_.load(seq_cst)      <-- B
     *
     * worker side:
     *   waiters_.fetch_add(seq_cst) <-- C
     *   epoch_.load(seq_cst)        <-- D
     *
     * Because A-B and C-D are all seq_cst, they are totally ordered. Either C
     * precedes B: enqueue sees waiters_ > 0 at B --> notifies. Or A precedes
     * D: worker sees new `epoch` at D --> does not sleep. In both cases a
     * wakeup is guaranteed.
     *
     * Any other memory order cannot prevent Store-Load reordering. A producer
     * might load `waiters_` before the new `epoch_` becomes visible (B
     * reordered before A). Similarly, D might be reordered before C. This
     * could cause the consumer to read an old `epoch_` (matching `snapshot`)
     * and sleep (`epoch_.wait`), while the producer reads `waiters_ == 0` and
     * skips the wakeup. The worker would then sleep indefinitely.
     */
    void enqueue(Task task)
    {
        // [15] relaxed: Sufficient for this context. In the destructor, there
        // is no other state published prior to `stop_.store`, and this is
        // merely checking the flag without needing further synchronization.
        // Stricter orders offer no extra benefit. This check is purely
        // defensive; the user is responsible for ensuring `enqueue` is called
        // within the thread pool's lifetime.
        if (stop_.load(std::memory_order_relaxed)) [[unlikely]]
            throw std::runtime_error("thread pool has stopped");

        static thread_local size_t local_next =
            std::hash<std::thread::id>{}(std::this_thread::get_id());

        if (local_pool_ != this)
        {
            if (++local_next >= thread_count_)
            {
                local_next = 0;
            }
        }
        const size_t target = (local_pool_ == this) ? local_id_ : local_next;
        queues_[target]->push(std::move(task));

        // [16] seq_cst: As explained above, seq_cst is required to prevent
        // Store-Load reordering. It ensures `epoch_` is committed to main
        // memory BEFORE we check `waiters_`.
        epoch_.fetch_add(1, std::memory_order_seq_cst);

        // [17] seq_cst: As explained above, this prevents `waiters_.load`
        // from being reordered before `epoch_.fetch_add`.
        if (waiters_.load(std::memory_order_seq_cst) > 0)
        {
            epoch_.notify_one();
        }
    }

    // ==== Check local queue first, then steal from neighbors ====
    bool try_pop_or_steal(size_t id, Task& task)
    {
        if (queues_[id]->try_pop(task))
        {
            return true;
        }
        thread_local uint32_t rng = id * 2654435761u + 1;
        auto fast_rand = [&]() {
            rng ^= rng << 13; rng ^= rng >> 17;
            rng ^= rng << 5; return rng;
        };
        size_t start = fast_rand() % thread_count_;
        for (size_t i = 0; i < thread_count_ - 1; ++i)
        {
            size_t victim = start + i;
            if (victim >= thread_count_)
            {
                victim -= thread_count_;
            }
            if (victim == id)
            {
                continue;
            }
            if (queues_[victim]->try_steal(task))
            {
                return true;
            }
        }
        return false;
    }

    // ==================== Worker Thread Loop ====================
    void worker(size_t id)
    {
        local_pool_ = this;
        local_id_   = id;

        while (true)
        {
            Task task;
            // -- Fast path: Local pop + steal --
            if (try_pop_or_steal(id, task))
            {
                task();
                continue;
            }

            // [18] relaxed: early-exit fast path.  If `stop_ == true` is
            // not yet visible here, the worker will take the acquire load
            // of `epoch_` at [19], and then see `stop_ == true` at [20],
            // so correctness is maintained.
            if (stop_.load(std::memory_order_relaxed))
            {
                return;
            }

            // -- Intermediate path: Spin wait --
            bool found = false;
            for (int spin = 0; spin < 64; ++spin)
            {
                // ::_mm_pause();
                cpu_pause();
                if (try_pop_or_steal(id, task))
                {
                    found = true;
                    break;
                }
            }
            if (found)
            {
                task();
                continue;
            }

            // -- Slow path: Wait via condition_variable --
            {
                // [19] acquire: Establishes a happens-before edge with any
                // prior `epoch_.fetch_add` (release/seq_cst) from `enqueue()`
                // or the destructor. Two effects:
                // (a) Tasks pushed before the matching epoch increment are
                //     visible to the subsequent `try_pop_or_steal()` call.
                // (b) The destructor's `stop_.store(release)` - which is
                //     sequenced before its `epoch_.fetch_add(release)` -
                //     becomes visible after this acquire, so the relaxed
                //     `stop_` load at [20] reliably observes `stop_ == true`.
                auto snapshot = epoch_.load(std::memory_order_acquire);

                // [20] relaxed: Safe because the acquire at [19] synchronizes
                // with the destructor's release sequence (`stop_.store(true)`
                // -> `epoch_.fetch_add`). If [19] reads the incremented
                // `epoch_`,`stop_ == true` is guaranteed to be visible here.
                //
                // Note: This check is mandatory. If the destructor runs before
                // [19] and this check is omitted, the worker thread may sleep
                // indefinitely at [24].
                if (stop_.load(std::memory_order_relaxed))
                {
                    return;
                }

                if (try_pop_or_steal(id, task))
                {
                    task();
                    continue;
                }

                // [21] seq_cst: Required to prevent Store-Load reordering.
                // It ensures `waiters_` is committed to main memory BEFORE
                // we re-check `epoch_`.
                waiters_.fetch_add(1, std::memory_order_seq_cst);

                // [22] seq_cst: Prevents `epoch_.load` from being
                // reordered before `waiters_.fetch_add`.
                if (epoch_.load(std::memory_order_seq_cst) != snapshot)
                {
                    // Epoch changed while we were registering: a task was
                    // submitted. Undo the waiter count and retry.

                    // [23] relaxed: We only need to atomically decrement the
                    // counter; no ordering is required, since we are not
                    // entering the wait. The worst case is that the delayed
                    // visibility of `waiters_` might cause an extra (harmless)
                    // `notify_one` from the producer.
                    waiters_.fetch_sub(1, std::memory_order_relaxed);
                    continue;
                }

                // [24] relaxed: The memory order on `wait` specifies how the
                // atomic load inside the `wait` is performed. No
                // synchronization is needed here. Correctness relies on the
                // facts below:
                // (a) Even if the destructor's `notify_all` executes before
                //     this `wait`, the underlying futex mechanism ensures the
                //     thread will not be suspended (but will instead return
                //     `EWOULDBLOCK`).
                // (b) After waking, the worker loops back to [19] and issues a
                //     fresh acquire load of `epoch_`, which then synchronizes
                //     with any release/seq_cst stores that triggered wakeup.
                epoch_.wait(snapshot, std::memory_order_relaxed);

                // [25] relaxed: Same rationale as [23].
                waiters_.fetch_sub(1, std::memory_order_relaxed);
            }
        }
    }

    const size_t                            thread_count_;
    std::vector<std::unique_ptr<WorkQueue>> queues_;
    std::vector<std::thread>                threads_;

    alignas(64) std::atomic<uint64_t>       epoch_{0};
    alignas(64) std::atomic<int64_t>        waiters_{0};
    alignas(64) std::atomic<bool>           stop_{false};

    static inline thread_local ThreadPool*  local_pool_ = nullptr;
    static inline thread_local size_t       local_id_   = 0;
};

} // namespace STP
