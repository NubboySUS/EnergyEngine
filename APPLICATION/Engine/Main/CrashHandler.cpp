//============== ENERGY ENGINE =================//
//                                              //
// Purpose: Scream very loud when game crashes  //
//                                              //
//==============================================//

#include "CrashHandler.h"
#include "Logger.h"
#ifdef _WIN32
#define NOMINMAX
#include <Windows.h>
#include <DbgHelp.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#pragma comment(lib, "Dbghelp.lib")

const std::filesystem::path crashDirectory = "Engine/Logs/Crashes";

namespace Engine
{
    namespace
    {
        std::string Timestamp() {
            using namespace std::chrono;
            const auto now = system_clock::now();
            const auto seconds = time_point_cast<std::chrono::seconds>(now);
            const auto millis = duration_cast<milliseconds>(now - seconds).count();
            const std::time_t time = system_clock::to_time_t(seconds);
            std::tm local;
            localtime_s(&local, &time);
            std::ostringstream out;
            out << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3) << millis;
            return out.str();
        }

        std::string ExceptionName(DWORD code) {
            switch (code)
            {
                case EXCEPTION_ACCESS_VIOLATION:         return "EXCEPTION_ACCESS_VIOLATION";
                case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "EXCEPTION_ARRAY_BOUNDS_EXCEEDED";
                case EXCEPTION_BREAKPOINT:               return "EXCEPTION_BREAKPOINT";
                case EXCEPTION_DATATYPE_MISALIGNMENT:    return "EXCEPTION_DATATYPE_MISALIGNMENT";
                case EXCEPTION_FLT_DENORMAL_OPERAND:     return "EXCEPTION_FLT_DENORMAL_OPERAND";
                case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "EXCEPTION_FLT_DIVIDE_BY_ZERO";
                case EXCEPTION_FLT_INVALID_OPERATION:    return "EXCEPTION_FLT_INVALID_OPERATION";
                case EXCEPTION_FLT_OVERFLOW:             return "EXCEPTION_FLT_OVERFLOW";
                case EXCEPTION_FLT_STACK_CHECK:          return "EXCEPTION_FLT_STACK_CHECK";
                case EXCEPTION_FLT_UNDERFLOW:            return "EXCEPTION_FLT_UNDERFLOW";
                case EXCEPTION_GUARD_PAGE:               return "EXCEPTION_GUARD_PAGE";
                case EXCEPTION_ILLEGAL_INSTRUCTION:      return "EXCEPTION_ILLEGAL_INSTRUCTION";
                case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "EXCEPTION_INT_DIVIDE_BY_ZERO";
                case EXCEPTION_INT_OVERFLOW:             return "EXCEPTION_INT_OVERFLOW";
                case EXCEPTION_INVALID_DISPOSITION:      return "EXCEPTION_INVALID_DISPOSITION";
                case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "EXCEPTION_NONCONTINUABLE_EXCEPTION";
                case EXCEPTION_PRIV_INSTRUCTION:         return "EXCEPTION_PRIV_INSTRUCTION";
                case EXCEPTION_SINGLE_STEP:              return "EXCEPTION_SINGLE_STEP";
                case EXCEPTION_STACK_OVERFLOW:           return "EXCEPTION_STACK_OVERFLOW";
                case EXCEPTION_IN_PAGE_ERROR:            return "EXCEPTION_IN_PAGE_ERROR";
                default:                                 return "UNKNOWN_EXCEPTION";
            }
        }

