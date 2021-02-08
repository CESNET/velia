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
 * Spawns a new process with an executable specified by executableFilename and waits until it returns. stdout is thrown
 * away. Throws if the program has a non-zero exit code with a message containing the stderr of the process.
 *
 * @param logger Logger to use.
 * @param executableFilename Full path to the excutable.
 * @param args Arguments to pass to the program. Can be {} if no arguments should be passed.
 * @param std_in stdin input fo the program.
 */
enum class ExecOptions {
    DropRoot
};
void execAndWait(velia::Log logger, const std::string& executableFilename, std::initializer_list<std::string> args, std::string_view std_in, const std::set<ExecOptions> opts = {});
}
