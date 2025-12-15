#include <QApplication>
#include "MainWindow.h"
#include "Importers.h"
#include <cstdlib>
#include <filesystem>
#include <string>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    
    // Set application metadata for Bello (Zotero fork)
    app.setApplicationName("Bello");
    app.setApplicationDisplayName("Bello Reference Manager");
    app.setApplicationVersion("1.0.0");
    app.setOrganizationName("Bello Project");
    app.setOrganizationDomain("bello.project");
    
    const char *home = std::getenv("HOME");
    std::string dbPath = std::string(home ? home : ".") + "/.local/share/bello/bello.db";
    // Ensure parent directory exists
    { std::filesystem::path p(dbPath); std::filesystem::create_directories(p.parent_path()); }

    // Support destructive reset: either via env var BELLO_RESET_DB=1 or CLI flag --reset-db
    bool resetDb = false;
    const char *envReset = std::getenv("BELLO_RESET_DB");
    if (envReset && std::string(envReset) == "1") resetDb = true;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--reset-db") { resetDb = true; break; }
    }
    if (resetDb) {
        try {
            std::filesystem::remove(dbPath);
        } catch (...) {}
    }
    // Headless parser test: if BELLO_PARSE_FILE is set, parse and print entries then exit
    const char *parseFile = std::getenv("BELLO_PARSE_FILE");
    if (parseFile && std::string(parseFile).size() > 0) {
        std::string p(parseFile);
        std::string lower = p;
        for (auto &c : lower) c = std::tolower((unsigned char)c);
        std::vector<Item> items;
        if (lower.size() >= 4 && lower.substr(lower.size()-4) == ".bib") {
            items = parseBibTeXFile(QString::fromStdString(p));
        } else if (lower.size() >= 4 && (lower.substr(lower.size()-4) == ".rdf" || lower.substr(lower.size()-4) == ".xml")) {
            // try RDF then EndNote/Mendeley
            items = parseZoteroRDFFile(QString::fromStdString(p));
            if (items.empty()) items = parseEndNoteXMLFile(QString::fromStdString(p));
            if (items.empty()) items = parseMendeleyXMLFile(QString::fromStdString(p));
        }
        std::cout << "Parsed " << items.size() << " items from '" << p << "'\n";
        for (size_t i = 0; i < items.size(); ++i) {
            const auto &it = items[i];
            std::cout << "--- Item " << i+1 << " ---\n";
            std::cout << "id: " << it.id << "\n";
            std::cout << "title: " << it.title << "\n";
            std::cout << "authors: " << it.authors << "\n";
            std::cout << "year: " << it.year << "\n";
            std::cout << "doi: " << it.doi << "\n";
            std::cout << "isbn: " << it.isbn << "\n";
            std::cout << "pdf_path: " << it.pdf_path << "\n";
        }
        // If BELLO_TEST_IMPORT==1, try inserting into a temp DB and report how many persisted
        const char *doImport = std::getenv("BELLO_TEST_IMPORT");
        if (doImport && std::string(doImport) == "1") {
            std::string tmpdb = std::string(std::getenv("HOME")) + "/.local/share/bello/test-bello.db";
            try { std::filesystem::remove(tmpdb); } catch(...) {}
            Database testdb(tmpdb);
            testdb.init();
            for (auto &it : items) {
                it.id = std::string(); // ensure addItem will set id if needed
                // generate ids the same way import path does
                it.id = ""; // will be set below
                it.id = std::string();
                // generate uuid here
                
            }
            // Add items with generated ids and default collection 'Test'
            int idx = 0;
            for (auto it : items) {
                // generate a simple id
                it.id = std::to_string(std::chrono::high_resolution_clock::now().time_since_epoch().count()) + "-" + std::to_string(idx);
                it.collection = "Test";
                testdb.addItem(it);
                ++idx;
            }
            auto persisted = testdb.listItemsInCollection("Test");
            std::cout << "Persisted " << persisted.size() << " items into temp DB at " << tmpdb << "\n";
            for (size_t i = 0; i < persisted.size(); ++i) {
                auto &it = persisted[i];
                std::cout << "DB Item " << i+1 << ": title='" << it.title << "' doi='" << it.doi << "' isbn='" << it.isbn << "' pdf='" << it.pdf_path << "'\n";
            }
        }
        return 0;
    }
    MainWindow w(dbPath);
    w.resize(900, 600);
    w.show();
    return app.exec();
}
