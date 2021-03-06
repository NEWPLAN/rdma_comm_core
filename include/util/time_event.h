
#ifndef __NEWPLAN_TIME_EVENT_H__
#define __NEWPLAN_TIME_EVENT_H__

#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>

#include <cassert>
#include <chrono>
#include <functional>
#include <iterator>
#include <memory>
#include <set>
#include <thread>

namespace newplan
{
    class ManualEvent
    {
    public:
        explicit ManualEvent(bool signaled = false) :
            m_signaled(signaled)
        {
        }

        void signal()
        {
            {
                std::unique_lock lock(m_mutex);
                m_signaled = true;
            }
            m_cv.notify_all();
        }

        void cancel()
        {
            {
                std::unique_lock lock(m_mutex);
                m_signaled = true;
            }
            m_cv.notify_all();
        }

        void wait()
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [&]() { return m_signaled != false; });
        }

        template <typename Rep, typename Period>
        bool wait_for(const std::chrono::duration<Rep, Period> &t)
        {
            std::unique_lock lock(m_mutex);
            return m_cv.wait_for(lock, t, [&]() { return m_signaled != false; });
        }

        template <typename Clock, typename Duration>
        bool wait_until(const std::chrono::time_point<Clock, Duration> &t)
        {
            std::unique_lock lock(m_mutex);
            return m_cv.wait_until(lock, t, [&]() { return m_signaled != false; });
        }

        void reset()
        {
            std::unique_lock lock(m_mutex);
            m_signaled = false;
        }

    private:
        bool m_signaled = false;
        std::mutex m_mutex;
        std::condition_variable m_cv;
    };

    class AutoEvent
    {
    public:
        explicit AutoEvent(bool signaled = false) :
            m_signaled(signaled)
        {
        }

        void signal()
        {
            {
                std::unique_lock lock(m_mutex);
                m_signaled = true;
            }
            m_cv.notify_one();
        }

        void cancel()
        {
            {
                std::unique_lock lock(m_mutex);
                m_signaled = true;
            }
            m_cv.notify_one();
        }

        void wait()
        {
            std::unique_lock lock(m_mutex);
            m_cv.wait(lock, [&]() { return m_signaled != false; });
            m_signaled = false;
        }

        template <typename Rep, typename Period>
        bool wait_for(const std::chrono::duration<Rep, Period> &t)
        {
            std::unique_lock lock(m_mutex);
            bool result = m_cv.wait_for(lock, t, [&]() { return m_signaled != false; });
            if (result)
                m_signaled = false;
            return result;
        }

        template <typename Clock, typename Duration>
        bool wait_until(const std::chrono::time_point<Clock, Duration> &t)
        {
            std::unique_lock lock(m_mutex);
            bool result = m_cv.wait_until(lock, t, [&]() { return m_signaled != false; });
            if (result)
                m_signaled = false;
            return result;
        }

    private:
        bool m_signaled = false;
        std::mutex m_mutex;
        std::condition_variable m_cv;
    };

    class TimeEvent
    {
    public:
        template <typename T>
        TimeEvent(T &&tick) :
            m_tick(std::chrono::duration_cast<std::chrono::nanoseconds>(tick)), m_thread([this]() {
                assert(m_tick.count() > 0);
                auto start = std::chrono::high_resolution_clock::now();
                std::chrono::nanoseconds drift{0};
                while (!m_event.wait_for(m_tick - drift))
                {
                    ++m_ticks;
                    auto it = std::begin(m_events);
                    auto end = std::end(m_events);
                    while (it != end)
                    {
                        auto &event = *it;
                        ++event.elapsed;
                        if (event.elapsed == event.ticks)
                        {
                            auto remove = event.proc();
                            if (remove)
                            {
                                m_events.erase(it++);
                                continue;
                            }
                            else
                            {
                                event.elapsed = 0;
                            }
                        }
                        ++it;
                    }
                    auto now = std::chrono::high_resolution_clock::now();
                    auto realDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start);
                    auto fakeDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(m_tick * m_ticks);
                    drift = realDuration - fakeDuration;
                }
            })
        {
        }

        ~TimeEvent()
        {
            m_event.signal();
            m_thread.join();
        }

        template <typename T, typename F, typename... Args>
        auto set_timeout(T &&timeout, F f, Args &&...args)
        {
            assert(std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count() >= m_tick.count());
            auto event = std::make_shared<ManualEvent>();
            auto proc = [=]() {
                if (event->wait_for(std::chrono::seconds(0)))
                    return true;
                f(args...);
                return true;
            };
            m_events.insert({event_ctx::kNextSeqNum++, proc,
                             static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(timeout).count() / m_tick.count()), 0, event});
            return event;
        }

        template <typename T, typename F, typename... Args>
        auto set_interval(T &&interval, F f, Args &&...args)
        {
            assert(std::chrono::duration_cast<std::chrono::nanoseconds>(interval).count() >= m_tick.count());
            auto event = std::make_shared<ManualEvent>();
            auto proc = [=]() {
                if (event->wait_for(std::chrono::seconds(0)))
                    return true;
                f(args...);
                return false;
            };
            m_events.insert({event_ctx::kNextSeqNum++, proc,
                             static_cast<unsigned long long>(std::chrono::duration_cast<std::chrono::nanoseconds>(interval).count() / m_tick.count()), 0, event});
            return event;
        }

    private:
        std::chrono::nanoseconds m_tick;
        unsigned long long m_ticks = 0;
        ManualEvent m_event;
        std::thread m_thread;

        struct event_ctx
        {
            bool operator<(const event_ctx &rhs) const
            {
                return seq_num < rhs.seq_num;
            }
            static inline unsigned long long kNextSeqNum = 0;
            unsigned long long seq_num;
            std::function<bool(void)> proc;
            unsigned long long ticks;
            mutable unsigned long long elapsed;
            std::shared_ptr<ManualEvent> event;
        };

        using set = std::set<event_ctx>;
        set m_events;
    };
} // namespace newplan
#endif