#ifndef _UTILS_LOADED
#define _UTILS_LOADED

#define EXECVP_CPP(args) \
    do { \
        std::vector<char*> c_args; \
        for (const std::string& arg : args) { \
            c_args.push_back(const_cast<char*>(arg.c_str())); \
        } \
        c_args.push_back(nullptr); \
        execvp(c_args[0], c_args.data()); \
    } while (0)

#endif