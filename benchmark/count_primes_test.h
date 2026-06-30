/*
 * count_primes_test.h - Test cases based on primality testing.
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

#include "benchmark.h"
#include "count_primes.h"
#include <atomic>
#include <latch>

template <typename TPoolHandle,
          std::unsigned_integral T,
          bool UseSubmit = true>
class IsPrimeTest : public ITestCase<TPoolHandle>
{
public:
    explicit IsPrimeTest(size_t n) : n_(n) 
    {
        std::random_device rd;
        std::mt19937_64 rng(rd());
        data_ = generate_random_array<T>(n, rng);

        for (T v : data_)
        {
            if (is_prime(v)) ++expected_count_;
        }
    }

    std::string name() const override
    {
        return std::format("count_prime({}, {}) [{}]",
            detail::type_name<T>(), n_, 
            UseSubmit ? "submit" : "fire_and_forget"); 
    }

    void run(TPoolHandle& pool) override
    {
        if constexpr (UseSubmit)
        {
            std::vector<std::future<bool>> futs_;
            futs_.reserve(data_.size());
            for (auto n : data_)
            {
                futs_.push_back(pool.submit(&is_prime<T>, n));
            }
            
            size_t cnt = 0;
            for (auto& f : futs_)
            {
                cnt += f.get() ? 1 : 0;
            }

            if (expected_count_ != cnt)
            {
                this->all_correct_ = false;
            }
        }
        else
        {
            std::vector<ShardedCounter> shards(kShardCount);
            std::latch done_latch(data_.size());

            for (auto n : data_)
            {
                pool.fire_and_forget([n, &done_latch, &shards]() {
                    static std::atomic<size_t> global_tls_idx{0};
                    // thread_local, only init once
                    thread_local size_t my_idx = 
                        global_tls_idx.fetch_add(1, std::memory_order_relaxed);
                    if (is_prime<T>(n))
                    {
                        // Use bitwise AND (&) instead of modulo (%)
                        shards[my_idx & kShardMask].val.fetch_add(
                            1, std::memory_order_relaxed);
                    }
                    done_latch.count_down();
                });
            }
            
            done_latch.wait();
            
            size_t total = 0;
            for (auto& s : shards)
            {
                total += s.val.load(std::memory_order_relaxed);
            }
            if (expected_count_ != total)
            {
                this->all_correct_ = false;
            }
        }
    }
private:
    std::vector<T> data_;
    size_t expected_count_ = 0;
    size_t n_ = 0;
    // Round up the number of shards to the next power of two 
    // to allow using bitwise AND (&) instead of modulo (%)
    static inline const size_t kShardCount = []() {
        size_t cores = std::thread::hardware_concurrency();
        if (0 == cores)
        {
            cores = 8;
        }
        size_t power_of_two = 1;
        while (power_of_two < cores)
        {
            power_of_two <<= 1;
        }
        return power_of_two; 
    }();
    static inline const size_t kShardMask = kShardCount - 1;
    // Use a sharded accumulator to reduce lock contention.
    // Align to 64 bytes to avoid false sharing.
    struct alignas(64) ShardedCounter
    {
        std::atomic<size_t> val{0};
    };
};
