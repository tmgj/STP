/*
 * bench.cpp - The entry point for all test cases.
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

#include "count_primes_test.h"
#include "matrix_mul_test.h"
#include "stp_thread_pool.h"
#include "thread_pool/thread_pool.h"
#include "BS_thread_pool.hpp"
#include "riften/thiefpool.hpp"
#include "task_thread_pool.hpp"

using PoolHandle = PolymorphicPool<STP::ThreadPool,
                                   dp::thread_pool<>,
                                   BS::thread_pool<>,
                                   riften::Thiefpool,
                                   task_thread_pool::task_thread_pool>;
using TestCase   = ITestCase<PoolHandle>;
using Engin      = BenchmarkEngine<PoolHandle>;

int main()
{
    const unsigned int thread_num = std::thread::hardware_concurrency();
    STP::ThreadPool st_pool(thread_num);
    dp::thread_pool dp_pool(thread_num);
    BS::thread_pool bs_pool(thread_num);
    riften::Thiefpool riften_pool(thread_num);
    task_thread_pool::task_thread_pool tt_pool(thread_num);

    Engin runner(20, 200);

    runner.add_pool(PoolHandle(st_pool, "STP::ThreadPool"));
    runner.add_pool(PoolHandle(dp_pool, "dp::thread_pool"));
    runner.add_pool(PoolHandle(bs_pool, "BS_thread_pool"));
    runner.add_pool(PoolHandle(riften_pool, "riften::Thiefpool"));
    runner.add_pool(PoolHandle(tt_pool, "task_thread_pool::task_thread_pool"));

    // --- IsPrimeTest ---
    // test submit interface
    runner.add_case(std::make_unique<IsPrimeTest<PoolHandle, uint16_t, true>>(100000));
    runner.add_case(std::make_unique<IsPrimeTest<PoolHandle, uint32_t, true>>(50000));
    runner.add_case(std::make_unique<IsPrimeTest<PoolHandle, uint64_t, true>>(10000));
    // test fire_and_forget interface
    runner.add_case(std::make_unique<IsPrimeTest<PoolHandle, uint16_t, false>>(100000));
    runner.add_case(std::make_unique<IsPrimeTest<PoolHandle, uint32_t, false>>(50000));
    runner.add_case(std::make_unique<IsPrimeTest<PoolHandle, uint64_t, false>>(10000));

    // --- MatrixMulTest ---
    // test submit interface
    runner.add_case(std::make_unique<MatrixMulTest<PoolHandle, int16_t, true>>(100, 64, 100));
    runner.add_case(std::make_unique<MatrixMulTest<PoolHandle, int32_t, true>>(100, 64, 100));
    runner.add_case(std::make_unique<MatrixMulTest<PoolHandle, int64_t, true>>(64, 32, 128));
    runner.add_case(std::make_unique<MatrixMulTest<PoolHandle, double, true>>(64, 32, 128));
    // test fire_and_forget interface
    runner.add_case(std::make_unique<MatrixMulTest<PoolHandle, int16_t, false>>(100, 64, 100));
    runner.add_case(std::make_unique<MatrixMulTest<PoolHandle, int32_t, false>>(100, 64, 100));
    runner.add_case(std::make_unique<MatrixMulTest<PoolHandle, int64_t, false>>(64, 32, 128));
    runner.add_case(std::make_unique<MatrixMulTest<PoolHandle, double, false>>(64, 32, 128));

    runner.run_all();

    return 0;
}
