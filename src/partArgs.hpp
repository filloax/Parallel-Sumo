/**
args.hpp

Wrapper for args, to access them in a single object.

Author: Filippo Lenzi
*/

#pragma once

#include "libs/argparse.hpp"
#include "args.hpp"

class PartArgs : public Args {
public:
    PartArgs(argparse::ArgumentParser& program):
    Args(program)
    {
        program.add_argument("-P", "--part-id")
            .help("Id of this partition")
            .required()
            .scan<'i', int>()
            ;
        program.add_argument("-T", "--end-time")
            .help("Time to end the simulation at")
            .default_value(100)
            .scan<'i', int>()
            ;

        printOnParse = false;
    }

    void parse_known_args(int argc, char* argv[]) {
        Args::parse_known_args(argc, argv);

        partId = program.get<int>("--part-id");
        endTime = program.get<int>("--end-time");
    }

    int partId;
    int endTime;
};