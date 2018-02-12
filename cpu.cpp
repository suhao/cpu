// cpu.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"
#include <windows.h>
#include <TlHelp32.h>
#include <string>
#include <thread>

HANDLE process = INVALID_HANDLE_VALUE;
int processor_count = 0;
long long last_system_time = 0;
long long last_time = 0;
volatile bool running = true;

// Please change |MonitorName| to the name of the monitor instance.
static const std::wstring kMonitorName = L"Monitor.exe";

static unsigned long long FileTimeToUTC(const FILETIME& ftime) {
    LARGE_INTEGER li;
    li.LowPart = ftime.dwLowDateTime;
    li.HighPart = ftime.dwHighDateTime;
    return li.QuadPart;
}

double GetCPUUsage() {
    if (INVALID_HANDLE_VALUE == process) return -1;

    if (0 == processor_count) {
        SYSTEM_INFO system_info = { 0 };
        ::GetNativeSystemInfo(&system_info);
        processor_count = system_info.dwNumberOfProcessors;
    }

    FILETIME now;
    FILETIME creation_time;
    FILETIME exit_time;
    FILETIME kernel_time;
    FILETIME user_time;

    ::GetSystemTimeAsFileTime(&now);

    if (!GetProcessTimes(process, &creation_time, &exit_time,
        &kernel_time, &user_time)) {
        // We don't assert here because in some cases (such as in the Task Manager)
        // we may call this function on a process that has just exited but we have
        // not yet received the notification.
        return 0;
    }
    long long system_time = (FileTimeToUTC(kernel_time) + FileTimeToUTC(user_time)) /
        processor_count;
    long long time = FileTimeToUTC(now);

    if ((last_system_time == 0) || (last_time == 0)) {
        // First call, just set the last values.
        last_system_time = system_time;
        last_time = time;
        return 0;
    }

    long long system_time_delta = system_time - last_system_time;
    long long  time_delta = time - last_time;
    if (time_delta == 0) return 0;

    int cpu = static_cast<int>((system_time_delta * 100 + time_delta / 2) / time_delta);

    last_system_time = system_time;
    last_time = time;

    return cpu;
}

int main() {
    auto pid = [&]()->DWORD {
        auto snap = ::CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (!snap) return 0;

        PROCESSENTRY32 entry = { 0 };
        entry.dwSize = sizeof(PROCESSENTRY32);
        auto result = ::Process32First(snap, &entry);
        while (TRUE == result) {
            if (kMonitorName == entry.szExeFile) {
                ::CloseHandle(snap);
                return entry.th32ProcessID;
            }
            result = ::Process32Next(snap, &entry);
        }
        ::CloseHandle(snap);
        return 0;
    }();
    printf("pid: %u\n", pid);

    process = ::OpenProcess(PROCESS_QUERY_INFORMATION, FALSE, pid);
    if (INVALID_HANDLE_VALUE == process) return -1;

    auto cpu_counter = std::thread([]() {
        printf("thread start...\n");
        while (running) {
            SYSTEMTIME now;
            ::GetLocalTime(&now);
            printf("cpu-usage:\t%2d:%2d:%2d.%08d\t%lf\n", now.wHour, now.wMinute, now.wSecond, now.wMilliseconds, GetCPUUsage());
            Sleep(1000);
        }
        printf("thread ending...\n");
    });

    struct Handler {
    static BOOL Routine(DWORD type) {
        if (type == CTRL_CLOSE_EVENT) {
            printf("close...\n");
            running = false;
            return TRUE;
        }
        return FALSE;
    }};

    SetConsoleCtrlHandler((PHANDLER_ROUTINE)Handler::Routine, TRUE);
    cpu_counter.join();
    return 0;
}

