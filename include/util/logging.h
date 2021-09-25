#ifndef __NEWPLAN_WHEEL_LOG_H__
#define __NEWPLAN_WHEEL_LOG_H__

#include <stdarg.h>
#include <stdint.h>

#include <iostream>
#include <random>
#include <string>
#include <memory>
#include <unistd.h>
#include <sys/syscall.h>
#include <iostream>

class RLOG
{
public:
    // explicit RLOG()
    // {
    //     set_glog();
    // }
    virtual ~RLOG()
    {
    }

protected:
    explicit RLOG()
    {
        set_glog();
    }

private:
    int set_glog(void)
    {
        if (this->registerted)
            return 1;
        this->registerted = true;

        char *LOG_LEVEL = getenv("RCL_MAX_VLOG_LEVEL");

        if (LOG_LEVEL != NULL && LOG_LEVEL != nullptr)
        {
            if (setenv("GLOG_v", LOG_LEVEL, 1) != 0)
            {
                printf("Error of setting env!\n");
                exit(0);
            }
            if (setenv("GLOG_logtostderr", "1", 1) != 0)
            {
                printf("Error of setting env!\n");
                exit(0);
            }
            if (setenv("GLOG_colorlogtostderr", "1", 1) != 0)
            {
                printf("Error of setting env!\n");
                exit(0);
            }

            log_level = atoi(LOG_LEVEL);
        }
        printf("RCL_MAX_VLOG_LEVEL is set to %d, "
               "indicating level in VLOG (level) less than %d would show\n",
               log_level, log_level);

        return 0;
    }

public:
    // static int log_test(void)
    // {
    //     LOG(INFO) << "Hello LOG(INFO)";
    //     LOG(WARNING) << "Hello LOG(WARNING)";
    //     LOG(ERROR) << "Hello LOG(ERROR)";
    //     VLOG(0) << "Hello VLOG(0)";
    //     VLOG(1) << "Hello VLOG(1)";
    //     VLOG(2) << "Hello VLOG(2)";
    //     VLOG(3) << "Hello VLOG(3)";
    //     VLOG(10) << "Hello VLOG(10)";
    //     VLOG(100) << "Hello VLOG(100)";
    //     return 0;
    // }
    static RLOG *register_function()
    {
        static RLOG mylog;
        return &mylog;
    }
    static std::string make_string(const char *fmt, ...)
    {
        char *sz;
        va_list ap;
        va_start(ap, fmt);
        if (vasprintf(&sz, fmt, ap) == -1)
            throw std::runtime_error("memory allocation failed\n");
        va_end(ap);
        std::string str(sz);
        free(sz);
        return str;
    }

private:
    int log_level = 0;
    bool registerted = false;
};

//RLOG f;
#define REGISTER_LOG_LEVEL(name) \
    static RLOG *mylog_##__COUNTER__##name = RLOG::register_function();

REGISTER_LOG_LEVEL(glog);

#include <glog/logging.h>

/****************************below is tracing*************************************/
#define TRACING_LOG_LEVEL 5
#define LOG_TERMINAL 1

inline uint32_t get_tid()
{
#if defined(APPLE)
    return static_cast<std::uint32_t>(std::this_thread::get_id());
#else
    return static_cast<std::uint32_t>(syscall(SYS_gettid));
#endif
}

