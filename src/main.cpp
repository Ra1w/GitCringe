#include "gitcringe.hpp"

#include <span>
#include <set>
#include <ranges>
#include <string>
#include <iostream>
#include <filesystem>
#include <cassert>
#include <print>

int levenshtein(const std::string_view& s1, const std::string_view& s2) 
{
    std::vector<std::vector<int>> dp(s1.length() + 1, std::vector<int>(s2.length() + 1));

    for (int i = 0; i <= s1.length(); i++) dp[i][0] = i;
    for (int j = 0; j <= s2.length(); j++) dp[0][j] = j;

    for (int i = 1; i <= s1.length(); i++) 
    {
        for (int j = 1; j <= s2.length(); j++) 
        {
            dp[i][j] = std::min({dp[i - 1][j] + 1, 
                                 dp[i][j - 1] + 1, 
                                 dp[i - 1][j - 1] + (s1[i - 1] != s2[j - 1])});
        }
    }

    return dp[s1.length()][s2.length()];
}


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

    if (command == "help")
    {
        return cringe::help_fn();
    }
    else if (command == "init")
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
    else if (command == "exec")
    {
        return cringe::cmd_exec(singles, args);
    }
    else if (command == "status")
    {
        return cringe::cmd_status(singles, args);
    }
    else if (command == "branch")
    {
        return cringe::cmd_branch(singles, args);
    }
    else if (command == "switch")
    {
        return cringe::cmd_switch(singles, args);
    }
    else if (command == "merge")
    {
        return cringe::cmd_merge(singles, args);
    }
    else if (command == "squash")
    {
        return cringe::cmd_squash(singles, args);
    }
    else if (command == "show")
    {
        return cringe::cmd_show(singles, args);
    }
    else
    {
        std::vector<std::string> known_commands = {
            "init",
            "add",
            "commit",
            "log",
            "status",
            "exec",
            "help",
            "switch",
            "merge",
            "branch",
            "squash",
            "show"
        };
        std::sort(known_commands.begin(), known_commands.end(), 
             [&command](const std::string& a, const std::string& b) {
                 return levenshtein(a, command) < levenshtein(b, command);
             }
         );
        std::println("Unknown command: {}", command);
        std::print("May be you mean ");
        for (int i = 0; i < known_commands.size() && i < 2; ++i)
        {
            std::print("\"{}\"{}", known_commands[i], (i + 1 != 2 ? " or ":""));
        }
        std::print("?\n");
        return 1;
    }
}
