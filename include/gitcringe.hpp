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


namespace cringe
{
    enum GitCommandTypes
    {
        GitCommandInit,
        GitCommandCommit,
        GitCommandMerge,
    };

    class Repo;


    using commit_id_t = int64_t;


    class Commit
    {
        Repo &repo;
        commit_id_t id;
    public:
        Commit(Repo &repo, commit_id_t id);
        ~Commit();


        // restores File sytem to state of this commit
        void RestoreFS();

        bool IsChildOf(Commit other);

        bool IsDirectChildOf(Commit other);

        int64_t GetId();
        
        std::vector<Commit> GetParents();
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
        
        vector<std::filesystem::path> Transaction::GetDiffrentFiles()    
        
        int64_t GetFileId(PendingUpdate update);
            
    public:
        Transaction(Repo &repo, SQLite::Transaction tn);
        ~Transaction();

        // Applyes changes and returnining new commit
        Commit Apply();
        
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
        
        Repo(std::filesystem::path path);
        ~Repo();

        void CollectGarbage();

        // updates current index to this commit.
        bool UpdateIndex(std::optional<Commit> commit);
        
        bool UpdateHead(Commit commit);
        
        Commit GetIndex();
        
        Commit GetHead();

        // Returns from 0 to 2 commits.
        std::vector<Commit> GetCommit(std::string_view identifer);

        // Get root path
        std::filesystem::path RootPath() const;

        // Start new commit
        Transaction StartCommit();
    };

    int help_fn();
    int cmd_init(const std::set<char> &singles, const std::vector<std::string_view> &args);
    int cmd_add(const std::set<char> &singles, const std::vector<std::string_view> &args);
    int cmd_log(const std::set<char> &singles, const std::vector<std::string_view> &args);
    int cmd_commit(const std::set<char> &singles, const std::vector<std::string_view> &args);
}


#endif
