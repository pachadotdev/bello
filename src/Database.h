#pragma once

#include <string>
#include <vector>

struct Item {
    std::string id;
    std::string title;
    std::string authors;
    std::string year;
    std::string type;
    std::string doi;
    std::string isbn;
    std::string abstract;
    std::string address;
    std::string publisher;
    std::string pdf_path;
    std::string collection;
    // Additional BibTeX fields
    std::string url;
    std::string journal;
    std::string pages;
    std::string volume;
    std::string number;
    std::string editor;
    std::string booktitle;
    std::string series;
    std::string edition;
    std::string chapter;
    std::string school;
    std::string institution;
    std::string organization;
    std::string howpublished;
    std::string language;
    std::string keywords;
    std::string month;
    std::string note;
    // JSON for arbitrary extra fields (dynamic BibTeX fields)
    std::string extra;
};

class Database {
public:
    Database(const std::string &path);
    ~Database();

    void init();
    void addItem(const Item &it);
    void updateItem(const Item &it);
    std::vector<Item> listItems();
    std::vector<std::string> listCollections();
    std::vector<Item> listItemsInCollection(const std::string &collection);
    bool getItem(const std::string &id, Item &out);
    bool findItemByDOI(const std::string &doi, Item &out);
    bool findItemByISBN(const std::string &isbn, Item &out);
    bool findItemByTitleAndAuthor(const std::string &title, const std::string &authors, Item &out);
    bool findItemByTitleAndCollection(const std::string &title, const std::string &collection, Item &out);
    void addCollection(const std::string &name);
    void deleteItem(const std::string &id);
    // Collection management
    void renameCollection(const std::string &oldName, const std::string &newName);
    void deleteCollection(const std::string &name);
    // Multi-collection support
    void addItemToCollection(const std::string &itemId, const std::string &collection);
    void removeItemFromCollection(const std::string &itemId, const std::string &collection);
    std::vector<std::string> getItemCollections(const std::string &itemId);

private:
    struct Impl;
    Impl *pimpl;
};

// Inline implementation
#include <duckdb.hpp>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

struct Database::Impl {
    duckdb::DuckDB db;
    std::unique_ptr<duckdb::Connection> conn;
    Impl(const std::string &path) : db(path), conn(std::make_unique<duckdb::Connection>(db)) {}
};

inline Database::Database(const std::string &path) : pimpl(new Impl(path)) {}

inline Database::~Database() { delete pimpl; }

inline void Database::init() {
    try {
        pimpl->conn->Query("CREATE TABLE IF NOT EXISTS items (id TEXT PRIMARY KEY, title TEXT, authors TEXT, year TEXT, doi TEXT, isbn TEXT, type TEXT, abstract TEXT, address TEXT, publisher TEXT, journal TEXT, pages TEXT, volume TEXT, number TEXT, keywords TEXT, month TEXT, url TEXT, note TEXT, extra TEXT, pdf_path TEXT, collection TEXT);");
        // Ensure older DBs get new columns (ignore errors if they already exist)
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN isbn TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN type TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN abstract TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN address TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN publisher TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN editor TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN booktitle TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN series TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN edition TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN chapter TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN school TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN institution TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN organization TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN howpublished TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN language TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN journal TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN pages TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN volume TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN number TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN keywords TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN month TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN url TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN note TEXT;"); } catch(...) {}
        try { pimpl->conn->Query("ALTER TABLE items ADD COLUMN extra TEXT;"); } catch(...) {}
        pimpl->conn->Query("CREATE TABLE IF NOT EXISTS collections (name TEXT PRIMARY KEY);");
        // Create item_collections join table for many-to-many relationship
        pimpl->conn->Query("CREATE TABLE IF NOT EXISTS item_collections (item_id TEXT, collection TEXT, PRIMARY KEY (item_id, collection));");
        auto res = pimpl->conn->Query("SELECT COUNT(*) FROM collections");
        if (res && !res->HasError() && res->RowCount() > 0) {
            auto cnt = res->GetValue(0,0).ToString();
            if (cnt == "0") {
                pimpl->conn->Query("INSERT INTO collections (name) VALUES ('Rename or delete this collection');");
                pimpl->conn->Query("INSERT INTO items (id,title,authors,year,doi,pdf_path,collection) VALUES ('seed-1','Add references here','','2025','','','Rename or delete this collection');");
                pimpl->conn->Query("INSERT INTO item_collections (item_id, collection) VALUES ('seed-1', 'Rename or delete this collection');");
            }
        }
        // Migrate existing items to item_collections table if needed
        pimpl->conn->Query("INSERT OR IGNORE INTO item_collections (item_id, collection) SELECT id, collection FROM items WHERE collection != '';");
    } catch (std::exception &e) {
        std::cerr << "DB init error: " << e.what() << std::endl;
        throw;
    }
}

