#include "gitcringe.hpp"
#include <set>
#include <print>

int cringe::cmd_branch(const std::set<char> &singles, const std::vector<std::string_view> &args)
{
    (void)singles;

    cringe::Repo repo(std::filesystem::current_path());

    const char* GREEN = "\033[32m";
    const char* RESET = "\033[0m";

    if (args.empty())
    {
        std::string current = repo.GetCurrentBranch();
        for (const auto& [name, cid] : repo.ListBranches())
        {
            if (name == current)
            {
                std::println("* {}{}{}", GREEN, name, RESET);
            }
            else
            {
                std::println("  {}", name);
            }
        }
    }
    else
    {
        std::string_view name = args[0];
        if (repo.CreateBranch(std::string(name), repo.GetHead()))
        {
            std::println("Branch '{}' created.", name);
        }
        else
        {
            std::println("Error: Branch '{}' already exists.", name);
            return 1;
        }
    }
    
    return 0;
}
