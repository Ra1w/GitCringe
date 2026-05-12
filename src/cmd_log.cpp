#include <set>
#include <vector>
#include <print>
#include <queue>
#include <unordered_set>
#include "gitcringe.hpp"


int cringe::cmd_log(const std::set<char> &singles, const std::vector<std::string_view> &args)
{
    (void)singles;
    bool one_line = std::find(args.begin(), args.end(), "--oneline") != args.end();
    bool show_all = std::find(args.begin(), args.end(), "--all") != args.end();

    cringe::Repo repo(std::filesystem::current_path());
    
    std::optional<cringe::Commit> base = repo.GetHead();

    std::string_view target_commit = "";
    for (std::string_view arg : args) 
    {
        if (!arg.starts_with("--")) 
        {
            target_commit = arg;
            break;
        }
    }

    if (!target_commit.empty())
    {
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
        base.emplace(result[0]);
    }

    /* create tree from strings */
    
    std::priority_queue<int64_t> pq;
    std::unordered_set<int64_t> visited;

    if (show_all)
    {
        for (cringe::Commit& ref : repo.GetReferences())
        {
            if (ref.GetId() != 0 && visited.insert(ref.GetId()).second)
            {
                pq.push(ref.GetId());
            }
        }
    }
    else if (base.has_value() && base->GetId() != 0)
    {
        pq.push(base->GetId());
        visited.insert(base->GetId());
    }

    if (pq.empty())
    {
        std::println("No commits found.");
        return 1;
    }

    while (!pq.empty())
    {
        int64_t current_id = pq.top();
        pq.pop();

        cringe::Commit current(repo, current_id);

        std::string author = current.GetAuthor();
        std::string msg = current.GetMessage();

        std::string labels_str = "";
        std::vector<std::string> labels = current.GetLabels();
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

        if (one_line)
        {
            std::string first_line = msg;
            size_t nl = msg.find('\n');
            if (nl != std::string::npos) 
            {
                first_line = msg.substr(0, nl);
            }
            std::println("{}{} {}", current_id, labels_str, first_line);
        }
        else
        {
            std::println("commit {}{}", current_id, labels_str);
            std::println("Author: {}", author);
            std::println("");
            
            size_t start = 0;
            size_t end = msg.find('\n');
            while (end != std::string::npos) 
            {
                std::println("    {}", msg.substr(start, end - start));
                start = end + 1;
                end = msg.find('\n', start);
            }
            std::println("    {}", msg.substr(start));
            std::println("");
        }

        for (cringe::Commit& parent : current.GetParents())
        {
            if (parent.GetId() != 0 && visited.insert(parent.GetId()).second)
            {
                pq.push(parent.GetId());
            }
        }
    }

    return 0;
}