// Simple SQL escaper for single-quoted string literals
static inline std::string escapeSQL(const std::string &s) {
    std::string out;
    out.reserve(s.size()*2);
    for (char c : s) {
        if (c == '\'') out += "''";
        else out.push_back(c);
    }
    return out;
}

inline void Database::addItem(const Item &it) {
    // Escape fields to avoid SQL errors from quotes/newlines
    std::string id = escapeSQL(it.id);
    std::string title = escapeSQL(it.title);
    std::string authors = escapeSQL(it.authors);
    std::string year = escapeSQL(it.year);
    std::string doi = escapeSQL(it.doi);
    std::string isbn = escapeSQL(it.isbn);
    std::string type = escapeSQL(it.type);
    std::string abstract = escapeSQL(it.abstract);
    std::string address = escapeSQL(it.address);
    std::string publisher = escapeSQL(it.publisher);
    std::string journal = escapeSQL(it.journal);
    std::string pages = escapeSQL(it.pages);
    std::string volume = escapeSQL(it.volume);
    std::string number = escapeSQL(it.number);
    std::string editor = escapeSQL(it.editor);
    std::string booktitle = escapeSQL(it.booktitle);
    std::string series = escapeSQL(it.series);
    std::string edition = escapeSQL(it.edition);
    std::string chapter = escapeSQL(it.chapter);
    std::string school = escapeSQL(it.school);
    std::string institution = escapeSQL(it.institution);
    std::string organization = escapeSQL(it.organization);
    std::string howpublished = escapeSQL(it.howpublished);
    std::string language = escapeSQL(it.language);
    std::string keywords = escapeSQL(it.keywords);
    std::string month = escapeSQL(it.month);
    std::string url = escapeSQL(it.url);
    std::string note = escapeSQL(it.note);
    std::string extra = escapeSQL(it.extra);
    std::string pdf_path = escapeSQL(it.pdf_path);
    std::string collection = escapeSQL(it.collection);

    std::string sql = "INSERT INTO items (id,title,authors,year,doi,isbn,type,abstract,address,publisher,editor,booktitle,series,edition,chapter,school,institution,organization,howpublished,language,journal,pages,volume,number,keywords,month,url,note,extra,pdf_path,collection) VALUES ('" +
        id + "','" + title + "','" + authors + "','" + year + "','" + doi + "','" + isbn + "','" + type + "','" + abstract + "','" + address + "','" + publisher + "','" + editor + "','" + booktitle + "','" + series + "','" + edition + "','" + chapter + "','" + school + "','" + institution + "','" + organization + "','" + howpublished + "','" + language + "','" + journal + "','" + pages + "','" + volume + "','" + number + "','" + keywords + "','" + month + "','" + url + "','" + note + "','" + extra + "','" + pdf_path + "','" + collection + "');";
    auto res = pimpl->conn->Query(sql);
    if (!res || res->HasError()) {
        std::cerr << "DB insert error: " << (res ? res->GetError() : std::string("<no result>")) << "\n";
    }
    // Also add to item_collections
    if (!it.collection.empty()) {
        addItemToCollection(it.id, it.collection);
    }
}

