//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Connect engine to log writer        //
//                                              //
//==============================================//

#pragma once

#include <cstddef>
#include <deque>
#include <fstream>
#include <algorithm>
#include <filesystem>
#include <format>
#include <mutex>
#include <source_location>
#include <string>
#include <string_view>
#include <vector>

#include "Console.h"

namespace Engine
{
    enum class LogLevel {
        Trace = 0,
        Debug,
        Info,
        Warning,
        Error,
        Critical
    };
    class Logger final
    {
    public:
        ~Logger() { Shutdown(); }
        Logger(const Logger&) = delete;
        Logger& operator=(const Logger&) = delete;
        Logger(Logger&&) = delete;
        Logger& operator=(Logger&&) = delete;
        static Logger& Get() { static Logger instance; return instance; }
        bool Initialize(const std::filesystem::path& logFile);
        void Shutdown();
        void SetMinimumLevel(LogLevel level) { std::scoped_lock lock(m_Mutex); m_MinimumLevel = level; }
        LogLevel GetMinimumLevel() const { std::scoped_lock lock(m_Mutex); return m_MinimumLevel; }
        template <typename... Args>
        void Trace(std::source_location location,std::format_string<Args...> format,Args&&... args){
            Write(LogLevel::Trace,{},location,std::vformat(format.get(), std::make_format_args(args...)));
        }
        template <typename... Args>
        void Debug(std::source_location location,std::format_string<Args...> format,Args&&... args){
            Write(LogLevel::Debug, {}, location, std::vformat(format.get(), std::make_format_args(args...)));
        }
        template <typename... Args>
        void Info(std::source_location location,std::format_string<Args...> format,Args&&... args){
            Write(LogLevel::Info, {}, location, std::vformat(format.get(), std::make_format_args(args...)));
        }
        template <typename... Args>
        void Warning(std::source_location location,std::format_string<Args...> format,Args&&... args){
            Write(LogLevel::Warning, {}, location, std::vformat(format.get(), std::make_format_args(args...)));
        }
        template <typename... Args>
        void Error(std::source_location location,std::format_string<Args...> format,Args&&... args){
            Write(LogLevel::Error, {}, location, std::vformat(format.get(), std::make_format_args(args...)));
        }
        template <typename... Args>
        void Critical(std::source_location location,std::format_string<Args...> format,Args&&... args){
            Write(LogLevel::Critical, {}, location, std::vformat(format.get(), std::make_format_args(args...)));
        }
        template <typename... Args>
        void Info(std::string_view category,std::source_location location,std::format_string<Args...> format,Args&&... args){
            Write(LogLevel::Info, category, location, std::vformat(format.get(), std::make_format_args(args...)));
        }

        template <typename... Args>
        void Warning(std::string_view category,std::source_location location,std::format_string<Args...> format,Args&&... args){
            Write(LogLevel::Warning, category, location, std::vformat(format.get(), std::make_format_args(args...)));
        }
        template <typename... Args>
        void Error(std::string_view category,std::source_location location,std::format_string<Args...> format,Args&&... args){
            Write(LogLevel::Error, category, location, std::vformat(format.get(), std::make_format_args(args...)));
        }
        template <typename... Args>
        void Critical(std::string_view category,std::source_location location,std::format_string<Args...> format,Args&&... args){
            Write(LogLevel::Critical, category, location, std::vformat(format.get(), std::make_format_args(args...)));
        }
        std::vector<std::string> TryGetRecentEvents() const;
    private:
        Logger() = default;
        struct PendingConsoleLine
        {
            std::string text;
            ConsoleColor color;
        };
        void Write(LogLevel level,std::string_view category,std::source_location location,std::string message);
        static std::string BuildLine(LogLevel level,std::string_view category,std::source_location location,std::string_view message);
    private:
        mutable std::mutex m_Mutex;
        std::ofstream* m_File = nullptr;
        std::filesystem::path m_LogFile;
        LogLevel m_MinimumLevel = LogLevel::Trace;
        bool m_Initialized = false;
        std::deque<PendingConsoleLine> m_PendingConsole;
        std::deque<std::string> m_RecentEvents;
        std::size_t m_RecentCapacity = 512;
    };
}
#define LOG_TRACE(...) \
    ::Engine::Logger::Get().Trace(std::source_location::current(), __VA_ARGS__)
#define LOG_DEBUG(...) \
    ::Engine::Logger::Get().Debug(std::source_location::current(), __VA_ARGS__)
#define LOG_INFO(...) \
    ::Engine::Logger::Get().Info(std::source_location::current(), __VA_ARGS__)
#define LOG_WARN(...) \
    ::Engine::Logger::Get().Warning(std::source_location::current(), __VA_ARGS__)
#define LOG_ERROR(...) \
    ::Engine::Logger::Get().Error(std::source_location::current(), __VA_ARGS__)
#define LOG_CRITICAL(...) \
    ::Engine::Logger::Get().Critical(std::source_location::current(), __VA_ARGS__)
#define LOG_CATEGORY_INFO(category, ...) \
    ::Engine::Logger::Get().Info((category), std::source_location::current(), __VA_ARGS__)
#define LOG_CATEGORY_WARN(category, ...) \
    ::Engine::Logger::Get().Warning((category), std::source_location::current(), __VA_ARGS__)
#define LOG_CATEGORY_ERROR(category, ...) \
    ::Engine::Logger::Get().Error((category), std::source_location::current(), __VA_ARGS__)
#define LOG_CATEGORY_CRITICAL(category, ...) \
    ::Engine::Logger::Get().Critical((category), std::source_location::current(), __VA_ARGS__)
