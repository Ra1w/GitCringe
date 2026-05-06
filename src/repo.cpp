#include "gitcringe.hpp"

#include <vector>
#include <string>
#include <filesystem>
#include <print>
#include <cinttypes>

namespace cringe
{
    Repo::Repo(std::filesystem::path)
        try : db(path.u8string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE) 
        { } 
        catch (std::exception& e) 
        {
            std::print("Error: can't open database: {}", e.what());
            throw;
        },
        root(path),
        fs_storage_path(path/".cvcs"/"fs")
    {
        db.exec("PRAGMA foreign_keys = ON;");
        db.exec("PRAGMA case_sensitive_like = OFF;");
        db.exec(R"Request(
            CREATE TABLE IF NOT EXISTS blobs 
            (   
                id INTEGER PRIMARY KEY, 
                data BLOB
            );
            CREATE TABLE IF NOT EXISTS files
            (
                id INTEGER PRIMARY KEY, 
                hash BLOB(32) UNIQUE, 
                in_filesystem BOOL, 
                blob_id INTEGER REFERENCES blobs(id), 
                filesystem_path TEXT
            );
            CREATE TABLE IF NOT EXISTS commits 
            (
                id INTEGER PRIMARY KEY, 
                hash BLOB(32), 
                author TEXT,
                message TEXT
            );
            CREATE TABLE IF NOT EXISTS commit_links 
            (
                commit_id INTEGER REFERENCES commits(id) ON DELETE CASCADE, 
                parent_id INTEGER REFERENCES commits(id) ON DELETE CASCADE, 
                PRIMARY KEY (commit_id, parent_id)
            );
            CREATE TABLE IF NOT EXISTS labels
            (
                commit_id INTEGER REFERENCES commits(id) ON DELETE CASCADE,
                name TEXT, 
                PRIMARY KEY (commit_id, name)
            );
            CREATE TABLE IF NOT EXISTS commit_fs 
            (
                commit_id INTEGER REFERENCES commits(id) ON DELETE CASCADE, 
                path TEXT, 
                file_id INTEGER REFERENCES files(id)
                PRIMARY KEY (commit_id, path)
            );
            CREATE TABLE IF NOT EXISTS head 
            (
                id INTEGER PRIMARY KEY CHECK (id = 1), -- Only one entry
                commit_id INTEGER REFERENCES commits(id)
            );
            CREATE TABLE IF NOT EXISTS index
            (
                id INTEGER PRIMARY KEY CHECK (id = 1), -- Only one entry
                commit_id INTEGER REFERENCES commits(id)
            );
        )Request");
    }

    Repo::~Repo() = default;

    bool Repo::UpdateIndex(optional<Commit> commit)
    {
        SQLite::Statement query(db, R"Request(
            UPDATE "index" SET commit_id = ? WHERE id = 1
        )Request");
        query.bind(1, commit.GetId());
        return query.executeStep();
    }

    boool Repo::UpdateHead(Commit commit)
    {
        SQLite::Statement query(db, R"Request(
            UPDATE "head" SET commit_id = ? WHERE id = 1
        )Request");
        query.bind(1, head.GetId());
        return query.executeStep();
    }

    Commit Repo::GetIndex(Commit commit)
    {
        SQLite::Statement query(db, R"Request(
            SELECT commit_id FROM "index" WHERE id = 1
        )Request");
        if (query.executeStep()) 
        {
            return Commit(*this, query.getColumn(0).getInt64());
        }
    }

    Commit Repo::GetHead(Commit commit)
    {
        SQLite::Statement query(db, R"Request(
            SELECT commit_id FROM "head" WHERE id = 1
        )Request");
        if (query.executeStep()) 
        {
            return Commit(*this, query.getColumn(0).getInt64());
        }
    }

    std::optional<Commit> Repo::GetCommit(std::string_view identifer)
    {
        std::string argument = "*" + std::string(identifer) + "*";
        
        SQLite::Statement query(db, R"Request(
            SELECT commit_id FROM "labels" WHERE name like ?
        )Request");
        query.bind(1, argument);
        if (query.executeStep()) 
        {
            return Commit(*this, query.getColumn(0).getInt64());
        }
    }
    
    std::vector<Commit> Repo::GetCommit(std::string_view identifier) 
    {
        std::string argument = "%" + std::string(identifier) + "%";
        
        SQLite::Statement query(db, R"Request(
            SELECT commit_id, name FROM "labels" WHERE name LIKE ? LIMIT 2
        )Request");
        query.bind(1, argument);

        if (!query.executeStep()) 
        {
            return {};
        }
        int64_t first_id = query.getColumn(0).getInt64();
        if (query.getColumn(1).getString() == identifier)
        {
            return {Commit(*this, first_id)};
        }
        if (query.executeStep()) 
        {
            return {Commit(*this, first_id), Commit(*this, query.getColumn(0).getInt64())};
        }
        return {Commit(*this, first_id)};
    }

    std::filesystem::path RootPath() const
    {
        return root;
    }

