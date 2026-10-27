//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Handle game crashing                //
//                                              //
//==============================================//

#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>

struct _EXCEPTION_POINTERS;

namespace Engine
{
    class CrashHandler final
    {
    public:
        static CrashHandler& Get();
        CrashHandler(const CrashHandler&) = delete;
        CrashHandler& operator=(const CrashHandler&) = delete;
        CrashHandler(CrashHandler&&) = delete;
        CrashHandler& operator=(CrashHandler&&) = delete;
        bool Initialize();
        void Shutdown();
        bool IsInitialized() const noexcept;
    private:
        CrashHandler() = default;
        ~CrashHandler();
        static long __stdcall UnhandledExceptionFilter(_EXCEPTION_POINTERS* exceptionPointers);
        long HandleCrash(_EXCEPTION_POINTERS* exceptionPointers);
        bool WriteCrashReport(_EXCEPTION_POINTERS* exceptionPointers,const std::filesystem::path& reportPath,const std::filesystem::path& dumpPath);
        bool WriteMinidump(_EXCEPTION_POINTERS* exceptionPointers,const std::filesystem::path& dumpPath);
        void* m_PreviousExceptionFilter = nullptr;
        std::atomic_bool m_Initialized = false;
        std::atomic_flag m_HandlingCrash = ATOMIC_FLAG_INIT;
        bool m_SymbolsInitialized = false;
    };
}

inline Engine::CrashHandler& CrashHandler()
{
    return Engine::CrashHandler::Get();
}