inline void Database::updateItem(const Item &it) {
    if (!it.collection.empty()) {
        std::string ins = "INSERT INTO collections (name) SELECT '" + it.collection + "' WHERE NOT EXISTS (SELECT 1 FROM collections WHERE name='" + it.collection + "');";
        pimpl->conn->Query(ins);
    }
    // Escape fields
    std::string title = escapeSQL(it.title);
    std::string authors = escapeSQL(it.authors);
    std::string year = escapeSQL(it.year);
    std::string doi = escapeSQL(it.doi);
    std::string isbn = escapeSQL(it.isbn);
    std::string type = escapeSQL(it.type);
    std::string abstract = escapeSQL(it.abstract);
    std::string address = escapeSQL(it.address);
    std::string publisher = escapeSQL(it.publisher);
    std::string editor = escapeSQL(it.editor);
    std::string booktitle = escapeSQL(it.booktitle);
    std::string series = escapeSQL(it.series);
    std::string edition = escapeSQL(it.edition);
    std::string chapter = escapeSQL(it.chapter);
    std::string school = escapeSQL(it.school);
    std::string institution = escapeSQL(it.institution);
    std::string organization = escapeSQL(it.organization);
    std::string howpublished = escapeSQL(it.howpublished);
    std::string language = escapeSQL(it.language);
    std::string journal = escapeSQL(it.journal);
    std::string pages = escapeSQL(it.pages);
    std::string volume = escapeSQL(it.volume);
    std::string number = escapeSQL(it.number);
    std::string keywords = escapeSQL(it.keywords);
    std::string month = escapeSQL(it.month);
    std::string url = escapeSQL(it.url);
    std::string note = escapeSQL(it.note);
    std::string extra = escapeSQL(it.extra);
    std::string pdf_path = escapeSQL(it.pdf_path);
    std::string collectionEsc = escapeSQL(it.collection);
    std::string id = escapeSQL(it.id);

    std::string sql = "UPDATE items SET title='" + title + "', authors='" + authors + "', year='" + year + "', doi='" + doi + "', isbn='" + isbn + "', type='" + type + "', abstract='" + abstract + "', address='" + address + "', publisher='" + publisher + "', editor='" + editor + "', booktitle='" + booktitle + "', series='" + series + "', edition='" + edition + "', chapter='" + chapter + "', school='" + school + "', institution='" + institution + "', organization='" + organization + "', howpublished='" + howpublished + "', language='" + language + "', journal='" + journal + "', pages='" + pages + "', volume='" + volume + "', number='" + number + "', keywords='" + keywords + "', month='" + month + "', url='" + url + "', note='" + note + "', extra='" + extra + "', pdf_path='" + pdf_path + "', collection='" + collectionEsc + "' WHERE id='" + id + "';";
    auto res = pimpl->conn->Query(sql);
    if (!res || res->HasError()) {
        std::cerr << "DB update error: " << (res ? res->GetError() : std::string("<no result>")) << "\n";
    }
}

inline std::vector<Item> Database::listItems() {
    std::vector<Item> out;
        auto res = pimpl->conn->Query("SELECT id,title,authors,year,type,pdf_path FROM items ORDER BY title");
    if (!res || res->HasError()) return out;
    auto rows = res->RowCount();
    for (size_t i = 0; i < rows; ++i) {
        Item it;
        it.id = res->GetValue(0, i).ToString();
        it.title = res->GetValue(1, i).ToString();
        it.authors = res->GetValue(2, i).ToString();
        it.year = res->GetValue(3, i).ToString();
        it.type = res->GetValue(4, i).ToString();
        it.pdf_path = res->GetValue(5, i).ToString();
        out.push_back(it);
    }
    return out;
}

inline std::vector<std::string> Database::listCollections() {
    std::vector<std::string> out;
    auto res = pimpl->conn->Query("SELECT name FROM collections ORDER BY name");
    if (!res || res->HasError()) return out;
    auto rows = res->RowCount();
    for (size_t i = 0; i < rows; ++i) {
        out.push_back(res->GetValue(0,i).ToString());
    }
    return out;
}

