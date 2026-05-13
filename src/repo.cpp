#include "gitcringe.hpp"

#include <vector>
#include <cstring>
#include <string>
#include <unordered_set>
#include <set>
#include <map>
#include <filesystem>
#include <fstream>
#include <print>
#include <cinttypes>
#include <stdexcept>
#include <generator>


#include "../include/picosha2.h"


std::array<uint8_t, 32> GetFileHash(std::filesystem::path file)
{
    std::array<uint8_t, 32> hash;
    std::ifstream f(file, std::ios::binary);
    if (f.is_open()) 
    {
        picosha2::hash256(f, hash.begin(), hash.end());
    }
    return hash;
}


namespace cringe
{
    static std::filesystem::path ensure_dir(std::filesystem::path p) 
    {
        std::filesystem::create_directories(p);
        return p;
    }

    Repo::Repo(std::filesystem::path path)
    try : db((ensure_dir(path /".cvcs") / "cringe.db").u8string(), SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE),
          root(path.lexically_normal()),
          fs_storage_path(ensure_dir(path / ".cvcs") / "fs")
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
                child_id INTEGER REFERENCES commits(id) ON DELETE CASCADE, 
                parent_id INTEGER REFERENCES commits(id) ON DELETE CASCADE, 
                PRIMARY KEY (child_id, parent_id)
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
                file_id INTEGER REFERENCES files(id),
                PRIMARY KEY (commit_id, path)
            );
            CREATE TABLE IF NOT EXISTS branches
            (
                name TEXT PRIMARY KEY,
                commit_id INTEGER REFERENCES commits(id)
            );
            CREATE TABLE IF NOT EXISTS vhead 
            (
                id INTEGER PRIMARY KEY CHECK (id = 1), -- Only one entry
                commit_id INTEGER REFERENCES commits(id),
                branch_name TEXT REFERENCES branches(name)
            );
            CREATE TABLE IF NOT EXISTS vindex
            (
                id INTEGER PRIMARY KEY CHECK (id = 1), -- Only one entry
                commit_id INTEGER REFERENCES commits(id)
            );
        )Request");
        
        db.exec("INSERT OR IGNORE INTO vhead (id, commit_id, branch_name) VALUES (1, NULL, NULL);");
        db.exec("INSERT OR IGNORE INTO vindex (id, commit_id) VALUES (1, NULL);");
    }
    catch (std::exception& e) 
    {
        throw CringeError(std::format("Error: can't open database: {}", e.what()), {});
    }


    Repo::~Repo() = default;

    bool Repo::UpdateIndex(std::optional<Commit> commit)
    {
        int64_t commit_id = commit.has_value() ? commit->GetId() : GetHead().GetId();
        SQLite::Statement query(db, R"Request(
            UPDATE vindex SET commit_id = ? WHERE id = 1
        )Request");
        query.bind(1, commit_id);
        return query.exec() > 0;
    }

    bool Repo::UpdateHead(Commit commit)
    {
        SQLite::Statement query(db, R"Request(
            UPDATE vhead SET commit_id = ? WHERE id = 1
        )Request");
        query.bind(1, commit.GetId());
        bool res = query.exec() > 0;

        std::string branch = GetCurrentBranch();
        if (!branch.empty()) 
        {
            SQLite::Statement queryBranch(db, R"Request(
                UPDATE branches SET commit_id = ? WHERE name = ?
            )Request");
            queryBranch.bind(1, commit.GetId());
            queryBranch.bind(2, branch);
            queryBranch.exec();
        }
        return res;
    }

    void Repo::Squash(Commit child, Commit parent_to_drop)
    {
        if (child.GetId() == parent_to_drop.GetId() || parent_to_drop.GetId() == 0) return;

        SQLite::Statement qLink(db, R"Request(
            INSERT OR IGNORE INTO commit_links (child_id, parent_id)
            SELECT ?, parent_id FROM commit_links WHERE child_id = ?
        )Request");
        qLink.bind(1, child.GetId());
        qLink.bind(2, parent_to_drop.GetId());
        qLink.exec();

        SQLite::Statement qDelete(db, R"Request(
            DELETE FROM commits WHERE id = ?
        )Request");
        qDelete.bind(1, parent_to_drop.GetId());
        qDelete.exec();
    }

    Commit Repo::GetIndex()
    {
        SQLite::Statement query(db, R"Request(
            SELECT commit_id FROM vindex WHERE id = 1
        )Request");
        if (query.executeStep()) 
        {
            return Commit(*this, query.getColumn(0).getInt64());
        }
        throw CringeError("?No Index commit found?", {});
    }

    Commit Repo::GetHead()
    {
        SQLite::Statement query(db, R"Request(
            SELECT commit_id FROM vhead WHERE id = 1
        )Request");
        if (query.executeStep()) 
        {
            return Commit(*this, query.getColumn(0).getInt64());
        }
        throw CringeError("?No Index commit found?", {});
    }
    
    std::vector<Commit> Repo::GetCommit(std::string_view identifier) 
    {
        std::string str_id(identifier);
        std::vector<Commit> results;

        if (str_id == "HEAD") 
        {
            try 
            {
                results.push_back(GetHead());
                return results;
            }
            catch (...) 
            {
                return {};
            }
        }

        SQLite::Statement queryBranch(db, R"Request(
            SELECT commit_id FROM branches WHERE name = ?
        )Request");
        queryBranch.bind(1, str_id);
        
        if (queryBranch.executeStep())
        {
            return {Commit(*this, queryBranch.getColumn(0).getInt64())};
        }

        bool is_number = !str_id.empty() && std::all_of(str_id.begin(), str_id.end(), [](unsigned char c) 
        { 
            return std::isdigit(c); 
        });

        if (is_number)
        {
            try 
            {
                int64_t exact_id = std::stoll(str_id);
                SQLite::Statement qId(db, R"Request(
                    SELECT id FROM commits WHERE id = ?
                )Request");
                qId.bind(1, exact_id);
                
                if (qId.executeStep())
                {
                    results.emplace_back(*this, exact_id);
                }
            }
            catch (const std::exception&)
            {
            }
        }

        std::string argument = "%" + str_id + "%";
        
        SQLite::Statement query(db, R"Request(
            SELECT commit_id, name FROM labels WHERE name LIKE ?
        )Request");
        query.bind(1, argument);

        while (query.executeStep()) 
        {
            int64_t first_id = query.getColumn(0).getInt64();
            if (query.getColumn(1).getString() == str_id)
            {
                return {Commit(*this, first_id)};
            }
            
            bool exists = false;
            for (const Commit &c : results)
            {
                if (c.GetId() == first_id)
                {
                    exists = true;
                    break;
                }
            }

            if (!exists)
            {
                results.emplace_back(*this, first_id);
            }
        }

        return results;
    }

    std::vector<Commit> Repo::GetReferences()
    {
        std::vector<Commit> refs;
        SQLite::Statement query(db, R"Request(
            SELECT * FROM commits
        )Request");
        
        while (query.executeStep()) 
        {
            refs.emplace_back(*this, query.getColumn(0).getInt64());
        }
        return refs;
    }

    std::filesystem::path Repo::RootPath()
    {
        return root;
    }

    Transaction Repo::StartCommit()
    {
        return Transaction(*this, "NoName");
    }

    void Repo::CollectGarbage()
    {
        // TODO:
        // db.exec(R"Request(
        //     
        // )Request")
    }

    bool Repo::CreateBranch(std::string_view name, Commit commit)
    {
        SQLite::Statement query(db, R"Request(
            INSERT INTO branches (name, commit_id) VALUES (?, ?)
        )Request");
        query.bind(1, std::string(name));
        query.bind(2, commit.GetId());
        return query.exec() > 0;
    }

    std::vector<std::pair<std::string, commit_id_t>> Repo::ListBranches()
    {
        std::vector<std::pair<std::string, commit_id_t>> res;
        SQLite::Statement query(db, R"Request(
            SELECT name, commit_id FROM branches
        )Request");

        while (query.executeStep()) 
        {
            res.emplace_back(query.getColumn(0).getString(), query.getColumn(1).getInt64());
        }
        return res;
    }

    std::string Repo::GetCurrentBranch()
    {
        SQLite::Statement query(db, R"Request(
            SELECT branch_name FROM vhead WHERE id = 1
        )Request");

        if (query.executeStep() && !query.isColumnNull(0)) 
        {
            return query.getColumn(0).getString();
        }
        return "";
    }

    bool Repo::AttachHead(std::string_view branch_name)
    {
        SQLite::Statement query(db, R"Request(
            UPDATE vhead SET branch_name = ? WHERE id = 1
        )Request");
        query.bind(1, std::string(branch_name));
        
        return query.exec() > 0;
    }

    bool Repo::DetachHead()
    {
        SQLite::Statement query(db, R"Request(
            UPDATE vhead SET branch_name = NULL WHERE id = 1
        )Request");
        
        return query.exec() > 0;
    }

    std::vector<std::pair<UpdateTypes, std::filesystem::path>> Repo::ListChangedFiles(Commit commit)
    {    
        std::vector<std::pair<UpdateTypes, std::filesystem::path>> results;
        results.reserve(32);
        std::set<std::string> existing;
        for (auto it = std::filesystem::recursive_directory_iterator(root); 
                  it != std::filesystem::recursive_directory_iterator(); ++it) 
        {
            if (std::filesystem::is_directory(*it) && it->path().filename() == ".cvcs") 
            {
                it.disable_recursion_pending(); 
                continue;
            }
    
            if (std::filesystem::is_regular_file(*it)) 
            {
                std::array<uint8_t, 32> new_hash = GetFileHash(it->path());
                
                // TODO: add timestamp database
                       
                SQLite::Statement query(db, R"Request(
                    SELECT f.hash
                    FROM commit_fs cf JOIN files f ON cf.file_id = f.id
                    WHERE cf.commit_id = ?
                      AND cf.path = ?
                )Request");

                query.bind(1, commit.GetId());
                std::string name = std::filesystem::absolute(it->path()).lexically_normal().lexically_relative(root).string();
                query.bind(2, name);
                existing.insert(name);

                if (query.executeStep()) 
                {
                    SQLite::Column column = query.getColumn(0);
                    const void* db_hash_ptr = column.getBlob();
                    size_t db_hash_size = column.getBytes();
                
                    if (db_hash_size != new_hash.size() || std::memcmp(db_hash_ptr, new_hash.data(), new_hash.size()) != 0) 
                    {
                        results.emplace_back(UPDATE_CHANGE, it->path());
                    }
                } 
                else 
                {
                    results.emplace_back(UPDATE_CREATE, it->path());
                }
                
            }
        }

        /* now, find all deleted files */
        SQLite::Statement query(db, R"Request(
            SELECT path
            FROM commit_fs
            WHERE commit_id = ?
        )Request");

        query.bind(1, commit.GetId());

        while (query.executeStep())
        {
            std::string path_in_commit = query.getColumn(0).getText();
            if (existing.find(path_in_commit) == existing.end())
            {
                results.emplace_back(UPDATE_DELETE, std::filesystem::path(path_in_commit));
            }
        }
        
        return results;
    }
    


    void Repo::Exec(std::string command)
    {
        try 
        {
            SQLite::Statement query(db, command);
            
            int columnCount = query.getColumnCount();
            if (columnCount == 0) 
            {
                int rowsAffected = query.exec();
                std::println("Query executed successfully. Rows affected: {}", rowsAffected);
                return;
            }

            std::vector<std::string> headers;
            for (int i = 0; i < columnCount; ++i) 
            {
                headers.push_back(query.getColumnName(i));
            }

            std::vector<std::vector<std::vector<std::string>>> rows;
            std::vector<size_t> columnWidths(columnCount, 0);

            for (int i = 0; i < columnCount; ++i) 
            {
                columnWidths[i] = headers[i].length();
            }

            while (query.executeStep()) 
            {
                std::vector<std::vector<std::string>> cellLinesInRow(columnCount);
                for (int i = 0; i < columnCount; ++i) 
                {
                    std::string cellValue;
                    
                    if (query.isColumnNull(i)) 
                    {
                        cellValue = "NULL";
                    }
                    else 
                    {
                        const char* bytes = static_cast<const char*>(query.getColumn(i).getBlob());
                        int size = query.getColumn(i).getBytes();
                        
                        bool hasBinary = false;
                        for (int j = 0; j < size; ++j) 
                        {
                            unsigned char ch = static_cast<unsigned char>(bytes[j]);
                            if (ch < 32 && ch != '\t' && ch != '\n' && ch != '\r') 
                            {
                                hasBinary = true;
                                break;
                            }
                        }
                        
                        if (hasBinary) 
                        {
                            if (size)
                            {
                                cellValue += std::format("{:02X}", static_cast<unsigned char>(bytes[0]));
                                for (int j = 1; j < size; ++j) 
                                {
                                    cellValue += std::format(" {:02X}", static_cast<unsigned char>(bytes[j]));
                                }
                            }
                        }
                        else 
                        {
                            cellValue = std::string(bytes, size);
                        }
                    }
                    
                    size_t start = 0;
                    size_t end = cellValue.find('\n');
                    while (end != std::string::npos) 
                    {
                        std::string line = cellValue.substr(start, end - start);
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        columnWidths[i] = std::max(columnWidths[i], line.length());
                        cellLinesInRow[i].push_back(line);
                        start = end + 1;
                        end = cellValue.find('\n', start);
                    }
                    std::string lastLine = cellValue.substr(start);
                    columnWidths[i] = std::max(columnWidths[i], lastLine.length());
                    cellLinesInRow[i].push_back(lastLine);
                }
                rows.push_back(cellLinesInRow);
            }

            std::string separator = "+";
            for (size_t width : columnWidths) 
            {
                separator += std::string(width + 2, '-') + "+";
            }

            std::println("{}", separator);

            std::print("|");
            for (int i = 0; i < columnCount; ++i) 
            {
                std::print(" {} |", std::format("{: <{}}", headers[i], columnWidths[i]));
            }
            std::println("");

            std::println("{}", separator);

            for (const auto& gridRow : rows) 
            {
                size_t maxLines = 0;
                for (int i = 0; i < columnCount; ++i) 
                {
                    maxLines = std::max(maxLines, gridRow[i].size());
                }

                for (size_t lineIdx = 0; lineIdx < maxLines; ++lineIdx) 
                {
                    std::print("|");
                    for (int i = 0; i < columnCount; ++i) 
                    {
                        std::string text = (lineIdx < gridRow[i].size()) ? gridRow[i][lineIdx] : "";
                        std::print(" {} |", std::format("{: <{}}", text, columnWidths[i]));
                    }
                    std::println("");
                }
                std::println("{}", separator);
            }

            std::println("Total rows: {}", rows.size());
        } 
        catch (const std::exception& e) 
        {
            std::println("SQL Error: {}", e.what());
        }
    }




    Transaction::Transaction(Repo &repo, std::string authorName)
             : repo(repo), authorName(authorName), tn(repo.db)
    {
    }

    Transaction::~Transaction() = default;

    void Transaction::AddParent(Commit commit)
    {
        parents.push_back(commit);
    }

    void Transaction::LoadFile(std::filesystem::path path)
    {
        // update path
        path = std::filesystem::absolute(path);
        
        // calculate hash
        std::array<uint8_t, 32> hash = GetFileHash(path);
        
        // insert file entry
        updates.emplace_back(hash,
                             (std::filesystem::exists(path) ? PendingUpdateActionData
                                                            : PendingUpdateActionDelete), 
                             path.lexically_normal().lexically_relative(repo.RootPath()));
    }

    int64_t Transaction::GetFileId(Transaction::PendingUpdate update)
    {
        // is there file with same content
        SQLite::Statement query(repo.db, R"Request(
            SELECT id FROM files WHERE hash = ?
        )Request");
        query.bind(1, update.hash.data(), update.hash.size());
        if (query.executeStep())
        {
            // found same file, use it's id
            return query.getColumn(0).getInt64();
        }

        // need to create new file.
        uintmax_t size = std::filesystem::file_size(update.file);
        if (size > repo.Config.FilesystemSizeThreshold)
        {
            // copy and compress file to directory
            // TODO copy
            std::filesystem::path result_path = "...";
            
            // insert new entry into files
            SQLite::Statement query(repo.db, R"Request(
                INSERT INTO files (hash, in_filesystem, blob_id, filesystem_path) VALUES (?, true, null, ?)
            )Request");
            query.bind(1, update.hash.data(), update.hash.size());
            query.bind(2, result_path);
            query.exec();
            return repo.db.getLastInsertRowid();
        }
        else
        {
            // read file
            std::ifstream file(update.file, std::ios::binary);
            if (!file.is_open()) 
            {
                throw CringeError(std::format("Can't open file {}.", update.file.string()), {});
            }
            std::vector<std::byte> buffer(size);
            if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) 
            {
                throw CringeError(std::format("Can't open file {}.", update.file.string()), {});
            }
            // insert new entry into blobs
            SQLite::Statement queryBlob(repo.db, R"Request(
                INSERT INTO blobs (data) VALUES (?)
            )Request");
            queryBlob.bind(1, buffer.data(), static_cast<int>(buffer.size()));
            queryBlob.exec();
            int64_t blob_id = repo.db.getLastInsertRowid();
            // insert new entry into files
            SQLite::Statement query(repo.db, R"Request(
                INSERT INTO files (hash, in_filesystem, blob_id, filesystem_path) VALUES (?, false, ?, null)
            )Request");
            query.bind(1, update.hash.data(), update.hash.size());
            query.bind(2, blob_id);
            query.exec();
            return repo.db.getLastInsertRowid();
        }
    }

    std::vector<std::tuple<commit_id_t, bool, std::string>> Transaction::GetFileVersions(std::string path)
    {
        std::vector<std::tuple<commit_id_t, bool, std::string>> result;
    
        std::string queryParams = "?";
        for (size_t i = 1; i < parents.size(); ++i) 
        {
            queryParams += ", ?";
        }
            
        SQLite::Statement query(repo.db, R"Request(
            SELECT 
                b.data,
                f.in_filesystem,
                f.filesystem_path,
                cf.commit_id
            FROM commit_fs cf
            JOIN files f ON cf.file_id = f.id
            LEFT JOIN blobs b ON f.blob_id = b.id
            WHERE cf.path = ? 
              AND cf.commit_id IN ()Request" + queryParams + R"Request()
        )Request");
        
        query.bind(1, path);
        int bindIndex = 2;
        for (const Commit &id : parents) 
        {
            query.bind(bindIndex++, id.GetId());
        }
    
        while (query.executeStep()) 
        {
            bool inFilesystem = query.getColumn(1).getInt() != 0;
            std::string fsPath = query.getColumn(2).getText(); 
            int64_t id = query.getColumn(3).getInt64(); 
            
            std::string blobData;
            if (!query.getColumn(0).isNull()) 
            {
                const void* blobPtr = query.getColumn(0).getBlob();
                int blobSize = query.getColumn(0).getBytes();
                blobData.assign(static_cast<const char*>(blobPtr), blobSize);
            }

            if (inFilesystem)
            {
                result.emplace_back(id, inFilesystem, fsPath);
            }
            else
            {
                result.emplace_back(id, inFilesystem, blobData);
            }
        }
        return result;
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
    
        SQLite::Statement query(repo.db, R"Request(
            SELECT path 
            FROM commit_fs 
            WHERE commit_id IN ()Request" + queryParams + R"Request() 
            GROUP BY path
            HAVING MIN(file_id) != MAX(file_id) -- if there is 2 diffrent file_id's
                   OR COUNT(commit_id) < ?      -- or if in some parents id isn't presented
        )Request");
        
        int bindIndex = 1;
        for (const Commit &id : parents) 
        {
            query.bind(bindIndex++, id.GetId());
        }
        query.bind(bindIndex, static_cast<int64_t>(parents.size()));
    
        while (query.executeStep()) 
        {
            co_yield query.getColumn(0).getText();
        }
    }
    
    std::generator<std::pair<int64_t, std::string>> Transaction::GetCommonFiles()
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
    
        SQLite::Statement query(repo.db, R"Request(
            SELECT MIN(file_id) as file_id, path 
            FROM commit_fs 
            WHERE commit_id IN ()Request" + queryParams + R"Request() 
            GROUP BY path
            HAVING MIN(file_id) = MAX(file_id) AND COUNT(commit_id) = ? -- have one id and exists in all parents
        )Request");
        
        int bindIndex = 1;
        for (const Commit &id : parents) 
        {
            query.bind(bindIndex++, id.GetId());
        }
        query.bind(bindIndex, static_cast<int64_t>(parents.size()));        
    
        while (query.executeStep()) 
        {
            co_yield {query.getColumn(0).getInt64(), query.getColumn(1).getText()};
        }
    }


    Commit Transaction::Apply(std::string commitMessage)
    {
        // check if all diffrent files was changed, if there is more than 1 parent
        if (parents.size() > 1)
        {
            std::unordered_set<std::string> loaded;
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
        std::array<uint8_t, 32> buffer;
        
        // inset new commit id
        SQLite::Statement query(repo.db, R"Request(
            INSERT INTO commits (hash, author, message) VALUES (?, ?, ?)
        )Request");
        query.bind(1, buffer.data(), static_cast<int>(buffer.size()));
        query.bind(2, authorName);
        query.bind(3, commitMessage);
        query.exec();
        int64_t commit_id = repo.db.getLastInsertRowid();

        // insert all new and old files
        // add all new files
        std::map<std::string, int64_t> path2id;
        std::set<std::string> deleted;
        for (auto update : updates)
        {
            if (update.action == PendingUpdateActionData)
            {
                path2id[update.file] = GetFileId(update);
            }
            else
            {
                deleted.insert(update.file);
            }
        }
        // add old files
        for (auto [id, path] : GetCommonFiles())
        {
            if (!deleted.contains(path) && !path2id.contains(path))
            {
                path2id[path] = id;
            }
        }
        
        SQLite::Statement fsInsertQuery(repo.db, R"Request(
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

        
        SQLite::Statement linkInsertQuery(repo.db, R"Request(
            INSERT INTO commit_links (child_id, parent_id) VALUES (?, ?)
        )Request");

        for (const Commit &parent : parents) 
        {
            linkInsertQuery.reset();
            
            linkInsertQuery.bind(1, commit_id);
            linkInsertQuery.bind(2, parent.GetId());
            
            linkInsertQuery.executeStep();
        }

        tn.commit();
        
        return Commit(repo, commit_id);
    }

    Commit::Commit(Repo &repo, commit_id_t id) 
        : repo(repo),
          id(id)
    {}
    
    Commit::Commit(const Commit& other) 
        : repo(other.repo), id(other.id) 
    {}
    
    Commit::Commit(Commit&& other) noexcept 
        : repo(other.repo), id(other.id) 
    {
        other.id = -1;
    }

    Commit::~Commit() = default;

    std::vector<std::string> Commit::ListFiles()
    {
        SQLite::Statement query(repo.db, R"Request(
            SELECT path -- DISTINCT isn't required
            FROM commit_fs 
            WHERE commit_id = ?
        )Request");
        query.bind(1, id);
        std::vector<std::string> results;
        results.reserve(256); // for average fs
        while (query.executeStep()) 
        {
            results.emplace_back(query.getColumn(0).getText());
        }
        return results;
    }

    bool Commit::IsChildOf(Commit other) 
    {
        if (this->GetId() == other.GetId()) return true;
        SQLite::Statement query(repo.db, R"Request(
            WITH RECURSIVE ancestors(id) AS (
                SELECT ? 
                UNION
                SELECT parent_id
                FROM commit_links
                JOIN ancestors ON ancestors.id = commit_links.child_id
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
        SQLite::Statement query(repo.db, R"Request(
            SELECT 1 FROM commit_links WHERE child_id = ? AND parent_id = ?
        )Request");
        
        query.bind(1, id);
        query.bind(2, other.GetId());
    
        return query.executeStep();
    }

    std::vector<Commit> Commit::GetParents() 
    {
        SQLite::Statement query(repo.db, R"Request(
            SELECT parent_id FROM commit_links WHERE child_id = ?
        )Request");
        query.bind(1, id);
        
        std::vector<Commit> parents;
        parents.reserve(4);
        while (query.executeStep()) 
        {
            commit_id_t parentId = query.getColumn(0).getInt64();    
            parents.emplace_back(repo, parentId);
        }

        return parents;
    }

    bool Commit::RestoreFile(std::filesystem::path path) 
    {
        path = std::filesystem::absolute(path);
        std::string relativePath = path.lexically_normal().lexically_relative(repo.RootPath()).string();
        
        SQLite::Statement query(repo.db, R"Request(
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
    
    std::string Commit::GetFileContent(std::filesystem::path path) 
    {
        path = std::filesystem::absolute(path);
        std::string relativePath = path.lexically_normal().lexically_relative(repo.RootPath()).string();
        
        SQLite::Statement query(repo.db, R"Request(
            SELECT f.in_filesystem, f.filesystem_path, b.data
            FROM commit_fs cfs
            JOIN files f ON cfs.file_id = f.id
            LEFT JOIN blobs b ON f.blob_id = b.id
            WHERE cfs.commit_id = ? AND cfs.path = ?
        )Request");

        query.bind(1, id);
        query.bind(2, relativePath);

        if (!query.executeStep())
        {
            std::println("File don't exists");
            return "";
        }

        bool inFilesystem = query.getColumn(0).getInt() > 0;

        if (inFilesystem) 
        {
            std::println("Can't read file from LFS for now");
            // TODO:
            // std::string fsSourcePath = query.getColumn(1).getText();
            // std::filesystem::copy_file(repo.RootPath() / fsSourcePath, path, 
            //                            std::filesystem::copy_options::overwrite_existing);
            return ""; 
        } 
        else 
        {
            auto blob = query.getColumn(2);
            
            size_t data_size = blob.getBytes();
            const char* data_ptr = static_cast<const char*>(blob.getBlob());
            
            return std::string(data_ptr, data_size);
        }
    }

    commit_id_t Commit::GetId() const
    {
        return id;
    }

    std::string Commit::GetAuthor() const
    {
        SQLite::Statement query(repo.db, R"Request(
            SELECT author FROM commits WHERE id = ?
        )Request");

        query.bind(1, id);
        
        if (query.executeStep()) 
        {
            return query.getColumn(0).getText();
        }

        return "Unknown";
    }


    std::string Commit::GetMessage() const
    {
        SQLite::Statement query(repo.db, R"Request(
            SELECT message FROM commits WHERE id = ?
        )Request");

        query.bind(1, id);
        
        if (query.executeStep()) 
        {
            return query.getColumn(0).getText();
        }

        return "";
    }

    bool Commit::UpdateMessage(const std::string& message)
    {
        SQLite::Statement query(repo.db, R"Request(
            UPDATE commits SET message = ? WHERE id = ?
        )Request");

        query.bind(1, message);
        query.bind(2, id);
        
        return query.exec() > 0;
    }

    std::vector<std::string> Commit::GetLabels() const
    {
        std::vector<std::string> labels;

        bool is_head = false;
        std::string head_branch = "";
        
        SQLite::Statement qHead(repo.db, R"Request(
            SELECT branch_name FROM vhead WHERE commit_id = ?
        )Request");
        qHead.bind(1, id);
        if (qHead.executeStep())
        {
            is_head = true;
            if (qHead.isColumnNull(0)) 
            {
                labels.push_back("HEAD (detached)");
            }
            else 
            {
                head_branch = qHead.getColumn(0).getString();
                labels.push_back("HEAD -> " + head_branch);
            }
        }

        SQLite::Statement qBranches(repo.db, R"Request(
            SELECT name FROM branches WHERE commit_id = ?
        )Request");
        qBranches.bind(1, id);
        
        while (qBranches.executeStep()) 
        {
            std::string bname = qBranches.getColumn(0).getString();
            if (is_head && bname == head_branch)
            {
                continue;
            }
            labels.push_back(bname);
        }
        
        SQLite::Statement qLabels(repo.db, R"Request(
            SELECT name FROM labels WHERE commit_id = ?
        )Request");
        qLabels.bind(1, id);

        while (qLabels.executeStep()) 
        {
            labels.push_back(qLabels.getColumn(0).getString());
        }

        return labels;
    }

    std::vector<std::pair<UpdateTypes, std::string>> Commit::GetChanges()
    {
        std::vector<std::pair<UpdateTypes, std::string>> results;
        auto parents = GetParents();

        if (parents.empty()) 
        {
            for (const auto& path : ListFiles()) 
            {
                results.emplace_back(UPDATE_CREATE, path);
            }
            return results;
        }

        commit_id_t parent_id = parents[0].GetId();

        SQLite::Statement queryChild(repo.db, R"Request(
            SELECT c.path, c.file_id, p.file_id 
            FROM commit_fs c
            LEFT JOIN commit_fs p ON p.commit_id = ? AND c.path = p.path
            WHERE c.commit_id = ?
        )Request");
        queryChild.bind(1, parent_id);
        queryChild.bind(2, id);

        std::set<std::string> child_paths;
        
        while (queryChild.executeStep()) 
        {
            std::string path = queryChild.getColumn(0).getString();
            child_paths.insert(path);
            
            if (queryChild.isColumnNull(2)) 
            {
                results.emplace_back(UPDATE_CREATE, path);
            } 
            else if (queryChild.getColumn(1).getInt64() != queryChild.getColumn(2).getInt64()) 
            {
                results.emplace_back(UPDATE_CHANGE, path);
            }
        }

        SQLite::Statement queryParent(repo.db, R"Request(
            SELECT path FROM commit_fs WHERE commit_id = ?
        )Request");
        queryParent.bind(1, parent_id);
        
        while (queryParent.executeStep()) 
        {
            std::string path = queryParent.getColumn(0).getString();
            if (!child_paths.contains(path)) 
            {
                results.emplace_back(UPDATE_DELETE, path);
            }
        }
        
        return results;
    }
}
