#include "gitcringe.hpp"
#include <set>
#include <print>

int cringe::cmd_exec(const std::set<char> &singles, const std::vector<std::string_view> &args)
{
    (void)singles;
    (void)args;

    cringe::Repo repo(std::filesystem::current_path());
    
    std::println("Starting request {}", args[0]);

    auto start = std::chrono::steady_clock::now();

    repo.Exec(std::string(args[0]));

    auto end = std::chrono::steady_clock::now();
    
    std::println("Done in {:.2f} seconds", (std::chrono::duration<double>{end - start}).count());
    
    return 0;
}
