#pragma once
#include <spdlog/spdlog.h>

#include "defines.h"
#include "spdlog/sinks/stdout_color_sinks-inl.h"
#include "spdlog/fmt/bundled/ranges.h"


namespace EOS
{
    //TODO: Wrap in Handle Template to make it easier to manage
    class Logger final
    {
    public:
        Logger() = default;
        ~Logger() = default;
        DELETE_COPY_MOVE(Logger)

        static inline void Init(const char* loggerName)
        {
            LoggerInstance = spdlog::stdout_color_mt(loggerName);
        }

        inline std::shared_ptr<spdlog::logger> operator->() const noexcept
        {
            return LoggerInstance;
        }

    private:
        static inline std::shared_ptr<spdlog::logger> LoggerInstance;
    };

    // Global instance (externally linked)
    extern Logger Logger;
}
