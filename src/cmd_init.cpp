#include "gitcringe.hpp"
#include <set>
#include <print>

int cmd_init(const std::set<char> &singles, const std::vector<std::string_view> &args)
{
    (void)singles;
    (void)args;
    
    cringe::Repo repo(std::filesystem::current_path());    
    cringe::Transaction trn = repo.StartCommit();
    cringe::Commit commit = trn.Apply();

    std::print("Init repository. Top commit id is {}", commit.GetId());
    
    return 0;
}