    Transaction Repo::StartCommit()
    {
        SQLite::Transaction tn(db);
        return Transaction(*this, tn);
    }

    Repo::CollectGarbage()
    {
        db.exec(R"Request(
            
        )Request")
    }

    Transaction::Transaction(Repo &repo, SQLite::Transaction tn)
        : repo(repo), tn(tn)
    {
        
    }

    Transaction::~Transaction() = default;

    void Transaction::AddParent(Commit commit)
    {
        parents.push_back(commit);
    }

    void Transaction::LoadFile(std::filesystem::path path)
    {
        // calculate hash
        std::array<uint8_t, 32> hash = 0;
        // TODO: this
        
        // insert file entry
        files.emplace_back(hash, 
                           (std::filesystem::exists(path) ? PendingUpdateActionData
                                                          : PendingUpdateActionDelete), 
                           path);
    }

    int64_t Transaction::GetFileId(Transaction::PendingUpdate update)
    {
        // is there file with same content
        SQLite::Statement query(db, R"Request(
            SELECT id FROM "files" WHERE hash = ?
        )Request");
        query.bind(1, update.hash);
        if (query.executeStep())
        {
            // found same file, use it's id
            return Commit(*this, query.getColumn(0).getInt64());
        }

        // need to create new file.
        uintmax_t size = std::filesystem::file_size(update.path);
        if (size > Repo.Config.FilesystemSizeThreshold)
        {
            // copy and compress file to directory
            // TODO copy
            std::filesystem::path result_path = "...";
            
            // insert new entry into files
            SQLite::Statement query(db, R"Request(
                INSERT INTO files (hash, in_filesystem, blob_id, filesystem_path) VALUES (?, true, null, ?)
            )Request");
            query.bind(1, update.hash.data(), update.hash.size());
            query.bind(2, result_path);
            query.exec();
            return db.getLastInsertRowid();
        }
        else
        {
            // read file
            std::ifstream file(filePath, std::ios::binary);
            if (!file.is_open()) 
            {
                throw exception("Can't open file.");
            }
            std::vector<std::byte> buffer(size);
            if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) 
            {
                throw exception("Can't read file.");
            }
            // insert new entry into blobs
            SQLite::Statement queryBlob(db, R"Request(
                INSERT INTO "blobs" (data) VALUES (?)
            )Request");
            queryBlob.bind(1, buffer.data(), static_cast<int>(buffer.size()));
            queryBlob.exec();
            int64_t blob_id = db.getLastInsertRowid();
            // insert new entry into files
            SQLite::Statement query(db, R"Request(
                INSERT INTO files (hash, in_filesystem, blob_id, filesystem_path) VALUES (?, false, ?, null)
            )Request");
            query.bind(1, update.hash.data(), update.hash.size());
            query.bind(2, blob_id);
            query.exec();
            return db.getLastInsertRowid();
        }
    }

    vector<std::filesystem::path> Transaction::GetDiffrentFiles()
    {
        if (parents.size() <= 1) 
        {
            return {};
        }
    
        std::string queryParams = "?";
        for (size_t i = 1; i < parents.size(); ++i) 
        {
            queryParams += ", ?";
        }
    
        SQLite::Statement query(db, R"Request(
            SELECT path 
            FROM commit_fs 
            WHERE commit_id IN ()Request" + queryParams + R"Request() 
            GROUP BY path
            HAVING MIN(file_id) != MAX(file_id) -- if there is 2 diffrent file_id's
                   OR COUNT(commit_id) < ?      -- or if in some parents id isn't presented
        )Request");
        
        int bindIndex = 1;
        for (const Commit &id : parentIds) 
        {
            query.bind(bindIndex++, id.GetId());
        }
        
        query.bind(bindIndex, parentIds.size());
    
        std::vector<std::filesystem::path> changedPaths;
        changedPaths.reserve(32); // 32 files is enough for little changes
        while (query.executeStep()) 
        {
            changedPaths.push_back(query.getColumn(0).getText());
        }
        return changedPaths;
    }

    Commit Transaction::Apply()
    {
        // 1. Ensure, that all diffrent files was added
        for (auto path : GetDiffrentFiles())
        {
            // TODO:
        }
        
        // inset new commit id
        SQLite::Statement query(db, R"Request(
            INSERT INTO "commits" (hash, autor, message) VALUES (?, ?, ?)
        )Request");
        queryBlob.bind(1, buffer.data(), static_cast<int>(buffer.size()));
        queryBlob.bind(2, autorName);
        queryBlob.bind(3, commitMessage);
        queryBlob.exec();
        int64_t blob_id = db.getLastInsertRowid();

        tn.commit();
    }
    
        class Transaction
        {
            Repo &repo;
            SQLite::Transaction tn;
        public:
            Transaction(Repo &repo, SQLite::Transaction tn);
            ~Transaction();
    
            // Applyes changes and returnining new commit
            Commit Apply();
            // Add changes
            void LoadFile(std::filesystem::path path);
        };
}