inline std::vector<Item> Database::listItemsInCollection(const std::string &collection) {
    std::vector<Item> out;
    // Use item_collections join table to find items
    // Include items from this collection AND all subcollections
    std::string sql = "SELECT DISTINCT i.id,i.title,i.authors,i.year,i.doi,i.isbn,i.type,i.abstract,i.address,i.publisher,i.editor,i.booktitle,i.series,i.edition,i.chapter,i.school,i.institution,i.organization,i.howpublished,i.language,i.journal,i.pages,i.volume,i.number,i.keywords,i.month,i.url,i.note,i.extra,i.pdf_path,i.collection "
                      "FROM items i JOIN item_collections ic ON i.id = ic.item_id "
                      "WHERE ic.collection='" + collection + "' OR ic.collection LIKE '" + collection + "/%' ORDER BY i.title";
    auto res = pimpl->conn->Query(sql);
    if (!res || res->HasError()) return out;
    auto rows = res->RowCount();
    for (size_t i = 0; i < rows; ++i) {
        Item it;
        it.id = res->GetValue(0, i).ToString();
        it.title = res->GetValue(1, i).ToString();
        it.authors = res->GetValue(2, i).ToString();
        it.year = res->GetValue(3, i).ToString();
        it.doi = res->GetValue(4, i).ToString();
        it.isbn = res->GetValue(5, i).ToString();
        it.type = res->GetValue(6, i).ToString();
        it.abstract = res->GetValue(7, i).ToString();
        it.address = res->GetValue(8, i).ToString();
        it.publisher = res->GetValue(9, i).ToString();
        it.editor = res->GetValue(10, i).ToString();
        it.booktitle = res->GetValue(11, i).ToString();
        it.series = res->GetValue(12, i).ToString();
        it.edition = res->GetValue(13, i).ToString();
        it.chapter = res->GetValue(14, i).ToString();
        it.school = res->GetValue(15, i).ToString();
        it.institution = res->GetValue(16, i).ToString();
        it.organization = res->GetValue(17, i).ToString();
        it.howpublished = res->GetValue(18, i).ToString();
        it.language = res->GetValue(19, i).ToString();
        it.journal = res->GetValue(20, i).ToString();
        it.pages = res->GetValue(21, i).ToString();
        it.volume = res->GetValue(22, i).ToString();
        it.number = res->GetValue(23, i).ToString();
        it.keywords = res->GetValue(24, i).ToString();
        it.month = res->GetValue(25, i).ToString();
        it.url = res->GetValue(26, i).ToString();
        it.note = res->GetValue(27, i).ToString();
        it.extra = res->GetValue(28, i).ToString();
        it.pdf_path = res->GetValue(29, i).ToString();
        it.collection = res->GetValue(30, i).ToString();
        out.push_back(it);
    }
    return out;
}

inline bool Database::getItem(const std::string &id, Item &out) {
    std::string sql = "SELECT id,title,authors,year,doi,isbn,type,abstract,address,publisher,editor,booktitle,series,edition,chapter,school,institution,organization,howpublished,language,journal,pages,volume,number,keywords,month,url,note,extra,pdf_path,collection FROM items WHERE id='" + id + "' LIMIT 1";
    auto res = pimpl->conn->Query(sql);
    if (!res || res->HasError() || res->RowCount() == 0) return false;
    out.id = res->GetValue(0, 0).ToString();
    out.title = res->GetValue(1, 0).ToString();
    out.authors = res->GetValue(2, 0).ToString();
    out.year = res->GetValue(3, 0).ToString();
    out.doi = res->GetValue(4, 0).ToString();
    out.isbn = res->GetValue(5, 0).ToString();
    out.type = res->GetValue(6, 0).ToString();
    out.abstract = res->GetValue(7, 0).ToString();
    out.address = res->GetValue(8, 0).ToString();
    out.publisher = res->GetValue(9, 0).ToString();
    out.editor = res->GetValue(10, 0).ToString();
    out.booktitle = res->GetValue(11, 0).ToString();
    out.series = res->GetValue(12, 0).ToString();
    out.edition = res->GetValue(13, 0).ToString();
    out.chapter = res->GetValue(14, 0).ToString();
    out.school = res->GetValue(15, 0).ToString();
    out.institution = res->GetValue(16, 0).ToString();
    out.organization = res->GetValue(17, 0).ToString();
    out.howpublished = res->GetValue(18, 0).ToString();
    out.language = res->GetValue(19, 0).ToString();
    out.journal = res->GetValue(20, 0).ToString();
    out.pages = res->GetValue(21, 0).ToString();
    out.volume = res->GetValue(22, 0).ToString();
    out.number = res->GetValue(23, 0).ToString();
    out.keywords = res->GetValue(24, 0).ToString();
    out.month = res->GetValue(25, 0).ToString();
    out.url = res->GetValue(26, 0).ToString();
    out.note = res->GetValue(27, 0).ToString();
    out.extra = res->GetValue(28, 0).ToString();
    out.pdf_path = res->GetValue(29, 0).ToString();
    out.collection = res->GetValue(30, 0).ToString();
    return true;
}

