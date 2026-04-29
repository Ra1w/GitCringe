#ifndef GITCRINGE_H
#define GITCRINGE_H


#include <iostream>
#include <span>
#include <string_view>
#include <vector>



namespace gcringe
{
    class Repo;


    using commit_id_t = int64_t;


    class Commit
    {
    public:
        Commit(Repo repo, commit_id_t id);
        ~Commit();


        // restores File sytem to state of this commit
        void RestoreFS();
    };

    class Transaction
    {
    public:
        Transaction(Repo repo);
        ~Transaction();

        // Applyes changes and returnining new commit
        Commit Apply();
        // Add changes
        void LoadFile(std::string path);
    };

    
    class Repo
    {
    public:
        
        Repo(std::string path);
        ~Repo();

        // Start new commit
        Transaction StartCommit(std::string path);
    };
}


#endif
