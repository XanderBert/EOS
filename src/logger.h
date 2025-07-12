#pragma once
#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/fmt/bundled/ranges.h>
#include <assert.h>
#include "defines.h"
#include "spdlog/sinks/rotating_file_sink.h"

namespace EOS
{
    //TODO: Wrap in Handle Template to make it easier to manage (destruction)
    class Logger final
    {
    public:
        Logger() = default;
        ~Logger() = default;
        DELETE_COPY_MOVE(Logger)

        static inline void Init(const char* loggerName, const char* logFileName)
        {
            // Initialize async thread pool (queue size 8192, 1 background thread)
            spdlog::init_thread_pool(8192, 1);

            //Log everything info and up to console
            auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            consoleSink->set_level(spdlog::level::info);

            //Log everything to the log file
            auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(logFileName,true);
            fileSink->set_level(spdlog::level::trace);

            std::vector<spdlog::sink_ptr> sinks {consoleSink, fileSink};
            //Create the logger and let it log everything
            LoggerInstance = std::make_shared<spdlog::async_logger>(loggerName, sinks.begin(), sinks.end(), spdlog::thread_pool(), spdlog::async_overflow_policy::block);
            LoggerInstance->set_level(spdlog::level::trace);
            LoggerInstance->flush_on(spdlog::level::warn);                  // Auto-update on warnings and above
            spdlog::flush_every(std::chrono::seconds(10));      // Periodic update every 10 seconds

            //register the logger
            spdlog::register_logger(LoggerInstance);
        }

        static inline void Destroy()
        {
            if (LoggerInstance)
            {
                LoggerInstance->flush();
                spdlog::drop(LoggerInstance->name());
                LoggerInstance->sinks().clear();
                LoggerInstance.reset();
            }
        }

        inline std::shared_ptr<spdlog::async_logger> operator->() const noexcept
        {
            return LoggerInstance;
        }

    private:
        static inline std::shared_ptr<spdlog::async_logger> LoggerInstance;
    };

    // Global instance (externally linked)
    extern Logger Logger;


// This Check is stripped out in Release
#if defined(EOS_DEBUG)
#define CHECK(assertion, ...)                                                               \
do                                                                                          \
{                                                                                           \
    if (!(assertion))                                                                       \
    {                                                                                       \
        EOS::Logger->error("{} {}:{}", fmt::format(__VA_ARGS__), __FILE__, __LINE__);       \
        assert(false);                                                                      \
    }                                                                                       \
} while (0)
#else
#define CHECK(assertion, ...)                                                               \
do                                                                                          \
{                                                                                           \
if (!(assertion))                                                                       \
{                                                                                       \
EOS::Logger->error("{} {}:{}", fmt::format(__VA_ARGS__), __FILE__, __LINE__);       \
assert(false);                                                                      \
}                                                                                       \
} while (0)
#endif

//The Logging and assertion is Stripped Out in release, the if check and returning stays in release.
//Don't use this if you don't want branching in release.
#if defined(EOS_DEBUG)
#define CHECK_RETURN(assertion, ...)                                                        \
do                                                                                          \
{                                                                                           \
    if (!(assertion))                                                                       \
    {                                                                                       \
        EOS::Logger->error("{} {}:{}", fmt::format(__VA_ARGS__), __FILE__, __LINE__);       \
        assert(false);                                                                      \
        return;                                                                             \
    }                                                                                       \
} while (0)
#else
#define CHECK_RETURN(assertion, ...)                                                        \
    if (!(assertion))                                                                       \
    {                                                                                       \
        return;                                                                             \
    }
#endif
}
