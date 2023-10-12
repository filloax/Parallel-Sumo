#include "src/utils.hpp"

#include <cstring>
#include <string>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <iterator>

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__MSYS__)
    #include <windows.h>
    #include <dbghelp.h>
#else
    #include <execinfo.h>
#endif

using namespace std;

zmq::message_t createMessageWithStrings(vector<string> &strings, int offset, int spaceAfter) {
    int totalSize = 0;
    for (auto str: strings) totalSize += strings.size();

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
            result[i] = string(data + start, i - start);
            start = i + 1;
            currentString++;
        }
    }

    return result;
}

string getStackTrace() {
    stringstream outStream;

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__MSYS__)

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