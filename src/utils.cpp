#include "utils.hpp"

#include <boost/stacktrace/stacktrace_fwd.hpp>
#include <cstring>
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
    std::cout << std::endl;

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

pid_t waitProcess(int *status) {
    #ifndef USING_WIN
        return wait(status);
    #else
        cerr << "Windows waitProcess NYI!" << endl;
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

zmq::message_t createMessageWithStrings(vector<string> &strings, int offset, int spaceAfter) {
    int totalSize = 0;
    for (auto str: strings) totalSize += str.size();

    // + vector.size(): account for null characters at end of each string
    zmq::message_t message(offset + spaceAfter + sizeof(int) + totalSize + strings.size());

    // Also write an int with the vector size
    int vectorSize = strings.size();
    std::memcpy(static_cast<int*>(message.data()) + offset, &vectorSize, sizeof(int));

    int writtenBytes = sizeof(int);
    for (int i = 0; i < strings.size(); i++) {
        std::memcpy(
            static_cast<char*>(message.data()) + offset + writtenBytes, 
            (strings[i] + '\0').data(), strings[i].size() + 1
        );
        writtenBytes += strings[i].size() + 1;
    }

    return message;
}

std::vector<std::string> readStringsFromMessage(zmq::message_t &message, int offset) {
    const char* data = static_cast<const char*>(message.data());
    size_t size = message.size();

    int vectorSize;
    std::memcpy(&vectorSize, data, sizeof(int));

    std::vector<std::string> result(vectorSize);
    int currentString = 0;

    size_t start = offset + sizeof(int);
    for (size_t i = start; i < size && currentString < vectorSize; ++i) {
        if (data[i] == '\0') {
            result[currentString] = string(&data[start], &data[i]);
            start = i + 1;
            currentString++;
        }
    }

    return result;
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