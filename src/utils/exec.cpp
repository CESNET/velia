/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include <boost/algorithm/string/join.hpp>
#include <boost/process.hpp>
#include <boost/process/extend.hpp>
#include "exec.h"
#include "log.h"
#include "system_vars.h"

void velia::utils::execAndWait(
        velia::Log logger,
        const std::string& program,
        std::initializer_list<std::string> args,
        std::string_view std_in,
        const std::set<ExecOptions> opts)
{
    namespace bp = boost::process;
    bp::pipe stdinPipe;
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

    logger->trace("exec: {} {}", program, boost::algorithm::join(args, " "));
    bp::child c(
            bp::search_path(program),
            boost::process::args=std::move(args),
            bp::std_in < stdinPipe, bp::std_out > bp::null, bp::std_err > stderrStream,
            bp::extend::on_exec_setup=onExecSetup);

    stdinPipe.write(std_in.data(), std_in.size());
    stdinPipe.close();

    c.wait();
    logger->trace("{} exited", program);

    if (c.exit_code()) {
        std::istreambuf_iterator<char> begin(stderrStream), end;
        std::string stderrOutput(begin, end);
        logger->critical("{} ended with a non-zero exit code. stderr: {}", program, stderrOutput);

        throw std::runtime_error(program + " returned non-zero exit code " + std::to_string(c.exit_code()));
    }
}