        std::string ModuleForAddress(DWORD64 address)
        {
            HMODULE module = nullptr;
            if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,reinterpret_cast<LPCSTR>(address),&module))
                return {};
            std::array<char, MAX_PATH> buffer{};
            const DWORD length = GetModuleFileNameA(module,buffer.data(),static_cast<DWORD>(buffer.size()));
            return length > 0 ? std::string(buffer.data(), length) : std::string{};
        }

        std::string DescribeAddress(HANDLE process, DWORD64 address)
        {
            std::array<std::byte, sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)>symbolStorage{};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolStorage.data());
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = MAX_SYM_NAME;
            DWORD64 displacement = 0;
            std::ostringstream out;
            if (SymFromAddr(process, address, &displacement, symbol)) {
                out << symbol->Name;
                if (displacement != 0)
                    out << " + 0x" << std::hex << displacement;
            }
            else
                out << "0x" << std::uppercase << std::hex << address;
            IMAGEHLP_LINE64 line{};
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (SymGetLineFromAddr64(process,address,&lineDisplacement,&line))
                out << "\n        " << line.FileName << ':' << std::dec << line.LineNumber;
            return out.str();
        }
        struct StackFrameDescription
        {
            DWORD64 address = 0;
            std::string symbol;
            std::string file;
            DWORD line = 0;
        };
        std::vector<StackFrameDescription> CaptureStack(EXCEPTION_POINTERS* exceptionPointers) {
            std::vector<StackFrameDescription> frames;
            frames.reserve(64);
            if (!exceptionPointers || !exceptionPointers->ContextRecord)
                return frames;
            HANDLE process = GetCurrentProcess();
            HANDLE thread = GetCurrentThread();
            CONTEXT context = *exceptionPointers->ContextRecord;
            STACKFRAME64 frame{};
            DWORD machineType = 0;
#if defined(_M_X64)
            machineType = IMAGE_FILE_MACHINE_AMD64;
            frame.AddrPC.Offset = context.Rip;
            frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrFrame.Offset = context.Rbp;
            frame.AddrFrame.Mode = AddrModeFlat;
            frame.AddrStack.Offset = context.Rsp;
            frame.AddrStack.Mode = AddrModeFlat;
#elif defined(_M_IX86)
            machineType = IMAGE_FILE_MACHINE_I386;
            frame.AddrPC.Offset = context.Eip;
            frame.AddrPC.Mode = AddrModeFlat;
            frame.AddrFrame.Offset = context.Ebp;
            frame.AddrFrame.Mode = AddrModeFlat;
            frame.AddrStack.Offset = context.Esp;
            frame.AddrStack.Mode = AddrModeFlat;
#else
            return frames;
#endif
            for (std::size_t index = 0; index < 64; ++index) {
                if (frame.AddrPC.Offset == 0)
                    break;
                StackFrameDescription result{};
                result.address = frame.AddrPC.Offset;
                std::array<std::byte, sizeof(SYMBOL_INFO) + MAX_SYM_NAME * sizeof(char)> symbolStorage{};
                auto* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolStorage.data());
                symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
                symbol->MaxNameLen = MAX_SYM_NAME;
                DWORD64 symbolDisplacement = 0;
                if (SymFromAddr(process,result.address,&symbolDisplacement,symbol)) {
                    result.symbol = symbol->Name;
                    if (symbolDisplacement != 0) {
                        std::ostringstream symbolText;
                        symbolText << result.symbol << " + 0x" << std::hex << symbolDisplacement;
                        result.symbol = symbolText.str();
                    }
                }
                else {
                    std::ostringstream out;
                    out << "0x" << std::uppercase << std::hex << result.address;
                    result.symbol = out.str();
                }
                IMAGEHLP_LINE64 line{};
                line.SizeOfStruct = sizeof(line);
                DWORD lineDisplacement = 0;
                if (SymGetLineFromAddr64(process,result.address,&lineDisplacement,&line)) {
                    result.file = line.FileName;
                    result.line = line.LineNumber;
                }
                frames.push_back(std::move(result));
                if (!StackWalk64(machineType,process,thread,&frame,&context,nullptr,SymFunctionTableAccess64,SymGetModuleBase64,nullptr))
                    break;
            }
            return frames;
        }
        std::string AccessViolationDescription(EXCEPTION_POINTERS* exceptionPointers)
        {
            if (!exceptionPointers || !exceptionPointers->ExceptionRecord)
                return {};
            const EXCEPTION_RECORD* record = exceptionPointers->ExceptionRecord;
            if (record->ExceptionCode != EXCEPTION_ACCESS_VIOLATION || record->NumberParameters < 2)
                return {};
            const ULONG_PTR operation = record->ExceptionInformation[0];
            const ULONG_PTR address = record->ExceptionInformation[1];
            const char* operationName = "unknown operation";
            switch (operation)
            {
                case 0: operationName = "read"; break;
                case 1: operationName = "write"; break;
                case 8: operationName = "execute"; break;
                default: break;
            }
            std::ostringstream out;
            out << "0x" << std::uppercase << std::hex << static_cast<DWORD64>(address);
            return std::format("Attempted to {} memory at {}",operationName, out.str());
        }
    }

    CrashHandler& CrashHandler::Get() { static CrashHandler instance; return instance; }

    CrashHandler::~CrashHandler() { Shutdown(); }

    bool CrashHandler::Initialize() {
        if (m_Initialized.load(std::memory_order_acquire))
            return true;
        try {
            std::filesystem::create_directories(crashDirectory);
            HANDLE process = GetCurrentProcess();
            SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_FAIL_CRITICAL_ERRORS);
            m_SymbolsInitialized = SymInitialize(process, nullptr, TRUE) == TRUE;
            m_PreviousExceptionFilter = reinterpret_cast<void*>(SetUnhandledExceptionFilter(&CrashHandler::UnhandledExceptionFilter));
            m_Initialized.store(true, std::memory_order_release);
            return true;
        }
        catch (...) {
            m_Initialized.store(false, std::memory_order_release);
            return false;
        }
    }

    void CrashHandler::Shutdown() {
        if (!m_Initialized.exchange(false, std::memory_order_acq_rel))
            return;
        auto previous = reinterpret_cast<LPTOP_LEVEL_EXCEPTION_FILTER>(m_PreviousExceptionFilter);
        SetUnhandledExceptionFilter(previous);
        m_PreviousExceptionFilter = nullptr;
        if (m_SymbolsInitialized) {
            SymCleanup(GetCurrentProcess());
            m_SymbolsInitialized = false;
        }
    }

    bool CrashHandler::IsInitialized() const noexcept { return m_Initialized.load(std::memory_order_acquire); }

    long __stdcall CrashHandler::UnhandledExceptionFilter(EXCEPTION_POINTERS* exceptionPointers) { return CrashHandler::Get().HandleCrash(exceptionPointers); }

    long CrashHandler::HandleCrash(EXCEPTION_POINTERS* exceptionPointers) {
        if (m_HandlingCrash.test_and_set(std::memory_order_acquire))
            return EXCEPTION_EXECUTE_HANDLER;
        using namespace std::chrono;
        const auto now = system_clock::now();
        const std::time_t time = system_clock::to_time_t(now);
        std::tm local;
        localtime_s(&local, &time);
        std::ostringstream out;
        out << std::put_time(&local, "%Y-%m-%d_%H-%M-%S");
        const std::string timestamp = out.str();
        const DWORD processId = GetCurrentProcessId();
        const DWORD threadId = GetCurrentThreadId();
        const std::string stem = std::format("crash_{}_{}_{}",timestamp,processId,threadId);
        const auto reportPath = crashDirectory / (stem + ".txt");
        const auto dumpPath = crashDirectory / (stem + ".dmp");
        OutputDebugStringA("\n========== ENERGY ENGINE CRASHED :( ==========\n");
        OutputDebugStringA(("Crash report: " + reportPath.string() + "\n").c_str());
        const bool reportWritten = WriteCrashReport(exceptionPointers, reportPath, dumpPath);
        const bool dumpWritten = WriteMinidump(exceptionPointers, dumpPath);
        const std::string summary = std::format("Engine crash detected. Report: {} | Dump{}{}\n",reportPath.string(), dumpWritten ? ": " : " failed to write", dumpWritten ? dumpPath.string() : "");
        OutputDebugStringA(summary.c_str());
        if (reportWritten && exceptionPointers && exceptionPointers->ExceptionRecord) {
            std::ostringstream out;
            out << "0x" << std::uppercase << std::hex << reinterpret_cast<DWORD64>(exceptionPointers->ExceptionRecord->ExceptionAddress);
            OutputDebugStringA(std::format("Exception {} ({}) at {}\n", ExceptionName(exceptionPointers->ExceptionRecord->ExceptionCode), out.str(), Timestamp()).c_str());
        }
        if (dumpWritten)
            OutputDebugStringA("Minidump written successfully.\n");
        return EXCEPTION_EXECUTE_HANDLER;
    }

    bool CrashHandler::WriteCrashReport(EXCEPTION_POINTERS* exceptionPointers,const std::filesystem::path& reportPath,const std::filesystem::path& dumpPath) {
        std::ofstream report(reportPath, std::ios::out | std::ios::trunc);
        if (!report.is_open())
            return false;
        report << "============================================================\n";
        report << "ENGINE CRASH REPORT\n";
        report << "============================================================\n\n";
        report << "Time:\n    " << Timestamp() << "\n\n";
        report << "Process ID:\n    " << GetCurrentProcessId() << "\n\n";
        report << "Thread ID:\n    " << GetCurrentThreadId() << "\n\n";
        if (exceptionPointers && exceptionPointers->ExceptionRecord) {
            const EXCEPTION_RECORD* record = exceptionPointers->ExceptionRecord;
            report << "Exception:\n    " << ExceptionName(record->ExceptionCode) << "\n\n";
            report << "Exception code:\n    0x" << std::uppercase << std::hex << record->ExceptionCode << std::dec << "\n\n";
            report << "Exception address:\n    " << "0x" << std::uppercase << std::hex << reinterpret_cast<DWORD64>(record->ExceptionAddress) << "\n";
            const std::string module = ModuleForAddress(reinterpret_cast<DWORD64>(record->ExceptionAddress));
            if (!module.empty())
                report << "    Module: " << module << "\n";
            const std::string access = AccessViolationDescription(exceptionPointers);
            if (!access.empty())
                report << "    " << access << "\n";
            report << '\n';
            if (m_SymbolsInitialized)
                report << "Faulting address:\n    " << DescribeAddress(GetCurrentProcess(),reinterpret_cast<DWORD64>(record->ExceptionAddress)) << "\n\n";
        }
        else
            report << "Exception:\n    <no EXCEPTION_POINTERS>\n\n";
        report << "============================================================\n";
        report << "CALL STACK\n";
        report << "============================================================\n\n";
        if (m_SymbolsInitialized) {
            const auto frames = CaptureStack(exceptionPointers);
            for (std::size_t i = 0; i < frames.size(); ++i) {
                const auto& frame = frames[i];
                report << "#" << i << " " << frame.symbol << "\n";
                if (!frame.file.empty())
                    report << "    " << frame.file << ':' << frame.line << '\n';
                else
                    report << "    " << "0x" << std::uppercase << std::hex << frame.address << '\n';
                report << '\n';
            }
            if (frames.empty())
                report << "<stack unavailable>\n\n";
        }
        else
            report << "Symbols could not be initialized; source file/line resolution is unavailable.\n\n";
        report << "============================================================\n";
        report << "RECENT LOGGER EVENTS\n";
        report << "============================================================\n\n";
        const std::vector<std::string> recent = Logger::Get().TryGetRecentEvents();
        if (recent.empty())
            report << "<no recent events available>\n";
        else {
            for (const std::string& line : recent)
                report << line << '\n';
        }
        report << "\n============================================================\n";
        report << "FILES\n";
        report << "============================================================\n\n";
        report << "Crash report:\n    " << reportPath.string() << "\n";
        report << "Minidump:\n    " << dumpPath.string() << "\n";
        report.flush();
        return report.good();
    }
    bool CrashHandler::WriteMinidump(EXCEPTION_POINTERS* exceptionPointers,const std::filesystem::path& dumpPath) {
        HANDLE dumpFile = CreateFileW(dumpPath.wstring().c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (dumpFile == INVALID_HANDLE_VALUE)
            return false;
        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
        MINIDUMP_EXCEPTION_INFORMATION* exceptionInfoPtr = nullptr;
        if (exceptionPointers) {
            exceptionInfo.ThreadId = GetCurrentThreadId();
            exceptionInfo.ExceptionPointers = exceptionPointers;
            exceptionInfo.ClientPointers = FALSE;
            exceptionInfoPtr = &exceptionInfo;
        }
        const BOOL result = MiniDumpWriteDump(GetCurrentProcess(),GetCurrentProcessId(),dumpFile,static_cast<MINIDUMP_TYPE>(MiniDumpWithDataSegs | MiniDumpWithHandleData | MiniDumpWithUnloadedModules | MiniDumpWithProcessThreadData | MiniDumpWithThreadInfo | MiniDumpWithFullMemoryInfo),exceptionInfoPtr,nullptr,nullptr);
        FlushFileBuffers(dumpFile);
        CloseHandle(dumpFile);
        return result == TRUE;
    }
}
#endif