/*
 * stp_inplace_function.h - A fixed-size, move-only inline callable wrapper.
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

#include <functional>
#include <new>

// ============================================================
//  InplaceFunction<Signature, Capacity>
//
//  A fixed-size, move-only callable wrapper.
//  All callables are stored inline; no heap allocation is ever performed.
//  Triggers a compile-time error if the callable exceeds the given Capacity.
//
//  Default Capacity is 56. Combined with the 8-byte vt_ pointer,
//  sizeof(InplaceFunction) = 64 bytes (exactly one cache line).
// ============================================================
namespace STP
{

template <typename Signature, size_t Capacity = 56>
class InplaceFunction;

template <typename R, typename... Args, size_t Capacity>
class InplaceFunction<R(Args...), Capacity>
{
    static constexpr size_t kAlign = alignof(std::max_align_t);

    // Compile-time vtable: 
    //   generated per callable type and placed in .rodata
    struct VTable
    {
        R    (*invoke) (void*, Args...);
        void (*move)   (void*, void*) noexcept; // relocate
        void (*destroy)(void*)        noexcept;
    };

    template <typename F>
    static constexpr VTable vtable_for
    {
        // invoke
        [](void* p, Args... args) -> R {
            return std::invoke(
                *static_cast<F*>(p),
                std::forward<Args>(args)...);
        },
        // move-construct at dst, destroy src (relocate)
        [](void* dst, void* src) noexcept {
            ::new (dst) F(std::move(*static_cast<F*>(src)));
            static_cast<F*>(src)->~F();
        },
        // destroy
        [](void* p) noexcept {
            static_cast<F*>(p)->~F();
        }
    };

    // -- Data layout --
    // Placing buf_ before vt_ yields sizeof = Capacity + 8.
    // When Capacity = 56, sizeof = 64 bytes (one cache line).
    alignas(kAlign) unsigned char buf_[Capacity];
    VTable const* vt_ = nullptr;

    void reset() noexcept
    {
        if (vt_)
        {
            vt_->destroy(buf_);
            vt_ = nullptr;
        }
    }

public:
    InplaceFunction() = default;
    InplaceFunction(std::nullptr_t) noexcept {}

    // -- Construct from callable --
    template <typename F>
        requires (!std::same_as<std::decay_t<F>, InplaceFunction>
                  && std::is_invocable_r_v<R, std::decay_t<F>&, Args...>)
    InplaceFunction(F&& f)
        noexcept(std::is_nothrow_constructible_v<std::decay_t<F>, F>)
    {
        using Fn = std::decay_t<F>;
        static_assert(sizeof(Fn) <= Capacity,
            "Callable exceeds InplaceFunction buffer capacity. "
            "Increase the Capacity template parameter.");
        static_assert(alignof(Fn) <= kAlign,
            "Callable alignment exceeds InplaceFunction buffer alignment.");
        static_assert(std::is_nothrow_move_constructible_v<Fn>,
            "Callable must be nothrow move constructible "
            "(required for InplaceFunction's noexcept move).");

        ::new (static_cast<void*>(buf_)) Fn(std::forward<F>(f));
        vt_ = &vtable_for<Fn>;
    }

    // -- Move construction / assignment --
    InplaceFunction(InplaceFunction&& o) noexcept : vt_(o.vt_)
    {
        if (vt_)
        {
            vt_->move(buf_, o.buf_);
            o.vt_ = nullptr;
        }
    }

    InplaceFunction& operator=(InplaceFunction&& o) noexcept
    {
        if (this != &o) {
            reset();
            vt_ = o.vt_;
            if (vt_) {
                vt_->move(buf_, o.buf_);
                o.vt_ = nullptr;
            }
        }
        return *this;
    }

    InplaceFunction& operator=(std::nullptr_t) noexcept
    {
        reset();
        return *this;
    }

    ~InplaceFunction() { reset(); }

    // -- Non-copyable --
    InplaceFunction(const InplaceFunction&)            = delete;
    InplaceFunction& operator=(const InplaceFunction&) = delete;

    // -- Invocation --
    [[nodiscard]] R operator()(Args... args)
    {
        // UB on empty call, but never happen
        return vt_->invoke(buf_, std::forward<Args>(args)...);
    }

    // -- Validity check --
    explicit operator bool() const noexcept { return vt_ != nullptr; }
};

} // namespace ST
