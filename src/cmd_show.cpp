#include "gitcringe.hpp"
#include <set>
#include <vector>
#include <print>

int cringe::cmd_show(const std::set<char> &singles, const std::vector<std::string_view> &args)
{
    (void)singles;
    cringe::Repo repo(std::filesystem::current_path());

    std::string_view target = "HEAD";
    if (!args.empty()) 
    {
        target = args[0];
    }

    auto commits = repo.GetCommit(target);
    if (commits.empty())
    {
        std::println("Error: cannot find commit '{}'", target);
        return 1;
    }
    if (commits.size() > 1)
    {
        std::println("Error: ambiguous target '{}'. Multiple commits found.", target);
        return 1;
    }

    cringe::Commit commit = commits[0];

    std::string labels_str = "";
    std::vector<std::string> labels = commit.GetLabels();
    if (!labels.empty())
    {
        labels_str += " (";
        for (size_t i = 0; i < labels.size(); ++i)
        {
            labels_str += labels[i];
            if (i < labels.size() - 1) labels_str += ", ";
        }
        labels_str += ")";
    }

    const char* RED = "\033[31m";
    const char* GREEN = "\033[32m";
    const char* YELLOW = "\033[33m";
    const char* RESET = "\033[0m";

    std::println("{}commit {}{}{}", YELLOW, commit.GetId(), labels_str, RESET);
    std::println("Author: {}", commit.GetAuthor());
    std::println("\n    {}\n", commit.GetMessage());

    auto changes = commit.GetChanges();
    if (changes.empty())
    {
        std::println("No file changes in this commit.");
    }
    else
    {
        for (const auto& [type, path] : changes)
        {
            if (type == cringe::UPDATE_CREATE) 
            {
                std::println("{}+ {} (new){}", GREEN, path, RESET);
            }
            else if (type == cringe::UPDATE_CHANGE) 
            {
                std::println("{}~ {} (modified){}", YELLOW, path, RESET);
            }
            else if (type == cringe::UPDATE_DELETE) 
            {
                std::println("{}- {} (deleted){}", RED, path, RESET);
            }
        }
    }

    return 0;
}
