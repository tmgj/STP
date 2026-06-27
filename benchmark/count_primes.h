/*
 * count_primes.h - Implementation of the Miller-Rabin primality test.
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
#include <array>
#include <vector>
#include <random>
#include <algorithm>

template <std::unsigned_integral T>
[[nodiscard]] T mulmod(T a, T b, T m) noexcept
{
    if constexpr (sizeof(T) <= 4)
    {
        return static_cast<T>(static_cast<uint64_t>(a) * b % m);
    }
    else
    {
        return static_cast<T>(static_cast<__uint128_t>(a) * b % m);
    }
}

template <std::unsigned_integral T>
[[nodiscard]] T powmod(T base, T exp, T m) noexcept
{
    if (m <= T(1))
    {
        return exp == T(0) ? T(1) : T(0);
    }
    T result{1};
    base %= m;
    for (; exp; exp >>= 1)
    {
        if (exp & 1)
        {
            result = mulmod(result, base, m);
        }
        base = mulmod(base, base, m);
    }
    return result;
}

// Miller-Rabin primality test, return true is n is a prime
template <std::unsigned_integral T>
[[nodiscard]] bool miller_rabin_test(T n, T a) noexcept
{
    T d = static_cast<T>(n - 1);
    int r{0};
    while (!(d & 1))
    {
        d >>= 1;
        ++r;
    }

    T x = powmod(a, d, n);
    if (x == 1 || x == n - 1)
    {
        return true;
    }

    while (--r)
    {
        x = mulmod(x, x, n);
        if (x == n - 1)
            return true;
    }
    return false;
}

template <std::unsigned_integral T>
[[nodiscard]] bool is_prime(T n) noexcept
{
    if (n < 2)
    {
        return false;
    }
    if (2 == n)
    {
        return true;
    }
    if (0 == (n & 1))
    {
        return false;
    }

    static constexpr std::array<uint64_t, 12> witnesses{
        {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37}};
    for (const uint64_t w : witnesses)
    {
        if (n == w)
        {
            return true;
        }
        if (0 == n % w)
        {
            return false;
        }
    }

    for (const uint64_t w : witnesses)
    {
        if (!miller_rabin_test(n, static_cast<T>(w)))
        {
            return false;
        }
    }
    return true;
}

template <std::unsigned_integral T>
[[nodiscard]] std::vector<T> generate_random_array(
    std::size_t n, std::mt19937_64& rng)
{
    using DistT = std::conditional_t<(sizeof(T) < 2u), unsigned short, T>;
    std::uniform_int_distribution<DistT> dist{
        static_cast<DistT>(std::numeric_limits<T>::min()),
        static_cast<DistT>(std::numeric_limits<T>::max())};
    std::vector<T> v(n);
    std::ranges::generate(v, [&]{ return static_cast<T>(dist(rng)); });
    return v;
}
