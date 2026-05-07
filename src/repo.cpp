#include "gitcringe.hpp"

#include <vector>
#include <string>
#include <filesystem>
#include <print>
#include <cinttypes>
#include <stdexcept>
#include <generator>

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
        root(path.lexically_normal()),
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
            UPDATE index SET commit_id = ? WHERE id = 1
        )Request");
        query.bind(1, commit.GetId());
        return query.executeStep();
    }

    boool Repo::UpdateHead(Commit commit)
    {
        SQLite::Statement query(db, R"Request(
            UPDATE head SET commit_id = ? WHERE id = 1
        )Request");
        query.bind(1, head.GetId());
        return query.executeStep();
    }

    Commit Repo::GetIndex(Commit commit)
    {
        SQLite::Statement query(db, R"Request(
            SELECT commit_id FROM index WHERE id = 1
        )Request");
        if (query.executeStep()) 
        {
            return Commit(*this, query.getColumn(0).getInt64());
        }
    }

    Commit Repo::GetHead(Commit commit)
    {
        SQLite::Statement query(db, R"Request(
            SELECT commit_id FROM head WHERE id = 1
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
            SELECT commit_id FROM labels WHERE name like ?
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
            SELECT commit_id, name FROM labels WHERE name LIKE ? LIMIT 2
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

    Transaction::Transaction(Repo &repo, std::string authorName, SQLite::Transaction tn)
        : repo(repo), authorName(authorName), tn(tn)
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
        updates.emplace_back(hash, 
                             (std::filesystem::exists(path) ? PendingUpdateActionData
                                                            : PendingUpdateActionDelete), 
                             path.lexically_normal().lexically_relative(repo.RootPath()));
    }

    int64_t Transaction::GetFileId(Transaction::PendingUpdate update)
    {
        // is there file with same content
        SQLite::Statement query(db, R"Request(
            SELECT id FROM files WHERE hash = ?
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
                INSERT INTO blobs (data) VALUES (?)
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

    std::generator<std::string> Transaction::GetDiffrentFiles()
    {
        if (parents.size() <= 1) 
        {
            co_return;
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
    
        while (query.executeStep()) 
        {
            co_yield query.getColumn(0).getText();
        }
    }
    
    std::generator<std::string> Transaction::GetCommonFiles()
    {
        if (parents.size() == 0) 
        {
            co_return;
        }
    
        std::string queryParams = "?";
        for (size_t i = 1; i < parents.size(); ++i) 
        {
            queryParams += ", ?";
        }
    
        SQLite::Statement query(db, R"Request(
            SELECT MIN(file_id) as file_id, path 
            FROM commit_fs 
            WHERE commit_id IN ()Request" + queryParams + R"Request() 
            GROUP BY path
            HAVING MIN(file_id) = MAX(file_id) AND COUNT(commit_id) = ? -- have one id and exists in all parents
        )Request");
        
        int bindIndex = 1;
        for (const Commit &id : parentIds) 
        {
            query.bind(bindIndex++, id.GetId());
        }
        query.bind(bindIndex, parentIds.size());
    
        while (query.executeStep()) 
        {
            co_yield {query.getColumn(0).getInt64(), query.getColumn(1).getText()};
        }
    }


    Commit Transaction::Apply()
    {
        // check if all diffrent files was changed, if there is more than 1 parent
        if (parents.size() > 1)
        {
            std::hash_set<std::string> loaded;
            for (auto update : updates)
            {
                loaded.insert(update.file);
            }
            std::vector<std::string> error_msg;
            for (auto path : GetDiffrentFiles())
            {
                if (!loaded.contains(path))
                {
                    error_msg.emplace_back(std::format("File {} is different in parents, but wasn't added to transaction.", path));
                }
            }
            if (!error_msg.empty())
            {
                throw CringeError("Not all changed files was added to transaction.", std::move(error_msg));
            }
        }

        // calculate commit hash, as all updates hash + parent hash.
        // TODO: do we need hashes for commits at all?
        std::array<uint8_t, 32> buffer();
        
        // inset new commit id
        SQLite::Statement query(db, R"Request(
            INSERT INTO commits (hash, author, message) VALUES (?, ?, ?)
        )Request");
        query.bind(1, buffer.data(), static_cast<int>(buffer.size()));
        query.bind(2, authorName);
        query.bind(3, commitMessage);
        query.exec();
        int64_t commit_id = db.getLastInsertRowid();

        // insert all new and old files
        // add all new files
        std::map<std::string, int64_t> path2id;
        for (auto update : updates)
        {
            path2id[update.file] = GetFileId(update);
        }
        // add old files
        for (auto [path, id] : GetCommonFiles())
        {
            path2id[path] = id;
        }
        
        SQLite::Statement fsInsertQuery(db, R"Request(
            INSERT INTO commit_fs (commit_id, path, file_id) VALUES (?, ?, ?)
        )Request");

        for (const auto& [path, file_id] : path2id) 
        {
            fsInsertQuery.reset();
            
            fsInsertQuery.bind(1, commit_id);
            fsInsertQuery.bind(2, path);
            fsInsertQuery.bind(3, file_id);
            
            fsInsertQuery.executeStep();
        }

        
        SQLite::Statement linkInsertQuery(db, R"Request(
            INSERT INTO commit_lin (commit_id, parent_id) VALUES (?, ?)
        )Request");

        for (int64_t parent_id : parents) 
        {
            linkInsertQuery.reset();
            
            linkInsertQuery.bind(1, commit_id);
            linkInsertQuery.bind(2, parent_id);
            
            linkInsertQuery.executeStep();
        }

        tn.commit();
    }

    Commit::Commit(Repo &repo, commit_id_t id) 
        : repo(repo),
          id(id)
    {}

    Commit::~Commit() = default;

    std::vector<std::string> Commit::ListFiles()
    {
        if (parents.size() == 0) 
        {
            return {};
        }
    
        SQLite::Statement query(db, R"Request(
            SELECT path -- DISTINCT isn't required
            FROM commit_fs 
            WHERE commit_id = ?
        )Request");
        query.bind(1, id);
        std::vector<std::string> results;
        results.reserve(256); // for average fs
        while (query.executeStep()) 
        {
            result.emplace_back(query.getColumn(0).getInt64(), query.getColumn(1).getText());
        }
        return results;
    }

    bool Commit::IsChildOf(Commit other) 
    {
        if (this->GetId() == other.GetId()) return true;
        SQLite::Statement query(db, R"Request(
            WITH RECURSIVE ancestors(id) AS (
                SELECT ? 
                UNION
                SELECT parent_id
                FROM commit_parents
                JOIN ancestors ON ancestors.id = commit_parents.child_id
            )
            SELECT EXISTS(SELECT 1 FROM ancestors WHERE id = ?)
        )Request");

        query.bind(1, id);
        query.bind(2, other.GetId());

        if (query.executeStep()) 
        {
            return query.getColumn(0).getInt() > 0;
        }
        return false;
    }
    
    bool Commit::IsDirectChildOf(Commit other) 
    {
        SQLite::Statement query(db, R"Request(
            SELECT 1 FROM commit_parents WHERE child_id = ? AND parent_id = ?
        )Request");
        
        query.bind(1, id);
        query.bind(2, other.GetId());
    
        return query.executeStep();
    }

    commit_id_t GetId()
    {
        return id;
    }

    std::vector<Commit> Commit::GetParents() 
    {
        SQLite::Statement query(db, R"Request(
            SELECT parent_id FROM commit_parents WHERE child_id = ?
        )Request");
        query.bind(1, id);
        
        std::vector<Commit> parents;
        parents.reserve(4);
        while (query.executeStep()) 
        {
            commit_id_t parentId = query.getColumn(0).getInt64();    
            parents.emplace_back(db, parentId);
        }

        return parents;
    }

    bool Commit::RestoreFile(std::filesystem::path path) 
    {
        std::string relativePath = path.lexically_normal().lexically_relative(repo.RootPath()).string();
        
        SQLite::Statement query(db, R"Request(
            SELECT f.in_filesystem, f.filesystem_path, b.data
            FROM commit_fs cfs
            JOIN files f ON cfs.file_id = f.id
            LEFT JOIN blobs b ON f.blob_id = b.id
            WHERE cfs.commit_id = ? AND cfs.path = ?
        )Request");

        query.bind(1, id);
        query.bind(2, relativePath);

        if (!query.executeStep()) // if file doesn't exists in commit's fs, remove it.
        {
            if (!std::filesystem::exists(path)) 
            {
                return false;
            }
            std::filesystem::remove(path);
            return true;
        }

        bool inFilesystem = query.getColumn(0).getInt() > 0;

        if (inFilesystem) 
        {
            // TODO:
            // std::string fsSourcePath = query.getColumn(1).getText();
            // std::filesystem::copy_file(repo.RootPath() / fsSourcePath, path, 
            //                            std::filesystem::copy_options::overwrite_existing);
            return false; 
        } 
        else 
        {
            auto blob = query.getColumn(2);
            std::ofstream out(path, std::ios::binary);
            out.write(static_cast<const char*>(blob.getBlob()), blob.getBytes());
            return true;
        }
    }
    
}
