#include "gitcringe.hpp"
#include <set>
#include <vector>
#include <print>
#include <fstream>
#include <filesystem>

struct FileStream 
{
    int64_t id;
    std::vector<std::string> lines;
    size_t cursor = 0;
};

std::vector<std::string> SplitLinesView(std::string_view text) 
{
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) 
    {
        size_t end = text.find('\n', start);
        if (end == std::string_view::npos) 
        {
            lines.push_back(std::string(text.substr(start)));
            break;
        }
        lines.emplace_back(std::string(text.substr(start, end - start)));
        start = end + 1;
    }
    return lines;
}

std::pair<std::string, int64_t> MergeMultipleStreams(std::vector<FileStream>& streams) 
{
    std::string merged_result;
    const size_t LOOKAHEAD = 100;
    int64_t conflicts = 0;

    while (true) 
    {
        std::vector<size_t> active_indices;
        for (size_t i = 0; i < streams.size(); ++i) 
        {
            if (streams[i].cursor < streams[i].lines.size()) 
            {
                active_indices.push_back(i);
            }
        }

        if (active_indices.empty()) 
        {
            break;
        }

        bool all_match = true;
        std::string_view first_line = streams[active_indices[0]].lines[streams[active_indices[0]].cursor];
        for (size_t idx : active_indices) 
        {
            if (streams[idx].lines[streams[idx].cursor] != first_line) 
            {
                all_match = false;
                break;
            }
        }

        if (all_match || active_indices.size() == 1) 
        {
            merged_result += std::string(first_line) + "\n";
            for (size_t idx : active_indices) 
            {
                streams[idx].cursor++;
            }
            continue;
        }

        size_t best_match_count = 0;
        std::string_view best_anchor = "";
        
        for (size_t idx : active_indices) 
        {
            size_t limit = std::min(streams[idx].lines.size(), streams[idx].cursor + LOOKAHEAD);
            for (size_t s = streams[idx].cursor; s < limit; ++s) 
            {
                std::string_view candidate = streams[idx].lines[s];
                
                size_t match_count = 0;
                for (size_t other_idx : active_indices) 
                {
                    size_t other_limit = std::min(streams[other_idx].lines.size(), streams[other_idx].cursor + LOOKAHEAD);
                    for (size_t o = streams[other_idx].cursor; o < other_limit; ++o) 
                    {
                        if (streams[other_idx].lines[o] == candidate) 
                        {
                            match_count++;
                            break;
                        }
                    }
                }
                
                if (match_count > best_match_count) 
                {
                    best_match_count = match_count;
                    best_anchor = candidate;
                }
            }
        }

        merged_result += "===>>> START CRINGE MERGE CONFLICT <<<===\n";
        conflicts++;
        
        std::vector<size_t> advance_targets(streams.size(), 0);
        
        for (size_t idx : active_indices) 
        {
            size_t advance_to = streams[idx].cursor + 1;
            
            if (best_match_count > 1) 
            {
                size_t limit = std::min(streams[idx].lines.size(), streams[idx].cursor + LOOKAHEAD);
                for (size_t s = streams[idx].cursor; s < limit; ++s) 
                {
                    if (streams[idx].lines[s] == best_anchor) 
                    {
                        advance_to = s;
                        break;
                    }
                }
            }
            advance_targets[idx] = advance_to;
        }

        for (size_t idx : active_indices) 
        {
            size_t advance_to = advance_targets[idx];
            if (streams[idx].cursor < advance_to) 
            {
                merged_result += std::format("===>>> Changes from commit: {}\n", streams[idx].id);
                while (streams[idx].cursor < advance_to) 
                {
                    merged_result += std::string(streams[idx].lines[streams[idx].cursor]) + "\n";
                    streams[idx].cursor++;
                }
            }
        }
        
        merged_result += "===>>> END N-WAY MERGE CONFLICT <<<===\n";
    }

    return {merged_result, conflicts};
}

int cringe::cmd_merge(const std::set<char> &singles, const std::vector<std::string_view> &args)
{
    (void)singles;

    if (args.empty())
    {
        std::println("Error: at least one target commit or label required.");
        return 1;
    }

    cringe::Repo repo(std::filesystem::current_path());
    cringe::Transaction trn = repo.StartCommit();
    
    cringe::Commit current_head = repo.GetHead();
    trn.AddParent(current_head);

    for (auto arg : args)
    {
        std::string_view target = arg;

        auto commits = repo.GetCommit(target);
        if (commits.empty())
        {
            std::println("Error: cannot find commit or label '{}'", target);
            return 1;
        }
        if (commits.size() > 1)
        {
            std::println("Error: ambiguous target '{}'. Between commits with id {} and {}.", target, commits[0].GetId(), commits[1].GetId());
            return 1;
        }
        
        trn.AddParent(commits[0]);
    }

    int64_t total_conflicts = 0;

    for (auto path : trn.GetDiffrentFiles())
    {
        std::println("file {} is different in some parents, trying to merge it automatically...", path);
        std::vector<std::pair<int64_t, std::string>> content;
        for (const auto &[id, in_fs, data] : trn.GetFileVersions(path))
        {
            if (in_fs)
            {
                std::println("Can't merge file from LFS for now.");
                return 1;
            }
            content.emplace_back(id, data);
        }
        
        std::vector<FileStream> streams;
        streams.reserve(content.size());
        
        for (const auto& [id, data] : content) 
        {
            streams.push_back({id, SplitLinesView(data), 0});
        }

        auto [merged_content, conflicts] = MergeMultipleStreams(streams);
        
        std::filesystem::path abs_path = repo.RootPath() / path;
        
        {
            std::ofstream out_file(abs_path, std::ios::binary);
            out_file << merged_content;
        }
        
        std::println("File {} automatically merged. Occurred {} merge conflicts", path, conflicts);
        total_conflicts += conflicts;

        trn.LoadFile(abs_path);
    }

    std::println("\n\nTotal {} conflicts occured", total_conflicts);
    std::println("Use gitcringe commit then you will be ready to confirm changes");

    cringe::Commit new_index = trn.Apply("<merge-index>");
    std::println("Created merge commit with id {}", new_index.GetId());

    repo.UpdateIndex(new_index);

    return 0;
}
