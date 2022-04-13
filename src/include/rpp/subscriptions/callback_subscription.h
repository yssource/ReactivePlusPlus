// MIT License
// 
// Copyright (c) 2022 Aleksey Loginov
// 
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in all
// copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

#include <rpp/subscriptions/subscription_base.h>
#include <rpp/subscriptions/details/subscription_state.h>

#include <concepts>
#include <utility>

namespace rpp
{
/**
 * \brief Subscription which invoke callbable during unsubscribe
 */
class callback_subscription final : public subscription_base
{
public:
    template<std::invocable Fn>
    callback_subscription(Fn&& fn)
        : subscription_base{std::make_shared<state<std::decay_t<Fn>>>(std::forward<Fn>(fn))} { }

private:
    template<std::invocable Fn>
    class state final : public details::subscription_state
    {
    public:
        state(const Fn& fn)
            : m_fn{fn} {}

        state(Fn&& fn)
            : m_fn{std::move(fn)} {}

    protected:
        void on_unsubscribe() override
        {
            m_fn();
        }

    private:
        Fn m_fn{};
    };
};
} // namespace rpp