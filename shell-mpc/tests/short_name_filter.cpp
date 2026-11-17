// The names of the parameters the way the plugin hands them to the host (shell-mpc/host_strings.h, shortName): reads one full name a line from stdin, writes the shortened name a line.
// Used by shell-mpc/accept.sh to compare the parameter table of the built plugin with docs/params.json (the names of the table that do not fit the 31 characters of the host's buffer are shortened).
// Copyright (C) 2026 Safian Szabolcs
// SPDX-License-Identifier: AGPL-3.0-or-later
#include "host_strings.h"

#include <iostream>
#include <string>

int main()
{
    std::string line;
    while (std::getline (std::cin, line))
    {
        char out[synthyboy_mpc::kParamStrBytes];
        synthyboy_mpc::shortName (line.c_str(), out, sizeof out, synthyboy_mpc::kParamNameChars);
        std::cout << out << "\n";
    }
    return 0;
}
