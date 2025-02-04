//                  ReactivePlusPlus library
//
//          Copyright Aleksey Loginov 2023 - present.
// Distributed under the Boost Software License, Version 1.0.
//    (See accompanying file LICENSE_1_0.txt or copy at
//          https://www.boost.org/LICENSE_1_0.txt)
//
// Project home: https://github.com/victimsnino/ReactivePlusPlus
//

#pragma once

#include <rpp/schedulers/fwd.hpp>

#include <rpp/schedulers/details/queue.hpp>
#include <rpp/schedulers/details/utils.hpp>
#include <rpp/schedulers/details/worker.hpp>
#include <rpp/utils/functors.hpp>

#include <thread>

namespace rpp::schedulers
{
/**
 * @brief Schedules execution of schedulables via queueing tasks to the caller thread with priority to time_point and order.
 * @warning Caller thread is thread where `schedule` called.
 *
 * @details When this scheduler passed to some operators, then caller thread is thread where scheduling of some action happens. In most cases it is where `on_next` was called.
 *
 * @par Why do we need it?
 * This scheduler used to prevent recursion calls and making planar linear execution of schedulables. For example:
 * \code{.cpp}
 * auto worker = rpp::schedulers::current_thread::create_worker();
 * worker.schedule([&worker](const auto& handler)
 * {
 *     std::cout << "Task 1 starts" << std::endl;
 * 
 *     worker.schedule([&worker](const auto& handler)
 *     {
 *         std::cout << "Task 2 starts" << std::endl;
 *         worker.schedule([](const auto&)
 *         {
 *             std::cout << "Task 4" << std::endl;
 *             return rpp::schedulers::optional_delay_from_now{};
 *         }, handler);
 *         std::cout << "Task 2 ends" << std::endl;
 *         return rpp::schedulers::optional_delay_from_now{};
 *     }, handler);
 * 
 *     worker.schedule([](const auto&)
 *     {
 *         std::cout << "Task 3" << std::endl;
 *         return rpp::schedulers::optional_delay_from_now{};
 *     }, handler);
 * 
 *     std::cout << "Task 1 ends" << std::endl;
 *     return rpp::schedulers::optional_delay_from_now{};
 * }, handler);
 * \endcode
 * Would lead to:
 * - "Task 1 starts"
 * - "Task 1 ends"
 * - "Task 2 starts"
 * - "Task 2 ends"
 * - "Task 3"
 * - "Task 4"
 * 
 * @par How to use it properly?
 * To have any visible impact you need to use it at least **twice** during same observable. For example, `rpp::source::just` source uses it as default scheduler as well as `rpp::operators::merge` operator (which just "owns" it during subscription).
 *
 * For example, this one
 * \code{.cpp}
 * rpp::source::just(1, 2, 3) 
 *  | rpp::operators::merge_with(rpp::source::just(4, 5, 6)) 
 *  | rpp::operators::subscribe([](int v) { std::cout << v << " "; });
 * \endcode
 * Procedes output `1 4 2 5 3 6` due to `merge_with` takes ownership over this scheduler during subscription, both sources schedule their first emissions into scheduler, then `merge_with` frees scheduler and it starts to proceed scheduled actions. As a result it continues interleaving of values. In case of usingg `rpp::schedulers::immediate` it would be:
 * \code{.cpp}
 * rpp::source::just(rpp::schedulers::immediate{}, 1, 2, 3) 
 *  | rpp::operators::merge_with(rpp::source::just(rpp::schedulers::immediate{}, 4, 5, 6)) 
 *  | rpp::operators::subscribe([](int v) { std::cout << v << " "; });
 * \endcode
 * With output `1 2 3 4 5 6`
 *
 * @ingroup schedulers
 */
class current_thread
{
    friend class new_thread;
    class worker_strategy;

    inline static thread_local std::optional<details::schedulables_queue<worker_strategy>> s_queue{};

    struct is_queue_is_empty
    {
        const details::schedulables_queue<worker_strategy>& queue;

        bool operator()() const { return queue.is_empty(); }
    };


    static void drain_current_queue()
    {
        drain_queue(s_queue);
    }

    static void drain_queue(std::optional<details::schedulables_queue<worker_strategy>>& queue)
    {
        while (!queue->is_empty())
        {
            auto top = queue->pop();
            if (top->is_disposed())
                continue;

            std::optional<time_point> timepoint{top->get_timepoint()};
            // immediate like scheduling
            do
            {
                if (timepoint && !top->is_disposed())
                    details::sleep_until(top->get_timepoint());

                if (top->is_disposed())
                    timepoint.reset();
                else
                    timepoint = (*top)();

            } while (queue->is_empty() && timepoint.has_value());

            if (timepoint.has_value())
                queue->emplace(timepoint.value(), std::move(top));
        }

        queue.reset();
    }

    class worker_strategy
    {
    public:
        template<rpp::schedulers::constraint::schedulable_handler Handler, typename... Args, constraint::schedulable_fn<Handler, Args...> Fn>
        static void defer_for(duration duration, Fn&& fn, Handler&& handler, Args&&... args)
        {
            if (handler.is_disposed())
                return;

            auto&      queue              = s_queue;
            const bool someone_owns_queue = queue.has_value();
            std::optional<time_point> timepoint{};
            if (!someone_owns_queue)
            {
                queue.emplace();

                timepoint = details::immediate_scheduling_while_condition<worker_strategy>(duration, is_queue_is_empty{queue.value()}, fn, handler, args...);
                if (!timepoint || handler.is_disposed())
                    return drain_queue(queue);
            }
            else 
            {
                timepoint = now() + duration;
            }

            queue->emplace(timepoint.value(), std::forward<Fn>(fn), std::forward<Handler>(handler), std::forward<Args>(args)...);

            if (!someone_owns_queue)
                drain_queue(queue);
        }

        static constexpr rpp::schedulers::details::none_disposable get_disposable() { return {}; }

        static rpp::schedulers::time_point now() { return details::now(); }
    };

public:
    static rpp::utils::finally_action<void (*)()> own_queue_and_drain_finally_if_not_owned()
    {
        const bool someone_owns_queue = s_queue.has_value();

        if (!someone_owns_queue)
            s_queue.emplace();

        return rpp::utils::finally_action{!someone_owns_queue ? &drain_current_queue : &rpp::utils::empty_function<>};
    }

    static rpp::schedulers::worker<worker_strategy> create_worker()
    {
        return rpp::schedulers::worker<worker_strategy>{};
    }
};
} // namespace rpp::schedulers