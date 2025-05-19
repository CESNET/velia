/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#include <boost/algorithm/string/join.hpp>
#include <boost/asio.hpp>

#if BOOST_VERSION >= 108800
#include <boost/process.hpp>
namespace bp = boost::process;
#else
#include <boost/process/v2/process.hpp>
namespace bp = boost::process::v2;
#endif

#include "exec.h"
#include "log.h"
#include "system_vars.h"

namespace {
std::string readPipe(velia::Log logger, boost::asio::readable_pipe& pipe)
{
    boost::system::error_code ec;
    std::string str;

    auto sz = boost::asio::read(pipe, boost::asio::dynamic_buffer(str), ec);
    if (ec && ec != boost::asio::error::eof) {
        throw std::runtime_error("Failed to read from pipe: " + ec.message());
    }

    logger->trace("read {} bytes from pipe", sz);
    return str;
}
}

std::string velia::utils::execAndWait(
        velia::Log logger,
        const std::string& absolutePath,
        std::initializer_list<std::string> args,
        std::string_view std_in,
        const std::set<ExecOptions> opts)
{
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

    boost::asio::io_context io;
    boost::asio::writable_pipe stdinPipe{io};
    boost::asio::readable_pipe stdoutPipe{io};
    boost::asio::readable_pipe stderrPipe{io};

    bp::process proc(
            io,
            absolutePath,
            args,
            bp::process_stdio{stdinPipe, stdoutPipe, stderrPipe},
            onExecSetup);

    boost::system::error_code ec;
    boost::asio::write(stdinPipe, boost::asio::buffer(std_in.data(), std_in.size()), ec);

    auto stdoutContent = readPipe(logger, stdoutPipe);
    auto stderrContent = readPipe(logger, stderrPipe);

    proc.wait();
    logger->trace("{} exited", absolutePath);

    if (proc.exit_code()) {
        logger->critical("{} ended with a non-zero exit code. stderr: {}", absolutePath, stderrContent);
        throw std::runtime_error(absolutePath + " returned non-zero exit code " + std::to_string(proc.exit_code()));
    }

    return stdoutContent;
}
