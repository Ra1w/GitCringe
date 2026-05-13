#include "gitcringe.hpp"
#include <set>
#include <vector>
#include <print>
#include <filesystem>

int cringe::cmd_switch(const std::set<char> &singles, const std::vector<std::string_view> &args)
{
    (void)singles;

    if (args.empty())
    {
        std::println("Error: target commit or label required.");
        return 1;
    }

    std::string_view target = args[0];
    cringe::Repo repo(std::filesystem::current_path());

    auto commits = repo.GetCommit(target);
    if (commits.empty())
    {
        std::println("Error: cannot find commit or label '{}'", target);
        return 1;
    }
    if (commits.size() > 1)
    {
        std::println("Error: ambiguous target '{}'. Beetween commits with id {} and {}.", target, commits[0].GetId(), commits[1].GetId());
        return 1;
    }

    cringe::Commit new_head = commits[0];
    cringe::Commit current_head = repo.GetHead();

    if (new_head.GetId() == current_head.GetId())
    {
        std::println("Already on '{}'", target);
        return 0;
    }

    if (repo.GetIndex().GetId() != 0 && repo.GetIndex().GetId() != current_head.GetId())
    {
        std::println("Error: you have staged changes in the index. Commit them first.");
        return 1;
    }

    std::vector<std::string> new_files = new_head.ListFiles();
    std::set<std::string> new_files_set(new_files.begin(), new_files.end());
    
    bool has_conflicts = false;
    for (auto [type, path] : repo.ListChangedFiles(current_head))
    {
        if (type == cringe::UPDATE_CHANGE || type == cringe::UPDATE_DELETE)
        {
            has_conflicts = true;
            break;
        }
        else if (type == cringe::UPDATE_CREATE)
        {
            std::string rel_path = std::filesystem::absolute(path).lexically_normal().lexically_relative(repo.RootPath()).string();
            if (new_files_set.contains(rel_path))
            {
                has_conflicts = true;
                break;
            }
        }
    }

    if (has_conflicts)
    {
        std::println("Error: you have uncommitted changes that would be overwritten. Please commit them first.");
        return 1;
    }

    std::set<std::string> all_affected_files;
    if (current_head.GetId() != 0)
    {
        for (const std::string& f : current_head.ListFiles()) 
        {
            all_affected_files.insert(f);
        }
    }
    for (const std::string& f : new_files) 
    {
        all_affected_files.insert(f);
    }

    for (const std::string& f : all_affected_files)
    {
        new_head.RestoreFile(repo.RootPath() / f);
    }

    bool is_branch = false;
    for (const auto& [b_name, b_id] : repo.ListBranches())
    {
        if (b_name == target) 
        {
            is_branch = true;
            break;
        }
    }

    repo.DetachHead();

    repo.UpdateHead(new_head);
    repo.UpdateIndex(std::nullopt);

    if (is_branch)
    {
        repo.AttachHead(std::string(target));
        std::println("Switched to branch '{}'", target);
    }
    else
    {
        repo.DetachHead();
        std::println("Switched to commit {} (detached HEAD)", new_head.GetId());
    }

    return 0;
}
