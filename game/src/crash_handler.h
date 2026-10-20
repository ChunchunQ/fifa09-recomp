#pragma once
// Crash reporting for FIFA 09 PC.
//
// On a fatal error (REX_FATAL -> abort, e.g. "invalid or unregistered function")
// or an unhandled exception (access violation, ...) this writes crash.txt next
// to fifa09.exe with the reason and the last lines of the log, then shows a
// message box telling the player where the report is.

#include <atomic>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <deque>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>

#include <rex/cvar.h>
#include <rex/logging.h>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#include <dbghelp.h>
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "dbghelp.lib")
#endif

namespace fifa09 {

inline constexpr const char* kPortVersion = "FIFA 09 PC Port v1.0";

class CrashHandler {
 public:
  static void Install() {
#if defined(_WIN32)
    previous_filter_ = SetUnhandledExceptionFilter(&OnException);
#endif
    std::signal(SIGABRT, &OnAbort);
    std::set_terminate(&OnTerminate);
  }

 private:
  static std::filesystem::path ExeDir() {
#if defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return std::filesystem::path(std::wstring(buf, n)).parent_path();
#else
    return std::filesystem::current_path();
#endif
  }

  // Last `count` lines of the log file, and the most relevant error line.
  static std::deque<std::string> LogTail(size_t count, std::string& last_error) {
    std::deque<std::string> tail;
    std::string name = rex::cvar::GetFlagByName("log_file");
    if (name.empty()) return tail;
    std::filesystem::path path(name);
    if (path.is_relative()) path = std::filesystem::current_path() / path;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
      if (line.find("[FATAL]") != std::string::npos || line.find("[critical]") != std::string::npos ||
          line.find("[error]") != std::string::npos) {
        last_error = line;
      }
      tail.push_back(line);
      if (tail.size() > count) tail.pop_front();
    }
    return tail;
  }

  static void Report(const std::string& reason, const std::string& stack = {}) {
    static std::atomic<bool> reported{false};
    if (reported.exchange(true)) return;   // only once, even if several paths fire

    if (auto logger = rex::GetLogger()) logger->flush();

    std::string last_error;
    auto tail = LogTail(60, last_error);

    char when[64] = {};
    std::time_t now = std::time(nullptr);
    std::strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", std::localtime(&now));

    auto report_path = ExeDir() / "crash.txt";
    {
      std::ofstream out(report_path, std::ios::trunc);
      out << kPortVersion << " crash report\n";
      out << "Time:   " << when << "\n";
      out << "Reason: " << reason << "\n";
      if (!last_error.empty()) out << "Last error: " << last_error << "\n";
      if (!stack.empty()) out << "\n---- call stack ----\n" << stack;
      out << "\n---- last log lines ----\n";
      for (const auto& l : tail) out << l << "\n";
    }

#if defined(_WIN32)
    std::string text = std::string("FIFA 09 has crashed.\n\n") + reason;
    if (!last_error.empty()) {
      auto pos = last_error.find("] ", last_error.rfind("[t"));
      text += "\n\n" + (pos != std::string::npos ? last_error.substr(pos + 2) : last_error);
    }
    text += "\n\nA report was saved to crash.txt next to the game.\n"
            "Please attach it when you report the problem.\n\nOpen the folder now?";
    std::wstring wtext(text.begin(), text.end());
    int answer = MessageBoxW(nullptr, wtext.c_str(), L"FIFA 09 PC",
                             MB_YESNO | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    if (answer == IDYES) {
      std::wstring args = L"/select,\"" + report_path.wstring() + L"\"";
      ShellExecuteW(nullptr, L"open", L"explorer.exe", args.c_str(), nullptr, SW_SHOWNORMAL);
    }
#endif
  }

  static void OnAbort(int) {
    Report("Fatal error (the game stopped itself).");
    std::_Exit(3);
  }

  static void OnTerminate() {
    Report("Unhandled C++ exception.");
    std::_Exit(3);
  }

#if defined(_WIN32)
  // Names the functions on the crashing thread's stack (recompiled guest
  // functions are named after their Xbox 360 address, e.g. sub_8245A3C0), using
  // fifa09.pdb when it is next to the exe.
  static std::string StackText(const CONTEXT* ctx_in) {
    if (!ctx_in) return {};
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_LOAD_LINES | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, nullptr, TRUE);
    CONTEXT ctx = *ctx_in;
    STACKFRAME64 frame = {};
    frame.AddrPC.Offset = ctx.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = ctx.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    frame.AddrStack.Offset = ctx.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    std::string text;
    alignas(SYMBOL_INFO) char symbuf[sizeof(SYMBOL_INFO) + 256];
    for (int i = 0; i < 32; ++i) {
      if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &ctx, nullptr,
                       SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
          frame.AddrPC.Offset == 0) {
        break;
      }
      auto* sym = reinterpret_cast<SYMBOL_INFO*>(symbuf);
      sym->SizeOfStruct = sizeof(SYMBOL_INFO);
      sym->MaxNameLen = 255;
      DWORD64 disp = 0;
      char line[400];
      if (SymFromAddr(process, frame.AddrPC.Offset, &disp, sym)) {
        snprintf(line, sizeof(line), "  #%02d %s+0x%llx\n", i, sym->Name,
                 static_cast<unsigned long long>(disp));
      } else {
        snprintf(line, sizeof(line), "  #%02d 0x%016llx\n", i,
                 static_cast<unsigned long long>(frame.AddrPC.Offset));
      }
      text += line;
    }
    return text;
  }

  static LONG WINAPI OnException(EXCEPTION_POINTERS* info) {
    char buf[160];
    auto* rec = info ? info->ExceptionRecord : nullptr;
    snprintf(buf, sizeof(buf), "Unhandled exception 0x%08lX at 0x%p",
             rec ? rec->ExceptionCode : 0UL, rec ? rec->ExceptionAddress : nullptr);
    Report(buf, StackText(info ? info->ContextRecord : nullptr));
    if (previous_filter_) return previous_filter_(info);
    return EXCEPTION_EXECUTE_HANDLER;
  }
  static inline LPTOP_LEVEL_EXCEPTION_FILTER previous_filter_ = nullptr;
#endif
};

}  // namespace fifa09
