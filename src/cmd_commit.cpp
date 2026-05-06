#include "gitcringe.hpp"
#include <set>
#include <cassert>
#include <print>


int cmd_commit(const std::set<char> &singles, const std::vector<std::string_view> &args)
{
    (void)singles;
    (void)args;
    
    cringe::Repo repo(std::filesystem::current_path());
    
    // move index commit to branch
    // set index to null
    cringe::Commit head = repo.GetHead();
    cringe::Commit index = repo.GetIndex();
    // it is Index, so it's parent is 100% head.
    assert(index.IsDirectChildOf(head));
    repo.MoveHead(index);
    repo.UpdateIndex(std::nullopt);
    
    std::print("New commit with id {}\n", index.GetId());

    return 0;
}