#include <unordered_map>
#include <vector>
#include <tuple>
#include <mutex>
class Tracer
{
    using InfoTuple = std::tuple<std::string,                        // function_name
                                 std::string,                        // file
                                 size_t,                             //lines
                                 std::string,                        // info
                                 int>;                               // action=0 if in, 1 for out
    using LogStructure = std::unordered_map<size_t,                  // thread_id
                                            std::vector<InfoTuple>>; // info tuple

public:
    static std::unique_ptr<Tracer> get_tracer_instance(std::string func,
                                                       std::string src_file,
                                                       size_t tracing_line,
                                                       std::string info,
                                                       size_t thread_id)
    {
        static std::mutex tracer_protector;

        std::unique_ptr<Tracer> tracer_data(new Tracer(func,
                                                       src_file,
                                                       tracing_line,
                                                       info,
                                                       thread_id,
                                                       Tracer::get_struct_log(),
                                                       tracer_protector));

        return (tracer_data);
    }

protected:
    explicit Tracer(std::string function_name,
                    std::string file_name,
                    size_t tracing_line,
                    std::string info,
                    size_t thread_id,
                    LogStructure &global_log_map,
                    std::mutex &global_tracer_protector) :
        log_map_(global_log_map),
        tracer_protector(global_tracer_protector),
        tracing_function_(function_name),
        tracing_file_(file_name),
        tracing_line_(tracing_line),
        tracing_info_(info),
        tracing_tid_(thread_id)
    {
#ifndef LOG_TERMINAL
        auto tuple_value = std::tuple(tracing_function_,
                                      tracing_file_,
                                      tracing_line_,
                                      tracing_info_, 1);
        {
            std::lock_guard<std::mutex> lock(tracer_protector);
            if (log_map_.find(tracing_tid_) == log_map_.end())
            {
                std::vector<InfoTuple> value;
                log_map_[tracing_tid_] = std::move(value);
            }
        }
        auto &sub_logger = log_map_[tracing_tid_];
        sub_logger.push_back(std::move(tuple_value));
#else
        VLOG(TRACING_LOG_LEVEL) << "[TRACING-IN]: [" << tracing_function_ << " @ "
                                << tracing_file_.substr(tracing_file_.find_last_of("/") + 1) << ":" << tracing_line_ << "]    " << tracing_info_;
#endif
    }

public:
    ~Tracer()
    {
#ifndef LOG_TERMINAL
        auto tuple_value = std::tuple(tracing_function_,
                                      tracing_file_,
                                      tracing_line_,
                                      tracing_info_,
                                      -1);

        auto &sub_logger = log_map_[tracing_tid_];
        sub_logger.push_back(std::move(tuple_value));
#else
        VLOG(TRACING_LOG_LEVEL) << "[TRACING-OUT]: [" << tracing_function_ << " @ "
                                << tracing_file_.substr(tracing_file_.find_last_of("/") + 1) << ":" << tracing_line_ << "]    " << tracing_info_;
#endif
    }

    static LogStructure &get_struct_log()
    {
        static LogStructure logger_details;
        return logger_details;
        //return Tracer::log_map_;
    }

    static std::string to_string()
    {
        std::string log_info;
        for (auto each_log : Tracer::get_struct_log())
        {
            for (auto &structure_log : each_log.second)
            {
                auto &func_name = std::get<0>(structure_log);
                auto &file_name = std::get<1>(structure_log);
                auto &lines = std::get<2>(structure_log);
                auto &info = std::get<3>(structure_log);
                auto &action = std::get<4>(structure_log);

                // std::string func_name,
                //     file_name, info;
                // int action;
                // size_t lines;
                // std::tie(func_name, file_name, lines, info, action) = structure_log;

                std::string buf = "[Tracing_";
                if (action == 1)
                    buf += "in][";
                else
                    buf += "out][";
                buf += "tid=" + std::to_string(each_log.first) + "]: ";
                buf += "Source \"" + file_name + "\"";
                buf += ", line " + std::to_string(lines);
                buf += ", in " + func_name;

                buf += ", info = " + info;

                log_info += buf + "\n";
            }
        }
        return log_info;
    }

private:
    LogStructure &log_map_;
    std::mutex &tracer_protector;

    std::string tracing_function_;
    std::string tracing_file_;
    size_t tracing_line_;
    std::string tracing_info_;
    size_t tracing_tid_;
};

#define CONNECTION(text1, text2) text1##text2
#define CONNECT(text1, text2) CONNECTION(text1, text2)

#define TRACING_UNIQUE(info, count) auto CONNECT(value_, count) = std::move(Tracer::get_tracer_instance(__FUNCTION__, __FILE__, __LINE__, (info), get_tid()))

#define UNIMPLEMENTED LOG(FATAL) << "Function \"" << __PRETTY_FUNCTION__ << "\" is not implemented yet";

#if defined(DEBUGING_TRACING)
#define TRACING(info) \
    TRACING_UNIQUE(info, __COUNTER__)

#define TRACE_IN VLOG(5) << "[TRACING] in function \"" << __FUNCTION__ << "\"";
#define TRACE_OUT VLOG(5) << "[TRACING] out function \"" << __FUNCTION__ << "\"";

#else
#define TRACING(info) \
    do {              \
    } while (0)

#define TRACE_IN \
    do {         \
    } while (0);

#define TRACE_OUT \
    do {          \
    } while (0);
#endif

#endif