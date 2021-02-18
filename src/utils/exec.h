/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#pragma once
#include <set>
#include <string>
#include "log-fwd.h"

namespace velia::utils {
/**
 * Spawns a new process with an executable specified by `absolutePath` and waits until it returns. The return value is
 * stdout of the process. Throws if the program has a non-zero exit code with a message containing the stderr of the
 * process.
 *
 * @param logger Logger to use.
 * @param absolutePath Full path to the excutable.
 * @param args Arguments to pass to the program. Can be {} if no arguments should be passed.
 * @param std_in stdin input fo the program.
 * @return stdout of the command
 */
enum class ExecOptions {
    DropRoot
};
std::string execAndWait(velia::Log logger, const std::string& absolutePath, std::initializer_list<std::string> args, std::string_view std_in, const std::set<ExecOptions> opts = {});
}
