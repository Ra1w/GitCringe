#include "gitcringe.hpp"
#include <set>
#include <cassert>
#include <print>


int cringe::cmd_commit(const std::set<char> &singles, const std::vector<std::string_view> &args)
{
    (void)singles;
    
    std::string_view message;

    auto it = std::find(args.begin(), args.end(), "-m");
    if (it != args.end() && ++it != args.end()) 
    {
        message = *it;
    }

    if (message.empty())
    {
        std::println("Error: commit message is required. Use -m <message>");
        return 1;
    }
    
    cringe::Repo repo(std::filesystem::current_path());
    
    cringe::Commit head = repo.GetHead();
    cringe::Commit index = repo.GetIndex();

    if (index.GetId() == 0)
    {
        std::println("Nothing to commit (index is empty). Use 'add' command first.");
        return 1;
    }

    // it is Index, so it's parent is 100% head.
    assert(index.IsDirectChildOf(head));

    index.UpdateMessage(std::string(message));

    repo.UpdateHead(index);
    repo.UpdateIndex(std::nullopt);
    
    std::println("New commit with id {}", index.GetId());

    return 0;
}
