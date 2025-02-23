#include "utils.hpp"

#include <boost/stacktrace/stacktrace_fwd.hpp>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <iterator>

#ifdef USING_WIN
    #include <windows.h>
    #include <dbghelp.h>
#else
    #include <execinfo.h>
    #include <unistd.h>
    #include <sys/wait.h>
    #include <sched.h>
#endif

#ifndef NDEBUG
    #include <boost/stacktrace.hpp>
    #define USE_BOOST
#endif

using namespace std;

namespace psumo {

pid_t runProcess(string exePath, vector<string>& args) {
    std::cout << "command: " << exePath << " ";
    for (int i = 0; i < args.size(); i++) std::cout << args[i] << " ";
    std::cout << std:: endl;

    #ifndef USING_WIN
        int pid = fork();
        if (pid == 0) {
            vector<string> execpvpArgs(args);
            execpvpArgs.insert(execpvpArgs.begin(), exePath);
            EXECVP_CPP(execpvpArgs);
            stringstream msg;
            msg << "execp for process " << exePath << " [" << getPid() << "] failed!" << endl;
            cerr << msg.str();
        } else if (pid == -1) {
            perror("fork");
            exit(EXIT_FAILURE);
        }
        // printf("Started process %s with pid %d\n", exePath.c_str(), pid);
        return pid;
    #else
        cerr << "Windows runProcess NYI!" << endl;
        exit(EXIT_FAILURE);
    #endif
}

pid_t waitProcess(bool* exited, int* status_or_signal) {
    #ifndef USING_WIN
        int status;
        pid_t pid = wait(&status);
        *exited = WIFEXITED(status);
        if (*exited) {
            *status_or_signal = WEXITSTATUS(status);
        } else {
            *status_or_signal = WTERMSIG(status);
        }
        return pid;
    #else
        cerr << "Windows waitProcess NYI!" << endl;
        exit(EXIT_FAILURE);
    #endif
}

void killProcess(pid_t pid) {
    #ifndef USING_WIN
        kill(pid, SIGKILL);
    #else
        cerr << "Windows killProcess NYI!" << endl;
        exit(EXIT_FAILURE);
    #endif
}

pid_t getPid() {
    #ifndef USING_WIN
        return getpid();
    #else
        cerr << "Windows getpid NYI!" << endl;
        exit(EXIT_FAILURE);
    #endif
}

void bindProcessToCPU(unsigned int cpuId) {
    #ifndef USING_WIN
        cpu_set_t mask;
        CPU_ZERO(&mask);
        CPU_SET(cpuId, &mask);
        int result = sched_setaffinity(0, sizeof(mask), &mask);
        if (result < 0) {
            std::cerr << "sched_setaffinity failure: " << errno << std::endl;
            exit(EXIT_FAILURE);
        }
    #else
        cerr << "Windows bindProcessToCPU NYI!" << endl;
        exit(EXIT_FAILURE);
    #endif
}

string getSumoPath(bool gui) {
    string sumoExe;
    if(gui)
        sumoExe = "/bin/sumo-gui";
    else
        sumoExe = "/bin/sumo";

    string sumoPath;
    char* sumoPathPtr(getenv("SUMO_HOME"));
    if (sumoPathPtr == NULL) {
        std::cout << "$SUMO_HOME is not set! Must set $SUMO_HOME." << std::endl;
        exit(EXIT_FAILURE);
    } else {
        sumoPath = sumoPathPtr;
        std::cout << "$SUMO_HOME is set to '" << sumoPath << "'" << std::endl;
        return sumoPath + sumoExe;
    }
}

filesystem::path getPartitionDataFile(string dataFolder, int partId) {
    stringstream fname;
    fname << "partData" << partId << ".json";
    return filesystem::path(dataFolder) / fname.str();
}

filesystem::path getCurrentExePath() {
    char buffer[1024];
    #ifdef USING_WIN
        GetModuleFileName(NULL, buffer, sizeof(buffer));
    #elif __linux__
        ssize_t count = readlink("/proc/self/exe", buffer, sizeof(buffer));
        if (count != -1) {
            buffer[count] = '\0';
        }
    #elif __APPLE__ // Half-baked, as the rest of the program doesn't likely support Apple
        uint32_t size = sizeof(buffer);
        _NSGetExecutablePath(buffer, &size);
    #endif
    return string(buffer);
}

filesystem::path getCurrentExeDirectory()  {
    return std::filesystem::path(getCurrentExePath()).parent_path();
}


string getStackTrace() {
    stringstream outStream;

#ifdef USE_BOOST
    outStream << boost::stacktrace::stacktrace();
#elif defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__MSYS__)

    const int maxFrames = 128;
    HANDLE process = GetCurrentProcess();
    HANDLE thread = GetCurrentThread();
    CONTEXT context;
    memset(&context, 0, sizeof(CONTEXT));
    context.ContextFlags = CONTEXT_FULL;

    RtlCaptureContext(&context);

    STACKFRAME64 stackFrame;
    memset(&stackFrame, 0, sizeof(STACKFRAME64));
    stackFrame.AddrPC.Offset = context.Rip;
    stackFrame.AddrPC.Mode = AddrModeFlat;
    stackFrame.AddrFrame.Offset = context.Rbp;
    stackFrame.AddrFrame.Mode = AddrModeFlat;
    stackFrame.AddrStack.Offset = context.Rsp;
    stackFrame.AddrStack.Mode = AddrModeFlat;

    SYMBOL_INFO_PACKAGE symbolInfo;
    symbolInfo.si.SizeOfStruct = sizeof(SYMBOL_INFO);
    symbolInfo.si.MaxNameLen = MAX_SYM_NAME;

    for (int frame = 0; frame < maxFrames; ++frame) {
        if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &stackFrame, &context, NULL, SymFunctionTableAccess64, SymGetModuleBase64, NULL)) {
            break;
        }

        if (SymFromAddr(process, (DWORD64)(stackFrame.AddrPC.Offset), NULL, &symbolInfo.si)) {
            outStream << "Frame " << frame << ": " << symbolInfo.si.Name << endl;
        }
    }

#else

    void* callstack[128];
    int frames = backtrace(callstack, 128);
    char** symbols = backtrace_symbols(callstack, frames);

    if (symbols == nullptr) {
        perror("backtrace_symbols");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < frames; ++i) {
        outStream << "Frame " << i << ": " << symbols[i] << endl;
    }

    free(symbols);

#endif
    return outStream.str();
}

void printStackTrace() {
    cerr << getStackTrace();
}

template<typename T>
void _printVector(
    const vector<T>& v, const string prefix, 
    const string sep, const bool newline, ostream& stream
) {
    stringstream ss;
    ss << prefix;
    for (const auto& element : v) {
        stream << element;
        if (&element != &v.back()) {
        stream << sep;
        }
    }
    if (newline) ss << endl;
    stream << ss.str();
}

template<typename T>
void printVector(
    const vector<T, std::allocator<T>>& v, const string prefix, 
    const string sep, const bool newline, ostream& stream
) {
    _printVector(v, prefix, sep, newline, stream);
}

void printVector(
    const vector<string>& v, const string prefix, 
    const string sep, const bool newline, ostream& stream
) {
    _printVector<string>(v, prefix, sep, newline, stream);
}

}