inline bool Database::findItemByDOI(const std::string &doi, Item &out) {
    if (doi.empty()) return false;
    std::string sql = "SELECT id,title,authors,year,doi,isbn,type,abstract,address,publisher,editor,booktitle,series,edition,chapter,school,institution,organization,howpublished,language,journal,pages,volume,number,keywords,month,url,note,extra,pdf_path,collection FROM items WHERE doi='" + doi + "' LIMIT 1";
    auto res = pimpl->conn->Query(sql);
    if (!res || res->HasError() || res->RowCount() == 0) return false;
    out.id = res->GetValue(0,0).ToString();
    out.title = res->GetValue(1,0).ToString();
    out.authors = res->GetValue(2,0).ToString();
    out.year = res->GetValue(3,0).ToString();
    out.doi = res->GetValue(4,0).ToString();
    out.isbn = res->GetValue(5,0).ToString();
    out.type = res->GetValue(6,0).ToString();
    out.abstract = res->GetValue(7,0).ToString();
    out.address = res->GetValue(8,0).ToString();
    out.publisher = res->GetValue(9,0).ToString();
    out.editor = res->GetValue(10,0).ToString();
    out.booktitle = res->GetValue(11,0).ToString();
    out.series = res->GetValue(12,0).ToString();
    out.edition = res->GetValue(13,0).ToString();
    out.chapter = res->GetValue(14,0).ToString();
    out.school = res->GetValue(15,0).ToString();
    out.institution = res->GetValue(16,0).ToString();
    out.organization = res->GetValue(17,0).ToString();
    out.howpublished = res->GetValue(18,0).ToString();
    out.language = res->GetValue(19,0).ToString();
    out.journal = res->GetValue(20,0).ToString();
    out.pages = res->GetValue(21,0).ToString();
    out.volume = res->GetValue(22,0).ToString();
    out.number = res->GetValue(23,0).ToString();
    out.keywords = res->GetValue(24,0).ToString();
    out.month = res->GetValue(25,0).ToString();
    out.url = res->GetValue(26,0).ToString();
    out.note = res->GetValue(27,0).ToString();
    out.extra = res->GetValue(28,0).ToString();
    out.pdf_path = res->GetValue(29,0).ToString();
    out.collection = res->GetValue(30,0).ToString();
    return true;
}

