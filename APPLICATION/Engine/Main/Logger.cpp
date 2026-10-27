//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Write every game breath to file     //
//                                              //
//==============================================//

#include "Logger.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <thread>
#include <utility>

namespace Engine
{
    bool Logger::Initialize(const std::filesystem::path& logFile) {
        std::scoped_lock lock(m_Mutex);
#ifdef _DEBUG
        m_Initialized = true;
        return true;
#endif
        if (m_Initialized)
            return true;
        try {
            if (logFile.has_parent_path())
                std::filesystem::create_directories(logFile.parent_path());
            auto* file = new std::ofstream(logFile, std::ios::out);
            if (!file->is_open()) {
                delete file;
                return false;
            }
            m_File = file;
            m_LogFile = logFile;
            m_RecentCapacity = 512;
            m_RecentEvents.clear();
            m_Initialized = true;
            (*m_File) << "\n========== Logger session started ==========\n";
            m_File->flush();
            m_Initialized = true;
            return true;
        }
        catch (...) {
            if (m_File) {
                delete m_File;
                m_File = nullptr;
            }
            m_Initialized = false;
            return false;
        }
    }
    void Logger::Shutdown() {
        if (!m_Initialized)
            return;
        std::scoped_lock lock(m_Mutex);
        if (!m_File) {
            m_Initialized = false;
            return;
        }
        (*m_File) << BuildLine(LogLevel::Info, "Logger", std::source_location::current(), "Logger shutting down") << '\n';
        m_File->flush();
        m_File->close();
        delete m_File;
        m_File = nullptr;
        m_Initialized = false;
    }
    void Logger::Write(LogLevel level, std::string_view category, std::source_location location, std::string message) {
        if (!m_Initialized)
            return;
        std::scoped_lock lock(m_Mutex);
        if (level < m_MinimumLevel)
            return;
        const std::string line = BuildLine(level, category, location, message);
        m_RecentEvents.push_back(line);
        while (m_RecentEvents.size() > m_RecentCapacity)
            m_RecentEvents.pop_front();
        if (m_File && m_File->is_open()) {
            (*m_File) << line << '\n';
            m_File->flush();
        }
        auto* devModeVar = ConsoleSystem::Get().GetConVar("devmode");
        if (devModeVar && devModeVar->GetBool()) {
            ConsoleColor color = ConsoleColor::White();
            switch (level) {
            case LogLevel::Trace:    color = ConsoleColor::Secondary(); break;
            case LogLevel::Debug:    color = ConsoleColor::Secondary(); break;
            case LogLevel::Info:     color = ConsoleColor::Info(); break;
            case LogLevel::Warning:  color = ConsoleColor::Warning(); break;
            case LogLevel::Error:    color = ConsoleColor::Error(); break;
            case LogLevel::Critical: color = ConsoleColor::Error(); break;
            }
            std::string consoleText;
            if (!category.empty())
                consoleText = std::format("[{}] {}", category, message);
            else
                consoleText = message;
            ConsoleSystem::Get().Print(consoleText, color);
        }
#ifdef _DEBUG
        _CrtDbgReport(_CRT_WARN, nullptr, 0, nullptr, "%s\n", line.c_str());
#endif
    }
    std::string Logger::BuildLine(LogLevel level, std::string_view category, std::source_location location, std::string_view message) {
        using namespace std::chrono;
        const auto now = system_clock::now();
        const auto seconds = time_point_cast<std::chrono::seconds>(now);
        const auto millis = duration_cast<milliseconds>(now - seconds).count();
        const std::time_t timestamp = system_clock::to_time_t(seconds);
        std::tm local{};
#ifdef _WIN32
        localtime_s(&local, &timestamp);
#else
        localtime_r(&timestamp, &local);
#endif
        std::ostringstream out;
        std::string_view loglevel = "";
        switch (level)
        {
        case LogLevel::Trace:    loglevel = "TRACE"; break;
        case LogLevel::Debug:    loglevel = "DEBUG"; break;
        case LogLevel::Info:     loglevel = "INFO"; break;
        case LogLevel::Warning:  loglevel = "WARN"; break;
        case LogLevel::Error:    loglevel = "ERROR"; break;
        case LogLevel::Critical: loglevel = "CRITICAL"; break;
        }
        out << '[' << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << millis << "] [" << loglevel << ']';
        if (!category.empty())
            out << " [" << category << ']';
        std::ostringstream stream;
        stream << std::this_thread::get_id();
        out << " [Thread " << stream.str() << "] " << message;
        if (level == LogLevel::Warning || level == LogLevel::Error || level == LogLevel::Critical)
            out << "  <" << location.file_name() << ':' << location.line() << " | " << location.function_name() << '>';
        return out.str();
    }
    std::vector<std::string> Logger::TryGetRecentEvents() const {
        if (!m_Initialized)
            return {};
        std::unique_lock lock(m_Mutex, std::try_to_lock);
        if (!lock.owns_lock())
            return {};
        return { m_RecentEvents.begin(),m_RecentEvents.end() };
    }
}
