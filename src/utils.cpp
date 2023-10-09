#include <iostream>
#include <stdexcept>

#if defined(_WIN32) || defined(_WIN64) || defined(WIN32) || defined(WIN64) || defined(__MINGW32__) || defined(__MINGW64__) || defined(__MSYS__)

#include <windows.h>
#include <dbghelp.h>

void printStackTrace() {
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
            std::cout << "Frame " << frame << ": " << symbolInfo.si.Name << std::endl;
        }
    }
}

#else // Assume Unix-like system

#include <execinfo.h>

void printStackTrace() {
    void* callstack[128];
    int frames = backtrace(callstack, 128);
    char** symbols = backtrace_symbols(callstack, frames);

    if (symbols == nullptr) {
        perror("backtrace_symbols");
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < frames; ++i) {
        std::cerr << "Frame " << i << ": " << symbols[i] << std::endl;
    }

    free(symbols);
}

#endif