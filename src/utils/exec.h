/*
 * Copyright (C) 2021 CESNET, https://photonics.cesnet.cz/
 *
 * Written by Václav Kubernát <kubernat@cesnet.cz>
 *
*/

#pragma once
#include <boost/process.hpp>
#include <string>
#include "log-fwd.h"

namespace velia::utils {
/**
 * Spawns a new process and waits until it returns. stdout is thrown away. Throws if the program has a non-zero exit
 * code with a message containing the stderr of the process.
 *
 * @param logger Logger to use.
 * @param program The name of the program to spawn. PATH is searched for this program.
 * @param args Arguments to pass to the program. Can be {} if no arguments should be passed.
 * @param std_in stdin input fo the program.
 */
void execAndWait(velia::Log logger, const std::string& program, std::initializer_list<std::string> args, std::string_view std_in);
}