inline bool Database::findItemByISBN(const std::string &isbn, Item &out) {
    if (isbn.empty()) return false;
    std::string sql = "SELECT id,title,authors,year,doi,isbn,type,abstract,address,publisher,editor,booktitle,series,edition,chapter,school,institution,organization,howpublished,language,journal,pages,volume,number,keywords,month,url,note,extra,pdf_path,collection FROM items WHERE isbn='" + isbn + "' LIMIT 1";
    auto res = pimpl->conn->Query(sql);
    if (!res || res->HasError() || res->RowCount() == 0) return false;
    out.id = res->GetValue(0,0).ToString();
    out.title = res->GetValue(1,0).ToString();
    out.authors = res->GetValue(2,0).ToString();
    out.year = res->GetValue(3,0).ToString();
    out.doi = res->GetValue(4,0).ToString();
    out.isbn = res->GetValue(5,0).ToString();
    out.type = res->GetValue(6,0).ToString();
    out.abstract = res->GetValue(7,0).ToString();
    out.address = res->GetValue(8,0).ToString();
    out.publisher = res->GetValue(9,0).ToString();
    out.editor = res->GetValue(10,0).ToString();
    out.booktitle = res->GetValue(11,0).ToString();
    out.series = res->GetValue(12,0).ToString();
    out.edition = res->GetValue(13,0).ToString();
    out.chapter = res->GetValue(14,0).ToString();
    out.school = res->GetValue(15,0).ToString();
    out.institution = res->GetValue(16,0).ToString();
    out.organization = res->GetValue(17,0).ToString();
    out.howpublished = res->GetValue(18,0).ToString();
    out.language = res->GetValue(19,0).ToString();
    out.journal = res->GetValue(20,0).ToString();
    out.pages = res->GetValue(21,0).ToString();
    out.volume = res->GetValue(22,0).ToString();
    out.number = res->GetValue(23,0).ToString();
    out.keywords = res->GetValue(24,0).ToString();
    out.month = res->GetValue(25,0).ToString();
    out.url = res->GetValue(26,0).ToString();
    out.note = res->GetValue(27,0).ToString();
    out.extra = res->GetValue(28,0).ToString();
    out.pdf_path = res->GetValue(29,0).ToString();
    out.collection = res->GetValue(30,0).ToString();
    return true;
}

inline bool Database::findItemByTitleAndAuthor(const std::string &title, const std::string &authors, Item &out) {
    if (title.empty() || authors.empty()) return false;
    std::string sql = "SELECT id,title,authors,year,doi,isbn,type,abstract,address,publisher,editor,booktitle,series,edition,chapter,school,institution,organization,howpublished,language,journal,pages,volume,number,keywords,month,url,note,extra,pdf_path,collection FROM items WHERE title='" + title + "' AND authors='" + authors + "' LIMIT 1";
    auto res = pimpl->conn->Query(sql);
    if (!res || res->HasError() || res->RowCount() == 0) return false;
    out.id = res->GetValue(0,0).ToString();
    out.title = res->GetValue(1,0).ToString();
    out.authors = res->GetValue(2,0).ToString();
    out.year = res->GetValue(3,0).ToString();
    out.doi = res->GetValue(4,0).ToString();
    out.isbn = res->GetValue(5,0).ToString();
    out.type = res->GetValue(6,0).ToString();
    out.abstract = res->GetValue(7,0).ToString();
    out.address = res->GetValue(8,0).ToString();
    out.publisher = res->GetValue(9,0).ToString();
    out.editor = res->GetValue(10,0).ToString();
    out.booktitle = res->GetValue(11,0).ToString();
    out.series = res->GetValue(12,0).ToString();
    out.edition = res->GetValue(13,0).ToString();
    out.chapter = res->GetValue(14,0).ToString();
    out.school = res->GetValue(15,0).ToString();
    out.institution = res->GetValue(16,0).ToString();
    out.organization = res->GetValue(17,0).ToString();
    out.howpublished = res->GetValue(18,0).ToString();
    out.language = res->GetValue(19,0).ToString();
    out.journal = res->GetValue(20,0).ToString();
    out.pages = res->GetValue(21,0).ToString();
    out.volume = res->GetValue(22,0).ToString();
    out.number = res->GetValue(23,0).ToString();
    out.keywords = res->GetValue(24,0).ToString();
    out.month = res->GetValue(25,0).ToString();
    out.url = res->GetValue(26,0).ToString();
    out.note = res->GetValue(27,0).ToString();
    out.extra = res->GetValue(28,0).ToString();
    out.pdf_path = res->GetValue(29,0).ToString();
    out.collection = res->GetValue(30,0).ToString();
    return true;
}

