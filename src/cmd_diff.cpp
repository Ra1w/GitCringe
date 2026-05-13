#include "gitcringe.hpp"
#include <set>
#include <vector>
#include <print>

#include <fstream>

void print_file_diff(const std::vector<std::string>& old_lines, const std::vector<std::string>& new_lines)
{
    size_t m = old_lines.size();
    size_t n = new_lines.size();

    std::vector<std::vector<size_t>> dp(m + 1, std::vector<size_t>(n + 1, 0));

    for (size_t i = 1; i <= m; ++i) {
        for (size_t j = 1; j <= n; ++j) {
            if (old_lines[i - 1] == new_lines[j - 1]) {
                dp[i][j] = dp[i - 1][j - 1] + 1;
            } else {
                dp[i][j] = std::max(dp[i - 1][j], dp[i][j - 1]);
            }
        }
    }

    struct DiffOp { char type; std::string_view line; };
    std::vector<DiffOp> diff_ops;

    size_t i = m, j = n;
    while (i > 0 || j > 0) {
        if (i > 0 && j > 0 && old_lines[i - 1] == new_lines[j - 1]) {
            diff_ops.push_back({' ', old_lines[i - 1]});
            i--; j--;
        } else if (j > 0 && (i == 0 || dp[i][j - 1] >= dp[i - 1][j])) {
            diff_ops.push_back({'+', new_lines[j - 1]});
            j--;
        } else {
            diff_ops.push_back({'-', old_lines[i - 1]});
            i--;
        }
    }

    const char* RED = "\033[31m";
    const char* GREEN = "\033[32m";
    const char* RESET = "\033[0m";

    for (auto it = diff_ops.rbegin(); it != diff_ops.rend(); ++it) {
        if (it->type == '+') {
            std::println("{}+ {}{}", GREEN, it->line, RESET);
        } else if (it->type == '-') {
            std::println("{}- {}{}", RED, it->line, RESET);
        } else {
            std::println("  {}", it->line);
        }
    }
}

std::vector<std::string> read_lines_from_ram(const std::string& content)
{
    std::vector<std::string> lines;
    std::string_view sv(content);
    size_t start = 0;

    while (start < sv.size()) 
    {
        size_t end = sv.find('\n', start);
        
        if (end == std::string_view::npos) 
        {
            lines.emplace_back(sv.substr(start));
            break;
        }

        size_t len = end - start;
        
        lines.emplace_back(sv.substr(start, len));

        start = end + 1;
    }
    return lines;
}

std::vector<std::string> read_lines_from_disk(const std::filesystem::path& path) 
{
    std::vector<std::string> lines;
    std::ifstream ifs(path);
    if (!ifs.is_open()) return lines;
    std::string line;
    while (std::getline(ifs, line)) 
    {
        lines.push_back(line);
    }
    return lines;
}


int cringe::cmd_diff(const std::set<char> &singles, const std::vector<std::string_view> &args)
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

    bool any = false;
    auto cwd = std::filesystem::current_path();
    for (auto [type, rpath] : repo.ListChangedFiles(commit))
    {   
        any = true;

        auto abs_path = std::filesystem::absolute(rpath).lexically_normal();
        auto display_path = abs_path.lexically_relative(cwd);
        auto path = display_path.empty() ? rpath.filename() : display_path;
        
        std::println("\n{}diff of commit/{} fs/{}{}", YELLOW, path.string(), path.string(), RESET);
        
        if (type == cringe::UPDATE_CREATE) 
        {
            std::println("{}+ {} (new file){}", GREEN, path.string(), RESET);
            
            std::vector<std::string> old_lines;
            std::vector<std::string> new_lines = read_lines_from_disk(repo.RootPath() / path); 
            
            print_file_diff(old_lines, new_lines);
        }
        else if (type == cringe::UPDATE_CHANGE) 
        {
            std::println("{}~ {} (modified){}", YELLOW, path.string(), RESET);
            
            std::vector<std::string> new_lines = read_lines_from_disk(repo.RootPath() / path);
            std::vector<std::string> old_lines = read_lines_from_ram(commit.GetFileContent(path));
            
            print_file_diff(old_lines, new_lines);
        }
        else if (type == cringe::UPDATE_DELETE) 
        {
            std::println("{}- {} (deleted){}", RED, path.string(), RESET);
            
            std::vector<std::string> old_lines = read_lines_from_ram(commit.GetFileContent(path));
            std::vector<std::string> new_lines;
            
            print_file_diff(old_lines, new_lines);
        }
    }
    if (!any)
    {
        std::println("No changes in system");
    }

    return 0;
}
