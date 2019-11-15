/*
 * Copyright © 2016 Canonical Ltd.
 *
 * This program is free software: you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 or 3,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Authored by: Christopher James Halse Rogers <christopher.halse.rogers@canonical.com>
 */

#ifndef MIR_OBSERVER_MULTIPLEXER_H_
#define MIR_OBSERVER_MULTIPLEXER_H_

#include "mir/observer_registrar.h"
#include "mir/raii.h"
#include "mir/posix_rw_mutex.h"
#include "mir/executor.h"
#include "mir/main_loop.h"

#include <vector>
#include <algorithm>
#include <mutex>
#include <thread>
#include <shared_mutex>

namespace mir
{
template<class Observer>
class ObserverMultiplexer : public ObserverRegistrar<Observer>, public Observer
{
public:
    void register_interest(std::weak_ptr<Observer> const& observer) override;
    void register_interest(
        std::weak_ptr<Observer> const& observer,
        Executor& executor) override;
    void unregister_interest(Observer const& observer) override;

protected:
    /**
     * \param [in] default_executor Executor that will be used as the execution environment
     *                                  for any observer that does not specify its own.
     * \note \p default_executor must outlive any observer.
     */
    explicit ObserverMultiplexer(Executor& default_executor)
        : default_executor{default_executor}
    {
    }

    /**
     *  Invoke a member function of Observer on each registered observer.
     *
     * \tparam MemberFn Must be (Observer::*)(Args...)
     * \tparam Args     Parameter pack of arguments of Observer member function.
     * \param f         Pointer to Observer member function to invoke.
     * \param args  Arguments for member function invocation.
     */
    template<typename MemberFn, typename... Args>
    void for_each_observer(MemberFn f, Args&&... args);
private:
    Executor& default_executor;

    class WeakObserver
    {
    public:
        explicit WeakObserver(std::weak_ptr<Observer> observer)
            : observer{observer}
        {
        }

        class LockedObserver
        {
        public:
            LockedObserver(LockedObserver const&) = delete;
            LockedObserver(LockedObserver&&) = default;
            LockedObserver& operator=(LockedObserver const&) = delete;
            LockedObserver& operator=(LockedObserver&&) = default;

            ~LockedObserver() = default;

            Observer& operator*()
            {
                return *observer;
            }
            Observer* operator->()
            {
                return observer.get();
            }
            Observer* get()
            {
                return observer.get();
            }

            operator bool() const
            {
                return static_cast<bool>(observer);
            }
        private:
            friend class WeakObserver;
            LockedObserver(std::shared_ptr<Observer> observer, std::unique_lock<std::recursive_mutex>&& lock)
                : lock{std::move(lock)},
                  observer{std::move(observer)}
            {
            }

            std::unique_lock<std::recursive_mutex> lock;
            std::shared_ptr<Observer> observer;
        };

        LockedObserver lock()
        {
            auto live_observer = observer.lock();
            if (live_observer)
            {
                return LockedObserver{live_observer, std::unique_lock<std::recursive_mutex>(mutex)};
            }
            else
            {
                return LockedObserver{nullptr, {}};
            }
        }

        void reset()
        {
            std::lock_guard<std::recursive_mutex> lock{mutex};
            observer.reset();
        }

        bool operator==(Observer const* const other) const
        {
            return observer.lock().get() == other;
        }
        bool operator==(std::nullptr_t) const
        {
            return observer.expired();
        }
    private:
        // A recursive_mutex so that we can reset the observer pointer from a call to
        // a LockedObserver.
        std::recursive_mutex mutex;
        std::weak_ptr<Observer> observer;
    };

    PosixRWMutex observer_mutex;
    std::vector<std::pair<Executor&, std::shared_ptr<WeakObserver>>> observers;
};

template<class Observer>
void ObserverMultiplexer<Observer>::register_interest(std::weak_ptr<Observer> const& observer)
{
    register_interest(observer, default_executor);
}

template<class Observer>
void ObserverMultiplexer<Observer>::register_interest(
    std::weak_ptr<Observer> const& observer,
    Executor& executor)
{
    std::lock_guard<decltype(observer_mutex)> lock{observer_mutex};

    observers.emplace_back(std::make_pair(std::ref(executor), std::make_shared<WeakObserver>(observer)));
}

template<class Observer>
void ObserverMultiplexer<Observer>::unregister_interest(Observer const& observer)
{
    std::lock_guard<decltype(observer_mutex)> lock{observer_mutex};
    observers.erase(
        std::remove_if(
            observers.begin(),
            observers.end(),
            [&observer](auto& candidate)
            {
                if (*candidate.second == &observer)
                {
                    // Kill the observer; this will wait for any (other) thread
                    // to finish with the observer first.
                    candidate.second->reset();
                    return true;
                }
                // We also might as well clean up any expired observers while we're here.
                return candidate.second == nullptr;
            }),
        observers.end());
}

template<class Observer>
template<typename MemberFn, typename... Args>
void ObserverMultiplexer<Observer>::for_each_observer(MemberFn f, Args&&... args)
{
    static_assert(
        std::is_member_function_pointer<MemberFn>::value,
        "f must be of type (Observer::*)(Args...), a pointer to an Observer member function.");
    auto const invokable_mem_fn = std::mem_fn(f);
    std::shared_lock<decltype(observer_mutex)> lock{observer_mutex};
    for (auto& observer_pair: observers)
    {
        observer_pair.first.spawn(
            [invokable_mem_fn, weak_observer = observer_pair.second, args...]() mutable
            {
                if (auto observer = weak_observer->lock())
                {
                    invokable_mem_fn(observer, std::forward<Args>(args)...);
                }
            });
    }
}
}


#endif //MIR_OBSERVER_MULTIPLEXER_H_