inline bool Database::findItemByTitleAndCollection(const std::string &title, const std::string &collection, Item &out) {
    std::string sql = "SELECT id,title,authors,year,doi,isbn,type,abstract,address,publisher,editor,booktitle,series,edition,chapter,school,institution,organization,howpublished,language,journal,pages,volume,number,keywords,month,url,note,extra,pdf_path,collection FROM items WHERE title='" + title + "' AND collection='" + collection + "' LIMIT 1";
    auto res = pimpl->conn->Query(sql);
    if (!res || res->HasError() || res->RowCount() == 0) return false;
    out.id = res->GetValue(0,0).ToString();
    out.title = res->GetValue(1,0).ToString();
    out.authors = res->GetValue(2,0).ToString();
    out.year = res->GetValue(3,0).ToString();
    out.doi = res->GetValue(4,0).ToString();
    out.isbn = res->GetValue(5,0).ToString();
    out.type = res->GetValue(6,0).ToString();
    out.abstract = res->GetValue(7,0).ToString();
    out.address = res->GetValue(8,0).ToString();
    out.publisher = res->GetValue(9,0).ToString();
    out.editor = res->GetValue(10,0).ToString();
    out.booktitle = res->GetValue(11,0).ToString();
    out.series = res->GetValue(12,0).ToString();
    out.edition = res->GetValue(13,0).ToString();
    out.chapter = res->GetValue(14,0).ToString();
    out.school = res->GetValue(15,0).ToString();
    out.institution = res->GetValue(16,0).ToString();
    out.organization = res->GetValue(17,0).ToString();
    out.howpublished = res->GetValue(18,0).ToString();
    out.language = res->GetValue(19,0).ToString();
    out.journal = res->GetValue(20,0).ToString();
    out.pages = res->GetValue(21,0).ToString();
    out.volume = res->GetValue(22,0).ToString();
    out.number = res->GetValue(23,0).ToString();
    out.keywords = res->GetValue(24,0).ToString();
    out.month = res->GetValue(25,0).ToString();
    out.url = res->GetValue(26,0).ToString();
    out.note = res->GetValue(27,0).ToString();
    out.extra = res->GetValue(28,0).ToString();
    out.pdf_path = res->GetValue(29,0).ToString();
    out.collection = res->GetValue(30,0).ToString();
    return true;
}

inline void Database::renameCollection(const std::string &oldName, const std::string &newName) {
    if (oldName.empty() || newName.empty() || oldName == newName) return;
    try {
        // Use a transaction to ensure all operations succeed or fail together
        pimpl->conn->Query("BEGIN TRANSACTION");
        
        // First, rename the collection itself
        auto stmt1 = pimpl->conn->Prepare("UPDATE collections SET name = ? WHERE name = ?");
        stmt1->Execute(newName, oldName);
        
        // Then, rename items in this collection
        auto stmt2 = pimpl->conn->Prepare("UPDATE items SET collection = ? WHERE collection = ?");
        stmt2->Execute(newName, oldName);
        
        // For subcollections, use a simple approach: get all collections first
        auto allCollections = listCollections();
        std::string oldPrefix = oldName + "/";
        std::string newPrefix = newName + "/";
        
        for (const auto& collName : allCollections) {
            if (collName.length() > oldPrefix.length() && 
                collName.substr(0, oldPrefix.length()) == oldPrefix) {
                // This is a subcollection that needs to be renamed
                std::string newCollName = newPrefix + collName.substr(oldPrefix.length());
                
                auto updateStmt = pimpl->conn->Prepare("UPDATE collections SET name = ? WHERE name = ?");
                updateStmt->Execute(newCollName, collName);
                
                // Also update items in this subcollection
                auto updateItemsStmt = pimpl->conn->Prepare("UPDATE items SET collection = ? WHERE collection = ?");
                updateItemsStmt->Execute(newCollName, collName);
            }
        }
        
        pimpl->conn->Query("COMMIT");
        
    } catch (const std::exception &e) {
        try {
            pimpl->conn->Query("ROLLBACK");
        } catch (...) {}
    }
}

