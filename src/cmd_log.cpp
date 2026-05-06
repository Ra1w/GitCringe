#include <set>
#include <vector>
#include "gitcringe.hpp"


int cmd_log(const std::set<char> &singles, const std::vector<std::string_view> &args)
{
    (void)singles;
    // bool one_line = std::find(args.begin(), args.end(), "--oneline") != args.end();
    // bool show_all = std::find(args.begin(), args.end(), "--all") != args.end();

    cringe::Repo repo(std::filesystem::current_path());
    
    cringe::Commit base = repo.GetHead();
    if (args.size() == 1)
    {
        base = repo.GetCommit(args[0]);
    }

    (void)args;

    /* create tree from strings */
    // std::map<int64_t, >
    // for ()

    return 0;
}