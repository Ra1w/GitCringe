#include "gitcringe.hpp"
#include <set>
#include <cassert>
#include <print>
#include <filesystem>
#include <fstream>


std::string get_linux_editor() 
{
    if (const char* cri_editor = std::getenv("CRINGE_EDITOR")) return cri_editor;
    if (const char* visual     = std::getenv("VISUAL"))        return visual;
    if (const char* editor     = std::getenv("EDITOR"))        return editor;
    return "vi"; 
}

std::string request_message_from_editor() 
{
    std::string editor = get_linux_editor();
    
    std::filesystem::path temp_file = std::filesystem::temp_directory_path() / "CRINGE_COMMIT_EDITMSG";
    
    std::ofstream ofs(temp_file);
    if (!ofs.is_open()) return "";
    
    ofs << "\n"
        << "# Please enter the cringe commit message for your changes. Lines starting\n"
        << "# with '#' will be ignored, and an empty message aborts the commit.\n";
    ofs.close();

    std::string command = editor + " \"" + temp_file.string() + "\"";
    
    int status = std::system(command.c_str());
    
    if (status == -1 || !WIFEXITED(status) || WEXITSTATUS(status) != 0) 
    {
        std::println(std::cerr, "Error: Editor exited with an error.");
        std::filesystem::remove(temp_file);
        return "";
    }

    std::ifstream ifs(temp_file);
    if (!ifs.is_open()) return "";

    std::string line;
    std::string cleaned_content;
    
    while (std::getline(ifs, line)) 
    {
        if (!line.empty() && line[0] == '#') continue;
        cleaned_content += line + "\n";
    }
    ifs.close();
    std::filesystem::remove(temp_file);

    auto start = cleaned_content.find_first_not_of(" \t\r\n");
    auto end   = cleaned_content.find_last_not_of(" \t\r\n");
    
    return (start == std::string::npos) ? "" : cleaned_content.substr(start, end - start + 1);
}

int cringe::cmd_commit(const std::set<char> &singles, const std::vector<std::string_view> &args)
{
    (void)singles;
    
    std::string message;

    auto it = std::find(args.begin(), args.end(), "-m");
    if (it != args.end() && ++it != args.end()) 
    {
        message = *it;
    }

    if (message.empty())
    {
        /* request text from shell */
        message = request_message_from_editor();
        std::println("Using message {}", message);
        
        if (message.empty())
        {
            std::println("Error: commit message is required. Use -m <message>");
            return 1;
        }
    }
    
    cringe::Repo repo(std::filesystem::current_path());
    
    cringe::Commit head = repo.GetHead();
    cringe::Commit index = repo.GetIndex();

    if (index.GetId() == 0 || index.GetId() == head.GetId())
    {
        std::println("Nothing to commit. Use 'add' command first.");
        return 1;
    }

    // it is Index, so it's parent is 100% head.
    assert(index.IsDirectChildOf(head));

    index.UpdateMessage(message);

    repo.UpdateHead(index);
    repo.UpdateIndex(std::nullopt);
    
    std::println("New commit with id {}", index.GetId());

    return 0;
}