inline void Database::deleteCollection(const std::string &name) {
    if (name.empty()) return;
    try {
        // Use a transaction to ensure all operations succeed or fail together
        pimpl->conn->Query("BEGIN TRANSACTION");
        
        // First, delete the collection itself
        auto stmt1 = pimpl->conn->Prepare("DELETE FROM collections WHERE name=?");
        stmt1->Execute(name);
        
        // Move items in this collection back to root (empty collection)
        auto stmt2 = pimpl->conn->Prepare("UPDATE items SET collection='' WHERE collection=?");
        stmt2->Execute(name);
        
        // Handle subcollections - delete any collections that start with "name/"
        auto allCollections = listCollections();
        std::string prefix = name + "/";
        
        for (const auto& collName : allCollections) {
            if (collName.length() > prefix.length() && 
                collName.substr(0, prefix.length()) == prefix) {
                // This is a subcollection that needs to be deleted
                auto deleteStmt = pimpl->conn->Prepare("DELETE FROM collections WHERE name=?");
                deleteStmt->Execute(collName);
                
                // Move items in this subcollection back to root
                auto deleteItemsStmt = pimpl->conn->Prepare("UPDATE items SET collection='' WHERE collection=?");
                deleteItemsStmt->Execute(collName);
            }
        }
        
        pimpl->conn->Query("COMMIT");
        
    } catch (const std::exception &e) {
        try {
            pimpl->conn->Query("ROLLBACK");
        } catch (...) {}
    }
}

inline void Database::addCollection(const std::string &name) {
    if (name.empty()) return;
    try {
        auto stmt = pimpl->conn->Prepare("INSERT INTO collections (name) SELECT ? WHERE NOT EXISTS (SELECT 1 FROM collections WHERE name=?)");
        stmt->Execute(name, name);
    } catch (const std::exception &e) {
        // Handle error silently for now
    }
}

inline void Database::deleteItem(const std::string &id) {
    if (id.empty()) return;
    try {
        std::string q = "SELECT pdf_path FROM items WHERE id='" + id + "' LIMIT 1";
        auto res = pimpl->conn->Query(q);
        if (res && !res->HasError() && res->RowCount() > 0) {
            std::string path = res->GetValue(0,0).ToString();
            if (!path.empty()) {
                try { std::filesystem::remove(path); } catch(...) {}
            }
        }
    } catch(...) {}
    // Remove from item_collections first
    pimpl->conn->Query("DELETE FROM item_collections WHERE item_id='" + id + "'");
    std::string sql = "DELETE FROM items WHERE id='" + id + "'";
    pimpl->conn->Query(sql);
}

inline void Database::addItemToCollection(const std::string &itemId, const std::string &collection) {
    if (itemId.empty() || collection.empty()) return;
    try {
        // Ensure collection exists
        auto stmt1 = pimpl->conn->Prepare("INSERT INTO collections (name) SELECT ? WHERE NOT EXISTS (SELECT 1 FROM collections WHERE name=?)");
        stmt1->Execute(collection, collection);
        // Add to item_collections (ignore if already exists)
        pimpl->conn->Query("INSERT OR IGNORE INTO item_collections (item_id, collection) VALUES ('" + itemId + "', '" + collection + "')");
        // Update the primary collection field (for backward compatibility, use first collection)
        auto colls = getItemCollections(itemId);
        if (!colls.empty()) {
            pimpl->conn->Query("UPDATE items SET collection='" + colls[0] + "' WHERE id='" + itemId + "'");
        }
    } catch (...) {}
}

inline void Database::removeItemFromCollection(const std::string &itemId, const std::string &collection) {
    if (itemId.empty() || collection.empty()) return;
    try {
        pimpl->conn->Query("DELETE FROM item_collections WHERE item_id='" + itemId + "' AND collection='" + collection + "'");
        // Update the primary collection field (for backward compatibility)
        auto colls = getItemCollections(itemId);
        std::string newPrimary = colls.empty() ? "" : colls[0];
        pimpl->conn->Query("UPDATE items SET collection='" + newPrimary + "' WHERE id='" + itemId + "'");
    } catch (...) {}
}

inline std::vector<std::string> Database::getItemCollections(const std::string &itemId) {
    std::vector<std::string> out;
    if (itemId.empty()) return out;
    auto res = pimpl->conn->Query("SELECT collection FROM item_collections WHERE item_id='" + itemId + "' ORDER BY collection");
    if (!res || res->HasError()) return out;
    auto rows = res->RowCount();
    for (size_t i = 0; i < rows; ++i) {
        out.push_back(res->GetValue(0, i).ToString());
    }
    return out;
}

