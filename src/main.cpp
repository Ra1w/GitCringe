#include "gitcringe.hpp"

#include <span>
#include <set>
#include <string>
#include <iostream>
#include <filesystem>
#include <cassert>
#include <print>


int main(int argc, const char **argv)
{
    std::set<char> singles;
    for (std::string_view arg : std::span(argv, argc).subspan(1))
    {
        if (arg.size() > 2 && arg[0] == '-' && arg[1] != '-')
        {
            for (char c : arg.substr(1))
            {
                singles.insert(c);
            }
        }
        else if (arg == "--help")
        {
            return cringe::help_fn();
        }
    }

    if (singles.contains('h'))
    {
        return cringe::help_fn();
    }

    /* parse cmd args */
    bool first = true;
    std::string_view command;
    std::vector<std::string_view> args;
    
    for (std::string_view arg : std::span(argv, argc).subspan(1))
    {
        if (arg.size() > 2 && arg[0] == '-' && arg[1] != '-') continue;

        if (first)
        {
            first = false;
            command = arg;
        }
        else
        {
            args.push_back(arg);
        }
    }

    if (command == "init")
    {
        return cringe::cmd_init(singles, args);
    }
    else if (command == "add")
    {
        return cringe::cmd_add(singles, args);
    }
    else if (command == "commit")
    {
        return cringe::cmd_commit(singles, args);
    }
    else if (command == "log")
    {
        return cringe::cmd_log(singles, args);
    }
}
