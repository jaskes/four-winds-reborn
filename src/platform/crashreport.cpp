#include "crashreport.h"

#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <deque>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>

#include "swe/swe_systems.h"

#if defined(_WIN32)
#ifdef ERROR
#undef ERROR
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#ifdef ERROR
#undef ERROR
#endif
#endif

#if (defined(__APPLE__) || defined(__linux__)) && !defined(__ANDROID__)
#include <execinfo.h>
#endif

#if defined(__APPLE__) || defined(__linux__)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace
{
constexpr std::streamoff MaxReportSize = 4 * 1024 * 1024;
constexpr std::size_t MaxRecentBreadcrumbs = 64;

std::string reportPath;
std::ofstream reportStream;
bool failureReported = false;
std::uint64_t gBreadcrumbSequence = 0;
std::deque<std::string> gRecentBreadcrumbs;

#if defined(_WIN32)
std::string dumpPrefix;
LPTOP_LEVEL_EXCEPTION_FILTER previousExceptionFilter = nullptr;
volatile LONG handlingWindowsException = 0;
#endif

#if defined(__APPLE__) || defined(__linux__)
int reportDescriptor = -1;
volatile std::sig_atomic_t handlingSignal = 0;
#endif

std::string timestamp(void)
{
    const std::time_t now = std::time(nullptr);
    std::tm local = {};

#if defined(_WIN32)
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif

    char buffer[32] = {};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
    return buffer;
}

#if defined(_WIN32)
void appendWindowsText(const char* data, DWORD size)
{
    HANDLE file = CreateFileA(reportPath.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(file == INVALID_HANDLE_VALUE) return;

    while(size)
    {
        DWORD written = 0;
        if(!WriteFile(file, data, size, &written, nullptr) || !written) break;
        data += written;
        size -= written;
    }
    FlushFileBuffers(file);
    CloseHandle(file);
}
#endif

void appendLine(const std::string & message)
{
#if defined(_WIN32)
    const std::string line = timestamp() + " " + message + '\n';
    appendWindowsText(line.data(), static_cast<DWORD>(line.size()));
#else
    if(reportStream.is_open())
        reportStream << timestamp() << " " << message << '\n';
#endif
}

#if defined(_WIN32)
LONG WINAPI windowsUnhandledExceptionFilter(EXCEPTION_POINTERS* pointers)
{
    if(InterlockedCompareExchange(&handlingWindowsException, 1, 0) != 0)
        return EXCEPTION_CONTINUE_SEARCH;

    failureReported = true;

    SYSTEMTIME local = {};
    GetLocalTime(&local);

    char dumpPath[MAX_PATH * 4] = {};
    std::snprintf(dumpPath, sizeof(dumpPath),
        "%s%04u%02u%02u-%02u%02u%02u-%lu.dmp", dumpPrefix.c_str(),
        static_cast<unsigned>(local.wYear), static_cast<unsigned>(local.wMonth),
        static_cast<unsigned>(local.wDay), static_cast<unsigned>(local.wHour),
        static_cast<unsigned>(local.wMinute), static_cast<unsigned>(local.wSecond),
        static_cast<unsigned long>(GetCurrentProcessId()));

    bool dumpWritten = false;
    DWORD dumpError = ERROR_SUCCESS;
    HANDLE dump = CreateFileA(dumpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if(dump != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION exception = {};
        exception.ThreadId = GetCurrentThreadId();
        exception.ExceptionPointers = pointers;
        exception.ClientPointers = FALSE;
        dumpWritten = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(),
            dump, MiniDumpNormal, pointers ? &exception : nullptr, nullptr, nullptr) == TRUE;
        if(!dumpWritten) dumpError = GetLastError();
        FlushFileBuffers(dump);
        CloseHandle(dump);
    }
    else
        dumpError = GetLastError();

    const DWORD code = pointers && pointers->ExceptionRecord ?
        pointers->ExceptionRecord->ExceptionCode : 0;
    void* address = pointers && pointers->ExceptionRecord ?
        pointers->ExceptionRecord->ExceptionAddress : nullptr;

    char line[MAX_PATH * 5] = {};
    const int length = std::snprintf(line, sizeof(line),
        "\n[FATAL WINDOWS EXCEPTION] code=0x%08lX address=%p dump=%s status=%s error=%lu\n",
        static_cast<unsigned long>(code), address, dumpPath,
        dumpWritten ? "written" : "failed", static_cast<unsigned long>(dumpError));
    if(0 < length)
        appendWindowsText(line, static_cast<DWORD>(length < static_cast<int>(sizeof(line)) ?
            length : static_cast<int>(sizeof(line) - 1)));

    return EXCEPTION_EXECUTE_HANDLER;
}
#endif

#if defined(__APPLE__) || defined(__linux__)
void writeAll(const char* data, std::size_t size)
{
    while(reportDescriptor >= 0 && size)
    {
        const ssize_t written = ::write(reportDescriptor, data, size);
        if(written <= 0) return;
        data += written;
        size -= static_cast<std::size_t>(written);
    }
}

void writeSignalNumber(int value)
{
    char digits[16] = {};
    std::size_t count = 0;
    unsigned int number = value < 0 ? static_cast<unsigned int>(-value) :
                                      static_cast<unsigned int>(value);

    do
    {
        digits[count++] = static_cast<char>('0' + number % 10);
        number /= 10;
    }
    while(number && count < sizeof(digits));

    if(value < 0) digits[count++] = '-';
    while(count) writeAll(&digits[--count], 1);
}

void writeStackTrace(void)
{
    if(reportDescriptor < 0) return;

#if defined(__ANDROID__)
    static const char unavailable[] = "Native stack trace unavailable in the Android signal handler.\n";
    writeAll(unavailable, sizeof(unavailable) - 1);
#else
    void* frames[64] = {};
    const int count = ::backtrace(frames, 64);
    if(0 < count) ::backtrace_symbols_fd(frames, count, reportDescriptor);
#endif
}

void fatalSignalHandler(int signal)
{
    if(handlingSignal)
        _exit(128 + signal);

    handlingSignal = 1;
    static const char header[] = "\n[FATAL SIGNAL] signal=";
    static const char stack[] = "\nStack trace:\n";
    writeAll(header, sizeof(header) - 1);
    writeSignalNumber(signal);
    writeAll(stack, sizeof(stack) - 1);
    writeStackTrace();
    if(0 <= reportDescriptor) ::fsync(reportDescriptor);

    ::kill(::getpid(), signal);
    _exit(128 + signal);
}

void installSignalHandler(int signal)
{
    struct sigaction action = {};
    action.sa_handler = fatalSignalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESETHAND;
    sigaction(signal, &action, nullptr);
}
#endif
}

void CrashReport::install(const std::string & application)
{
    failureReported = false;
    gBreadcrumbSequence = 0;
    gRecentBreadcrumbs.clear();
    std::string directory;
    if(const char* overrideDirectory = SWE::Systems::environment("FOUR_WINDS_DIAGNOSTICS_DIR"))
        directory = overrideDirectory;
    else
        directory = SWE::Systems::homeDirectory(application);

    if(!directory.empty() && !SWE::Systems::isDirectory(directory))
        SWE::Systems::makeDirectory(directory);

    reportPath = directory.empty() ? std::string("crash-report.log") :
        SWE::Systems::concatePath(directory, "crash-report.log");

    std::ifstream previous(reportPath, std::ios::binary | std::ios::ate);
    if(previous.is_open() && MaxReportSize < previous.tellg())
    {
        previous.close();
        const std::string rotated = reportPath + ".previous";
        std::remove(rotated.c_str());
        std::rename(reportPath.c_str(), rotated.c_str());
    }
    else if(previous.is_open())
        previous.close();

#if !defined(_WIN32)
    reportStream.open(reportPath, std::ios::out | std::ios::app);
    if(reportStream.is_open()) reportStream.setf(std::ios::unitbuf);
#endif
    appendLine("[SESSION START]");
    appendLine("[DIAGNOSTICS] directory=" + (directory.empty() ? std::string(".") : directory));
    std::clog << "Four Winds diagnostics: " <<
        (directory.empty() ? std::string(".") : directory) << std::endl;

#if defined(_WIN32)
    dumpPrefix = directory.empty() ? std::string("crash-") :
        SWE::Systems::concatePath(directory, "crash-");
    handlingWindowsException = 0;
    previousExceptionFilter = SetUnhandledExceptionFilter(windowsUnhandledExceptionFilter);
#endif

#if defined(__APPLE__) || defined(__linux__)
    reportDescriptor = ::open(reportPath.c_str(), O_WRONLY | O_CREAT | O_APPEND, 0644);
    installSignalHandler(SIGABRT);
    installSignalHandler(SIGBUS);
    installSignalHandler(SIGFPE);
    installSignalHandler(SIGILL);
    installSignalHandler(SIGSEGV);
#endif

    std::set_terminate([]
    {
        CrashReport::reportException("std::terminate");
        std::abort();
    });
}

void CrashReport::shutdown(void)
{
    appendLine(failureReported ? "[SESSION END] failure" : "[SESSION END] clean shutdown");
    if(reportStream.is_open()) reportStream.close();

#if defined(_WIN32)
    SetUnhandledExceptionFilter(previousExceptionFilter);
    previousExceptionFilter = nullptr;
    handlingWindowsException = 0;
#endif

#if defined(__APPLE__) || defined(__linux__)
    if(0 <= reportDescriptor)
    {
        ::close(reportDescriptor);
        reportDescriptor = -1;
    }
#endif
}

void CrashReport::breadcrumb(const std::string & message)
{
    const std::string entry = "seq=" + std::to_string(++gBreadcrumbSequence) + " " + message;
    appendLine("[ACTION] " + entry);
    gRecentBreadcrumbs.push_back(entry);
    if(MaxRecentBreadcrumbs < gRecentBreadcrumbs.size()) gRecentBreadcrumbs.pop_front();
}

std::uint64_t CrashReport::breadcrumbSequence(void)
{
    return gBreadcrumbSequence;
}

std::vector<std::string> CrashReport::recentBreadcrumbs(void)
{
    return std::vector<std::string>(gRecentBreadcrumbs.begin(), gRecentBreadcrumbs.end());
}

void CrashReport::reportException(const std::string & message)
{
    failureReported = true;
    appendLine("[FATAL EXCEPTION] " + message);

#if defined(__APPLE__) || defined(__linux__)
    if(0 <= reportDescriptor)
    {
        static const char stack[] = "Stack trace:\n";
        writeAll(stack, sizeof(stack) - 1);
        writeStackTrace();
        ::fsync(reportDescriptor);
    }
#endif
}

const std::string & CrashReport::filePath(void)
{
    return reportPath;
}
