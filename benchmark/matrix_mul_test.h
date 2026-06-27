/*
 * matrix_mul_test.h - Test cases based on matrix multiplication.
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
#include <random>
#include <latch>

template <typename T>
concept Arithmetic = std::is_arithmetic_v<T> && !std::is_same_v<T, bool>;

template <typename TPoolHandle, Arithmetic T, bool UseSubmit = true>
class MatrixMulTest : public ITestCase<TPoolHandle>
{
public:
    explicit MatrixMulTest(
        size_t A_rows, size_t A_cols_B_rows, size_t B_cols)
        : A_rows_(A_rows), A_cols_B_rows_(A_cols_B_rows), B_cols_(B_cols),
          A_(A_rows * A_cols_B_rows), B_(A_cols_B_rows * B_cols),
          C_expected_(A_rows * B_cols), C_actual_(A_rows * B_cols)
    {
        if (0 == A_rows || 0 == A_cols_B_rows || 0 == B_cols) {
            throw std::invalid_argument("Matrix dimensions must be positive.");
        }
        generate_random_data();
        compute_serial();
    }

    std::string name() const override
    {
        return std::format("matrix_mul({}, {}, {}, {}) [{}]",
            detail::type_name<T>(),
            A_rows_, A_cols_B_rows_, B_cols_,
            UseSubmit ? "submit" : "fire_and_forget");
    }

    void run(TPoolHandle& pool)
    {
        if constexpr (UseSubmit)
        {
            std::vector<std::future<T>> futs_;
            futs_.reserve(A_rows_ * B_cols_);
            for (size_t i = 0; i < A_rows_; ++i) 
            {
                for (size_t j = 0; j < B_cols_; ++j)
                {
                    futs_.emplace_back(
                        pool.submit([this, i, j]() -> T {
                            T sum = static_cast<T>(0);
                            for (size_t k = 0; k < A_cols_B_rows_; ++k) {
                                sum += A_[i * A_cols_B_rows_ + k] * 
                                       B_[k * B_cols_ + j];
                            }
                            return sum;
                        })
                    );
                }
            }

            for (size_t idx = 0; idx < futs_.size(); ++idx)
            {
                verify_result(idx, futs_[idx].get());
            }
        }
        else
        {
            std::latch done_latch(A_rows_ * B_cols_);
            for (size_t i = 0; i < A_rows_; ++i) 
            {
                for (size_t j = 0; j < B_cols_; ++j)
                {
                    pool.fire_and_forget([this, i, j, &done_latch]() {
                        T sum = static_cast<T>(0);
                        for (size_t k = 0; k < A_cols_B_rows_; ++k) {
                            sum += A_[i * A_cols_B_rows_ + k] * 
                                   B_[k * B_cols_ + j];
                        }
                        C_actual_[i * B_cols_ + j] = sum;
                        done_latch.count_down();
                    });
                }
            }

            done_latch.wait();

            for (size_t idx = 0; idx < C_actual_.size(); ++idx)
            {
                verify_result(idx, C_actual_[idx]);
            }
        }
    }

private:
    inline void verify_result(size_t idx, T ret)
    {
        if constexpr (std::is_floating_point_v<T>)
        {
            if (std::abs(C_expected_[idx] - ret) >
                static_cast<T>(1e-4)) [[unlikely]]
            {
                this->all_correct_ = false;
            }
        }
        else
        {
            if (C_expected_[idx] != ret) [[unlikely]]
            {
                this->all_correct_ = false;
            }
        }
    }

    void generate_random_data()
    {
        std::random_device rd;
        std::mt19937_64 rng(rd());
        if constexpr (std::is_integral_v<T>)
        {
            double max_safe = std::sqrt(static_cast<double>(
                std::numeric_limits<T>::max()) / A_cols_B_rows_);
            T max_val = static_cast<T>(std::min(max_safe, 100.0));
            T min_val = std::is_unsigned_v<T> ? 0 : -max_val;
            using DistT = std::conditional_t<(sizeof(T) < 2u), unsigned short, T>;
            std::uniform_int_distribution<DistT> dist(min_val, max_val);
            std::ranges::generate(A_, [&]{ return static_cast<T>(dist(rng)); });
            std::ranges::generate(B_, [&]{ return static_cast<T>(dist(rng)); });
        }
        else
        {
            std::uniform_real_distribution<T> dist(-10.0, 10.0);
            std::ranges::generate(A_, [&]{ return static_cast<T>(dist(rng)); });
            std::ranges::generate(B_, [&]{ return static_cast<T>(dist(rng)); });
        }
    }

    void compute_serial()
    {
        for (size_t i = 0; i < A_rows_; ++i)
        {
            for (size_t j = 0; j < B_cols_; ++j)
            {
                T sum = static_cast<T>(0);
                for (size_t k = 0; k < A_cols_B_rows_; ++k)
                {
                    sum += A_[i * A_cols_B_rows_ + k] * B_[k * B_cols_ + j];
                }
                C_expected_[i * B_cols_ + j] = sum;
            }
        }
    }

private:
    size_t A_rows_;
    size_t A_cols_B_rows_;
    size_t B_cols_;
    std::vector<T> A_;
    std::vector<T> B_;
    std::vector<T> C_expected_;
    std::vector<T> C_actual_;
};
