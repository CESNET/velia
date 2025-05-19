/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#define BOOST_PROCESS_VERSION 1
#include <boost/algorithm/string/join.hpp>
#include <boost/process/v1/args.hpp>
#include <boost/process/v1/child.hpp>
#include <boost/process/v1/extend.hpp>
#include <boost/process/v1/io.hpp>
#include <boost/process/v1/pipe.hpp>
#include "exec.h"
#include "log.h"
#include "system_vars.h"

std::string velia::utils::execAndWait(
        velia::Log logger,
        const std::string& absolutePath,
        std::initializer_list<std::string> args,
        std::string_view std_in,
        const std::set<ExecOptions> opts)
{
    namespace bp = boost::process;
    bp::pipe stdinPipe;
    bp::ipstream stdoutStream;
    bp::ipstream stderrStream;

    auto onExecSetup = [opts] (const auto&) {
        if (opts.count(ExecOptions::DropRoot) == 1) {
            if (getuid() == 0) {
                if (setgid(NOBODY_GID) == -1) {
                    perror("couldn't drop root privileges");
                    exit(1);
                }

                if (setuid(NOBODY_UID) == -1) {
                    perror("couldn't drop root privileges");
                    exit(1);
                }
            }
        }
    };

    logger->trace("exec: {} {}", absolutePath, boost::algorithm::join(args, " "));
    bp::child c(
            absolutePath,
            boost::process::args=args,
            bp::std_in < stdinPipe, bp::std_out > stdoutStream, bp::std_err > stderrStream,
            bp::extend::on_exec_setup=onExecSetup);

    stdinPipe.write(std_in.data(), std_in.size());
    stdinPipe.close();

    c.wait();
    logger->trace("{} exited", absolutePath);

    if (c.exit_code()) {
        std::istreambuf_iterator<char> begin(stderrStream), end;
        std::string stderrOutput(begin, end);
        logger->critical("{} ended with a non-zero exit code. stderr: {}", absolutePath, stderrOutput);

        throw std::runtime_error(absolutePath + " returned non-zero exit code " + std::to_string(c.exit_code()));
    }

    std::istreambuf_iterator<char> begin(stdoutStream), end;
    return {begin, end};
}
