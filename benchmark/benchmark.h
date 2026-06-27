/*
 * benchmark.h - A general-purpose thread pool benchmarking framework.
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

#include <concepts>
#include <future>
#include <memory>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>
#include <iostream>
#include <numeric>
#include <iomanip>
#include <cmath>
#include <functional>

namespace detail
{
template <typename T>
requires std::integral<T> ||
         std::unsigned_integral<T> ||
         std::is_same_v<T, double>
constexpr std::string_view type_name() noexcept
{
    // integral
    if constexpr (std::is_same_v<T, std::int8_t>)    return "int8_t";
    if constexpr (std::is_same_v<T, std::int16_t>)   return "int16_t";
    if constexpr (std::is_same_v<T, std::int32_t>)   return "int32_t";
    if constexpr (std::is_same_v<T, std::int64_t>)   return "int64_t";
    if constexpr (std::is_same_v<T, long long>)      return "long long";
    if constexpr (std::is_same_v<T, long>)           return "long";
    if constexpr (std::is_same_v<T, int>)            return "int";
    if constexpr (std::is_same_v<T, short>)          return "short";
    if constexpr (std::is_same_v<T, char>)           return "char";
    // unsigned integral
    if constexpr (std::is_same_v<T, std::uint8_t>)   return "uint8_t";
    if constexpr (std::is_same_v<T, std::uint16_t>)  return "uint16_t";
    if constexpr (std::is_same_v<T, std::uint32_t>)  return "uint32_t";
    if constexpr (std::is_same_v<T, std::uint64_t>)  return "uint64_t";
    if constexpr (std::is_same_v<T, unsigned long long>)
        return "unsigned long long";
    if constexpr (std::is_same_v<T, unsigned long>)  return "unsigned long";
    if constexpr (std::is_same_v<T, unsigned int>)   return "unsigned int";
    if constexpr (std::is_same_v<T, unsigned short>) return "unsigned short";
    if constexpr (std::is_same_v<T, unsigned char>)  return "unsigned char";
    // double
    if constexpr (std::is_same_v<T, double>)         return "double";
    // others
    return "unknown type";
}
} // namespace detail

// =============================================================
//  Layer 1 - UnifiedThreadPool<Pool>
//  Unifies push/enqueue/submit_task into `submit`.
//  Unifies execute/enqueue_detach/detach_task into `fire_and_forget`.
// =============================================================
template <typename Pool>
class UnifiedThreadPool
{
public:
    explicit UnifiedThreadPool(Pool& p)
        : pool_(&p) {}

    // submit --> std::future<R>
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
    {
        // STP::ThreadPool --> push(f, args...)
        if constexpr (requires {
            pool_->push(std::forward<F>(f), std::forward<Args>(args)...);
        })
        {
            return pool_->push(
                std::forward<F>(f), std::forward<Args>(args)...);
        }
        // dp::thread_pool & riften::Thiefpool --> enqueue(f, args...)
        else if constexpr (requires {
            pool_->enqueue(std::forward<F>(f), std::forward<Args>(args)...);
        })
        {
            return pool_->enqueue(
                std::forward<F>(f), std::forward<Args>(args)...);
        }
        // task_thread_pool::task_thread_pool --> submit(f, args...)
        else if constexpr (requires {
            pool_->submit(std::forward<F>(f), std::forward<Args>(args)...);
        })
        {
            return pool_->submit(
                std::forward<F>(f), std::forward<Args>(args)...);
        }
        // BS_thread_pool --> submit_task(callable)
        else if constexpr (requires {
            pool_->submit_task([]{});
        })
        {
            return pool_->submit_task(
                [f        = std::forward<F>(f),
                 ...bound = std::forward<Args>(args)]() mutable
                    -> std::invoke_result_t<F, Args...>
                {
                    return std::invoke(std::move(f), std::move(bound)...);
                });
        }
        else
        {
            static_assert(false,
                "[UnifiedThreadPool::submit] "
                "Pool is missing push() / enqueue() / submit_task() APIs");
        }
    }

    // fire_and_forget --> void
    template <typename F, typename... Args>
        requires std::invocable<F, Args...>
    void fire_and_forget(F&& f, Args&&... args)
    {
        // ST::ThreadPool --> execute(f, args...)
        if constexpr (requires {
            pool_->execute(std::forward<F>(f), std::forward<Args>(args)...);
        })
        {
            pool_->execute(
                std::forward<F>(f), std::forward<Args>(args)...);
        }
        // dp::thread_pool & riften::Thiefpool --> enqueue_detach(f, args...)
        else if constexpr (requires {
            pool_->enqueue_detach(
                std::forward<F>(f), std::forward<Args>(args)...);
        })
        {
            pool_->enqueue_detach(
                std::forward<F>(f), std::forward<Args>(args)...);
        }
        // task_thread_pool::task_thread_pool --> submit_detach(f, args...)
        else if constexpr (requires {
            pool_->submit_detach(
                std::forward<F>(f), std::forward<Args>(args)...);
        })
        {
            pool_->submit_detach(
                std::forward<F>(f), std::forward<Args>(args)...);
        }
        // BS_thread_pool --> detach_task(callable)
        else if constexpr (requires {
            pool_->detach_task([]{});
        })
        {
            pool_->detach_task(
                [f        = std::forward<F>(f),
                 ...bound = std::forward<Args>(args)]() mutable
                {
                    std::invoke(std::move(f), std::move(bound)...);
                });
        }
        else
        {
            static_assert(false,
                "[UnifiedThreadPool::fire_and_forget] "
                "Pool is missing execute() / enqueue_detach() / "
                "detach_task() APIs");
        }
    }

private:
    Pool* pool_;   // non-owning
};

template <typename Pool>
UnifiedThreadPool(Pool&) -> UnifiedThreadPool<Pool>;

// =============================================================
//  Layer 2 - PolymorphicPool<Pools...>
//  Unified wrapper and dynamic task dispatch for heterogeneous 
//  thread pools (via std::variant + std::visit).
// =============================================================
template <typename... Pools>
class PolymorphicPool
{
public:
    template <typename Pool>
        requires (std::same_as<Pool, Pools> || ...)
    explicit PolymorphicPool(Pool& pool, std::string name) noexcept
        : impl_(std::in_place_type<UnifiedThreadPool<Pool>>, pool),
          pool_name_(std::move(name))
    {}

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
    {
        return std::visit(
            [&](auto& unified) {
                return unified.submit(
                    std::forward<F>(f), std::forward<Args>(args)...);
            },
            impl_
        );
    }

    template <typename F, typename... Args>
        requires std::invocable<F, Args...>
    void fire_and_forget(F&& f, Args&&... args)
    {
        std::visit(
            [&](auto& unified) {
                unified.fire_and_forget(
                    std::forward<F>(f), std::forward<Args>(args)...);
            },
            impl_
        );
    }

    const std::string& name() const
    {
        return pool_name_;
    }

private:
    std::variant<UnifiedThreadPool<Pools>...> impl_;
    std::string pool_name_;
};

// =============================================================
//  Layer 3 - ITestCase<PoolHandle>
//  Abstract base class for all test cases.
// =============================================================
template <typename PoolHandle>
struct ITestCase
{
    virtual ~ITestCase() = default;
    virtual void run(PoolHandle& pool)  = 0;
    virtual std::string name() const    = 0;
    virtual void reset_correct() { all_correct_ = true; }
    virtual bool correct() const { return all_correct_; }
protected:
    bool all_correct_ = true;
};

// =============================================================
//  Layer 4 - BenchmarkRunner<PoolHandle>
//  Manages the pool and test case collections, executing them 
//  via Cartesian product scheduling.
// =============================================================
template <typename PoolHandle>
class BenchmarkEngine
{
public:
    using TestPtr = std::unique_ptr<ITestCase<PoolHandle>>;

    BenchmarkEngine(size_t n_warmup, size_t n_measure)
        : warmup_rounds_(n_warmup), measure_rounds_(n_measure) {}

    void add_pool(PoolHandle pool) { pools_.emplace_back(std::move(pool)); }
    void add_case(TestPtr   tc)   { cases_.emplace_back(std::move(tc));   }

    void run_all()
    {
        for (auto& tc : cases_) 
        {
            std::cout << std::format("\n  >  {}\n", tc->name());
            print_column_name();
            for (auto& pool : pools_)
            {
                for (size_t i = 0; i < warmup_rounds_; ++i)
                {
                    tc->run(pool);
                }
                tc->reset_correct();
                run_one(tc, pool);
            }
        }
    }

private:
    struct Statistics
    {
        double mean   = 0.0;
        double stddev = 0.0;
        double minv   = 0.0;
        double maxv   = 0.0;
        double p99    = 0.0;
    };

private:
    [[nodiscard]] static Statistics compute_statistics(
        std::vector<double>& times)
    {
        if (times.empty()) return {};
        std::sort(times.begin(), times.end());

        const size_t n = times.size();
        const double mean = std::reduce(times.begin(), times.end()) / n;
        const double variance  = std::transform_reduce(
            times.begin(), times.end(), 0.0, std::plus<>{},
            [mean](double x){ return (x - mean) * (x - mean); }) / n;
        const double stddev = (n > 1) ? std::sqrt(variance / (n - 1)) : 0.0;
        const size_t p99_idx = std::min(
            static_cast<size_t>(std::ceil(0.99 * n)) - 1, n - 1);
    
        return {mean, stddev, times.front(), times.back(), times[p99_idx]};
    }

    void run_one(TestPtr& testcase, PoolHandle& pool)
    {
        std::vector<double> times;
        times.reserve(measure_rounds_);
        for (size_t i = 0; i < measure_rounds_; ++i)
        {
            auto t0 = std::chrono::steady_clock::now();
            testcase->run(pool);
            auto t1 = std::chrono::steady_clock::now();
            double ms = 
                std::chrono::duration<double, std::milli>(t1 - t0).count();
            times.push_back(ms);
        }
        Statistics stats = compute_statistics(times);

        std::cout << std::left  << std::setw(kPoolW) << ("  " + pool.name())
                  << std::right << std::fixed << std::setprecision(3)
                  << std::setw(kNumW) << stats.mean
                  << std::setw(kNumW) << stats.stddev
                  << std::setw(kNumW) << stats.minv
                  << std::setw(kNumW) << stats.maxv
                  << std::setw(kNumW) << stats.p99
                  << (testcase->correct() ? "    SUCC" : "    FAIL") << '\n';
    }

    static void print_column_name()
    {
        std::cout << std::left  << std::setw(kPoolW) << "  Pool"
                  << std::right
                  << std::setw(kNumW) << "mean(ms)"
                  << std::setw(kNumW) << "std_dev(ms)"
                  << std::setw(kNumW) << "min(ms)"
                  << std::setw(kNumW) << "max(ms)"
                  << std::setw(kNumW) << "p99(ms)"
                  << "   status\n";
        std::cout << "  " << std::string(kTotalW - 2, '-') << '\n';
    }

private:
    std::vector<PoolHandle> pools_;     // pool_array
    std::vector<TestPtr>    cases_;     // case_array
    size_t warmup_rounds_         = 0;
    size_t measure_rounds_        = 0;
    static constexpr int kPoolW   = 36; // Width of the pool name column
    static constexpr int kNumW    = 13; // Width of each metric column
    // Number of metric columns (mean/std_dev/min/max/p99)
    static constexpr int kNumCols = 5;
    static constexpr int kTotalW  = kPoolW + kNumW * kNumCols + 9; // +status
};
