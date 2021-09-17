#ifndef __NEWPLAN_TIMER_H__
#define __NEWPLAN_TIMER_H__
#include <chrono>
#include <iostream>

namespace newplan
{
    class Timer
    {
    public:
        Timer() :
            initted_(false),
            running_(false),
            has_run_at_least_once_(false)
        {
            Init();
        }
        ~Timer()
        {
        }
        void Start()
        {
            if (!running())
            {
                start_cpu_ = std::chrono::steady_clock::now();
                //gettimeofday(&t_start_, NULL);
                running_ = true;
                has_run_at_least_once_ = true;
            }
        }
        void Stop()
        {
            if (running())
            {
                stop_cpu_ = std::chrono::steady_clock::now();
                //gettimeofday(&t_stop_, NULL);
                running_ = false;
            }
        }
        uint64_t MilliSeconds()
        {
            if (!has_run_at_least_once())
            {
                std::cout << "Timer has never been run before reading time." << std::endl;
                return 0;
            }
            if (running())
            {
                Stop();
            }

            elapsed_milliseconds_ = std::chrono::duration_cast<std::chrono::milliseconds>(stop_cpu_ - start_cpu_).count();
            return elapsed_milliseconds_;
        }
        uint64_t MicroSeconds()
        {
            if (!has_run_at_least_once())
            {
                std::cout << "Timer has never been run before reading time." << std::endl;
                return 0;
            }
            if (running())
            {
                Stop();
            }
            //std::cout << t_stop_.tv_sec - t_start_.tv_sec + t_stop_.tv_usec - t_start_.tv_usec << " us[sys], ";

            elapsed_microseconds_ = std::chrono::duration_cast<std::chrono::microseconds>(stop_cpu_ - start_cpu_).count();
            return elapsed_microseconds_;
        }
        uint64_t NanoSeconds()
        {
            if (!has_run_at_least_once())
            {
                std::cout << "Timer has never been run before reading time." << std::endl;
                return 0;
            }
            if (running())
            {
                Stop();
            }

            elasped_nanoseconds_ = std::chrono::duration_cast<std::chrono::nanoseconds>(stop_cpu_ - start_cpu_).count();
            return elasped_nanoseconds_;
        }
        uint64_t Seconds()
        {
            if (!has_run_at_least_once())
            {
                std::cout << "Timer has never been run before reading time." << std::endl;
                return 0;
            }
            if (running())
            {
                Stop();
            }

            elasped_seconds_ = std::chrono::duration_cast<std::chrono::seconds>(stop_cpu_ - start_cpu_).count();
            return elasped_seconds_;
        }

        inline bool initted()
        {
            return initted_;
        }
        inline bool running()
        {
            return running_;
        }
        inline bool has_run_at_least_once()
        {
            return has_run_at_least_once_;
        }

    protected:
        void Init()
        {
            if (!initted())
            {
                initted_ = true;
            }
        }

        bool initted_;
        bool running_;
        bool has_run_at_least_once_;

        std::chrono::steady_clock::time_point start_cpu_;
        std::chrono::steady_clock::time_point stop_cpu_;
        //struct timeval t_start_, t_stop_;
        uint64_t elapsed_milliseconds_;
        uint64_t elapsed_microseconds_;
        uint64_t elasped_nanoseconds_;
        uint64_t elasped_seconds_;
    };

} // namespace newplan
#endif