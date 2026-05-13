#ifndef GITCRINGE_H
#define GITCRINGE_H


#include <SQLiteCpp/SQLiteCpp.h>

#include <iostream>
#include <span>
#include <set>
#include <string_view>
#include <vector>
#include <filesystem>
#include <optional>
#include <generator>


namespace cringe
{
    class CringeError : public std::runtime_error 
    {
    public:
        std::vector<std::string> user_info;
        CringeError(const std::string& msg, std::vector<std::string> info) 
            : std::runtime_error(msg), 
              user_info(std::move(info))
        {}
    };


    enum UpdateTypes
    {
        UPDATE_CREATE,
        UPDATE_CHANGE,
        UPDATE_DELETE,
    };
    

    class Repo;


    using commit_id_t = int64_t;


    class Commit
    {
        Repo &repo;
        commit_id_t id;
    public:
        Commit(Repo &repo, commit_id_t id);
        Commit(const Commit& other);
        Commit(Commit&& other) noexcept;        
        
        ~Commit();

        std::vector<std::string> ListFiles();

        bool IsChildOf(Commit other);

        bool IsDirectChildOf(Commit other);

        commit_id_t GetId() const;

        std::string GetAuthor() const;
        
        std::string GetMessage() const;

        bool UpdateMessage(const std::string& message);

        std::vector<std::string> GetLabels() const;

        std::vector<Commit> GetParents();

        std::vector<std::pair<UpdateTypes, std::string>> GetChanges();

        // restores File to state of this commit
        bool RestoreFile(std::filesystem::path);
    };

    enum PendingUpdateAction
    {
        PendingUpdateActionData,
        PendingUpdateActionDelete,
    };
    
    class Transaction
    {
        struct PendingUpdate
        {
            std::array<uint8_t, 32> hash;
            PendingUpdateAction action;
            std::filesystem::path file;
        };
        
        Repo &repo;
        SQLite::Transaction tn;
        std::vector<PendingUpdate> updates;
        std::vector<Commit> parents;
        std::string authorName;
        
        int64_t GetFileId(PendingUpdate update);
            
    public:
        Transaction(Repo &repo, std::string authorName);
        ~Transaction();
        
        std::generator<std::string> GetDiffrentFiles();
        std::generator<std::pair<int64_t, std::string>> GetCommonFiles();
        std::vector<std::tuple<commit_id_t, bool, std::string>> GetFileVersions(std::string path);

        // Applyes changes and returnining new commit
        Commit Apply(std::string commitMessage);
        
        // Add changes
        void LoadFile(std::filesystem::path path);

        // Add parents
        void AddParent(Commit commit);
    };

    
    class Repo
    {
        SQLite::Database db;
        std::filesystem::path root;
        std::filesystem::path fs_storage_path;

        struct Configuration
        {
            uintmax_t FilesystemSizeThreshold = 1024*1024; // if file > this size, it will be stored in fs
        };
    public:

        void Exec(std::string command);

        Configuration Config;
        
        Repo(std::filesystem::path path);
        ~Repo();

        void CollectGarbage();

        bool CreateBranch(std::string_view name, Commit commit);
        
        std::vector<std::pair<std::string, commit_id_t>> ListBranches();
        
        std::string GetCurrentBranch();
        
        bool AttachHead(std::string_view branch_name);
        
        bool DetachHead();

        // updates current index to this commit.
        bool UpdateIndex(std::optional<Commit> commit);
        
        bool UpdateHead(Commit commit);
        
        void Squash(Commit child, Commit parent_to_drop);

        Commit GetIndex();
        
        Commit GetHead();

        // Returns from 0 to 2 commits.
        std::vector<Commit> GetCommit(std::string_view identifer);

        std::vector<Commit> GetReferences();

        std::vector<std::pair<UpdateTypes, std::filesystem::path>> ListChangedFiles(Commit commit);

        // Get root path
        std::filesystem::path RootPath();

        // Start new commit
        Transaction StartCommit();

        friend class Transaction;
        friend class Commit;
    };

    int help_fn();
    
    int cmd_exec(const std::set<char> &singles, const std::vector<std::string_view> &args);
    int cmd_init(const std::set<char> &singles, const std::vector<std::string_view> &args);
    int cmd_add(const std::set<char> &singles, const std::vector<std::string_view> &args);
    int cmd_log(const std::set<char> &singles, const std::vector<std::string_view> &args);
    int cmd_commit(const std::set<char> &singles, const std::vector<std::string_view> &args);
    int cmd_status(const std::set<char> &singles, const std::vector<std::string_view> &args);
    int cmd_switch(const std::set<char> &singles, const std::vector<std::string_view> &args);
    int cmd_branch(const std::set<char> &singles, const std::vector<std::string_view> &args);
    int cmd_merge(const std::set<char> &singles, const std::vector<std::string_view> &args);
    int cmd_show(const std::set<char> &singles, const std::vector<std::string_view> &args);
    int cmd_squash(const std::set<char> &singles, const std::vector<std::string_view> &args);
}


#endif
