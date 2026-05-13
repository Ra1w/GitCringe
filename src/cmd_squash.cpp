#include "gitcringe.hpp"
#include <set>
#include <print>


int cringe::cmd_squash(const std::set<char> &singles, const std::vector<std::string_view> &args)
{
    if (args.size() == 0)
    {
        std::println("Need at least one argument: child to squash.");
        return 1;
    }
    
    cringe::Repo repo(std::filesystem::current_path());


    auto result = repo.GetCommit(args[0]);
    if (result.size() > 1)
    {
        std::println("Call is ambigous between {} and {}.", result[0].GetId(), result[1].GetId());
        return 1;
    }
    if (result.size() == 0)
    {
        std::println("Cannot find any commit matches {}", args[0]);
        return 1;
    }
    
    cringe::Commit child = result[0];

    if (child.GetParents().size() == 1)
    {
        int64_t a, b;
        a = child.GetId();
        b = child.GetParents()[0].GetId();
        repo.Squash(child, child.GetParents()[0]);
        std::println("Commit {} was squashed with parent {}", a, b);
        return 0;
    }
    else
    {
        if (args.size() >= 2)
        {
            auto result2 = repo.GetCommit(args[0]);
            if (result2.size() > 1)
            {
                std::println("Call is ambigous between {} and {}.", result2[0].GetId(), result2[1].GetId());
                return 1;
            }
            if (result2.size() == 0)
            {
                std::println("Cannot find any commit matches {}", args[0]);
                return 1;
            }

            cringe::Commit parent = result2[0];
            
            int64_t a, b;
            a = child.GetId();
            b = parent.GetId();

            if (!child.IsDirectChildOf(parent))
            {
                std::println("Commit {} isn't direct parent of {}. Can not squash.", b, a);
                return 1;
            }
            
            repo.Squash(child, parent);
            std::println("Commit {} was squashed with parent {}", a, b);
            return 0;
        }
        else
        {
            std::println("This commit have many parents. Select one to squash with.");
            return 1;
        }
    }
    
    return 0;
}
