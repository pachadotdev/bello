#pragma once

#include "Database.h"
#include <QString>
#include <QFile>
#include <QTextStream>
#include <QFileInfo>
#include <QDir>
#include <QRegularExpression>
#include <QMap>
#include <cstdlib>
#include <filesystem>
#include <vector>

// Importers returning parsed Items (id and collection left empty).

inline std::vector<Item> parseBibTeXFile(const QString &path) {
    std::vector<Item> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QByteArray all = f.readAll();
    QString content = QString::fromUtf8(all);
    int pos = 0;
    int len = content.size();

    // storage base for copying attached files
    std::filesystem::path storage = std::filesystem::path(std::getenv("HOME")) / ".local" / "share" / "bello" / "storage";
    std::filesystem::create_directories(storage);

    // Clean a BibTeX field value: strip outer braces/quotes, unescape
    auto cleanValue = [](QString s) -> QString {
        s = s.trimmed();
        // Remove ALL outer braces iteratively (handles {{text}} -> text)
        while (s.size() >= 2 && s.startsWith('{') && s.endsWith('}')) {
            s = s.mid(1, s.size() - 2);
        }
        // Remove outer quotes
        while (s.size() >= 2 && s.startsWith('"') && s.endsWith('"')) {
            s = s.mid(1, s.size() - 2);
        }
        // Unescape common LaTeX
        s.replace("\\{", "{").replace("\\}", "}").replace("\\%", "%");
        s.replace("\\&", "&").replace("\\_", "_").replace("\\$", "$");
        // Remove a trailing comma if present (messy BibTeX often leaves a trailing comma)
        s = s.trimmed();
        if (s.endsWith(',')) s.chop(1);

        // Remove any remaining braces used to protect capitalization (e.g. "{Mathematical}" -> "Mathematical")
        s.replace('{', ' ');
        s.replace('}', ' ');

        // Collapse multiple whitespace into single space and trim again
        s = s.replace(QRegularExpression("\\s+"), " ").trimmed();

        return s;
    };

    auto sanitizeName = [](const QString &in) -> QString {
        QString s = in;
        s = s.replace(QRegularExpression("[^A-Za-z0-9_\\-]"), "_");
        // Collapse multiple underscores
        s = s.replace(QRegularExpression("_+"), "_");
        return s;
    };

    while (true) {
        int at = content.indexOf('@', pos);
        if (at < 0) break;

        // Find the opening delimiter (either '{' or '(' )
        int startBrace = content.indexOf('{', at);
        int startParen = content.indexOf('(', at);
        int start = -1;
        QChar openChar = '{';
        QChar closeChar = '}';
        if (startBrace >= 0 && (startParen < 0 || startBrace < startParen)) {
            start = startBrace;
            openChar = '{'; closeChar = '}';
        } else if (startParen >= 0) {
            start = startParen;
            openChar = '('; closeChar = ')';
        }
        if (start < 0) break;

        // Find matching close, accounting for nested pairs of the chosen delimiter
        int i = start + 1;
        int depth = 1;
        while (i < len && depth > 0) {
            QChar c = content.at(i);
            if (c == openChar) depth++;
            else if (c == closeChar) depth--;
            ++i;
        }
        if (depth != 0) break;

        // Extract the entry content (without outer braces)
        QString entryBlock = content.mid(start + 1, i - start - 2);

        // Extract entry type (word after '@' and before the opening brace/paren)
        QString entryType = content.mid(at + 1, start - at - 1).trimmed().toLower();

        // Find the citation key (everything before first comma)
        int comma = entryBlock.indexOf(',');
        QString citationKey = (comma >= 0) ? entryBlock.left(comma).trimmed() : QString();
        QString fields = (comma >= 0) ? entryBlock.mid(comma + 1) : entryBlock;

        Item cur;
        cur.type = entryType.toStdString();
        int j = 0;
        int flen = fields.size();

        auto skipWs = [&]() { while (j < flen && fields.at(j).isSpace()) ++j; };

        while (j < flen) {
            skipWs();
            if (j >= flen) break;

            // Parse field name
            int nameStart = j;
            while (j < flen && (fields.at(j).isLetterOrNumber() || fields.at(j) == '_' || fields.at(j) == '-')) ++j;
            QString name = fields.mid(nameStart, j - nameStart).trimmed().toLower();
            
            skipWs();
            if (j >= flen || fields.at(j) != '=') {
                // Skip to next comma or end
                while (j < flen && fields.at(j) != ',') ++j;
                if (j < flen) ++j;
                continue;
            }
            ++j; // skip '='
            skipWs();

            // Parse field value
            QString value;
            if (j < flen && fields.at(j) == '{') {
                // Brace-delimited value - find matching close
                int vstart = j + 1;
                int vdepth = 1;
                ++j;
                while (j < flen && vdepth > 0) {
                    if (fields.at(j) == '{') vdepth++;
                    else if (fields.at(j) == '}') vdepth--;
                    if (vdepth > 0) ++j;
                }
                value = fields.mid(vstart, j - vstart);
                if (j < flen) ++j; // skip closing }
            } else if (j < flen && fields.at(j) == '"') {
                // Quote-delimited value
                int vstart = j + 1;
                ++j;
                while (j < flen && fields.at(j) != '"') {
                    if (fields.at(j) == '\\' && j + 1 < flen) j += 2;
                    else ++j;
                }
                value = fields.mid(vstart, j - vstart);
                if (j < flen) ++j; // skip closing "
            } else {
                // Unquoted value (number or string concatenation)
                int vstart = j;
                // Stop at comma, but not at } (which ends the entry, handled by outer loop)
                while (j < flen && fields.at(j) != ',') {
                    // Handle nested braces if someone writes year = {2020}
                    if (fields.at(j) == '{') {
                        int vdepth = 1;
                        ++j;
                        while (j < flen && vdepth > 0) {
                            if (fields.at(j) == '{') vdepth++;
                            else if (fields.at(j) == '}') vdepth--;
                            ++j;
                        }
                    } else {
                        ++j;
                    }
                }
                value = fields.mid(vstart, j - vstart);
            }

            value = cleanValue(value);

            // Assign to Item fields (include common BibTeX keys)
            if (name == "title") cur.title = value.toStdString();
            else if (name == "author") cur.authors = value.toStdString();
            else if (name == "year") cur.year = value.toStdString();
            else if (name == "doi") cur.doi = value.toStdString();
            else if (name == "isbn") cur.isbn = value.toStdString();
            else if (name == "abstract") cur.abstract = value.toStdString();
            else if (name == "address") cur.address = value.toStdString();
            else if (name == "publisher") cur.publisher = value.toStdString();
            else if (name == "editor") cur.editor = value.toStdString();
            else if (name == "booktitle") cur.booktitle = value.toStdString();
            else if (name == "series") cur.series = value.toStdString();
            else if (name == "edition") cur.edition = value.toStdString();
            else if (name == "chapter") cur.chapter = value.toStdString();
            else if (name == "school") cur.school = value.toStdString();
            else if (name == "institution") cur.institution = value.toStdString();
            else if (name == "organization") cur.organization = value.toStdString();
            else if (name == "howpublished") cur.howpublished = value.toStdString();
            else if (name == "language") cur.language = value.toStdString();
            else if (name == "url") cur.url = value.toStdString();
            else if (name == "journal") cur.journal = value.toStdString();
            else if (name == "pages") cur.pages = value.toStdString();
            else if (name == "volume") cur.volume = value.toStdString();
            else if (name == "number") cur.number = value.toStdString();
            else if (name == "keywords") cur.keywords = value.toStdString();
            else if (name == "month") cur.month = value.toStdString();
            else if (name == "note") cur.note = value.toStdString();
            else if (name == "file") {
                // Zotero file field format: "Desc:path:mime;Desc2:path2:mime2"
                auto parts = value.split(';', Qt::SkipEmptyParts);
                for (const QString &p : parts) {
                    QString seg = p.trimmed();
                    QStringList cols = seg.split(':');
                    QString pathCandidate;
                    if (cols.size() >= 3) {
                        // Format: Description:path:mimetype
                        pathCandidate = cols[1];
                    } else if (cols.size() == 2) {
                        pathCandidate = cols[1];
                    } else {
                        pathCandidate = seg;
                    }
                    pathCandidate = pathCandidate.trimmed();
                    if (pathCandidate.isEmpty()) continue;

                    // Resolve relative to .bib file location
                    QFileInfo bibfi(path);
                    QDir bibDir(bibfi.absolutePath());
                    QString absPath = bibDir.absoluteFilePath(pathCandidate);

                    if (QFile::exists(absPath)) {
                        // Determine storage folder name
                        QString baseName;
                        if (!cur.doi.empty()) {
                            baseName = sanitizeName(QString::fromStdString(cur.doi));
                        } else if (!cur.isbn.empty()) {
                            baseName = sanitizeName(QString::fromStdString(cur.isbn));
                        } else if (!citationKey.isEmpty()) {
                            baseName = sanitizeName(citationKey);
                        } else {
                            QString a = QString::fromStdString(cur.authors).section(',', 0, 0).trimmed();
                            if (a.isEmpty()) a = "unknown";
                            QString y = QString::fromStdString(cur.year);
                            if (y.isEmpty()) y = "0000";
                            baseName = sanitizeName(a + "_" + y);
                        }

                        std::filesystem::path targetDir = storage / baseName.toStdString();
                        std::filesystem::create_directories(targetDir);

                        QFileInfo src(absPath);
                        std::filesystem::path dest = targetDir / src.fileName().toStdString();

                        // Avoid overwrite
                        int idx = 1;
                        while (std::filesystem::exists(dest)) {
                            std::string stem = src.completeBaseName().toStdString();
                            std::string ext = src.suffix().isEmpty() ? "" : "." + src.suffix().toStdString();
                            dest = targetDir / (stem + "_" + std::to_string(idx) + ext);
                            ++idx;
                        }

                        try {
                            std::filesystem::copy_file(absPath.toStdString(), dest);
                            if (cur.pdf_path.empty()) {
                                cur.pdf_path = dest.string();
                            } else {
                                // Append additional files separated by ;
                                cur.pdf_path += ";" + dest.string();
                            }
                        } catch (...) {
                            // Ignore copy errors
                        }
                    }
                }
            } else {
                // unknown field: append to note as plain text for round-trip fidelity
                QString pair = QString("%1 = {%2}").arg(name, value);
                if (cur.note.empty()) cur.note = pair.toStdString();
                else cur.note += std::string("; ") + pair.toStdString();
            }

            // Skip trailing comma
            skipWs();
            if (j < flen && fields.at(j) == ',') ++j;
        }

        // Push entry if it has any meaningful data (title/authors/identifiers/files/notes)
        if (!cur.title.empty() || !cur.authors.empty() || !cur.doi.empty() || !cur.isbn.empty() || !cur.pdf_path.empty() || !citationKey.isEmpty() || !cur.url.empty() || !cur.note.empty()) {
            out.push_back(cur);
        }
        pos = i;
    }

    return out;
}

inline std::vector<Item> parseZoteroRDFFile(const QString &path) {
    std::vector<Item> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QString content = QString::fromUtf8(f.readAll());

    // First pass: collect attachments mapping (about id -> list of resource paths)
    QMap<QString, QStringList> attachMap;
    QRegularExpression attachRx("<z:Attachment[^>]*rdf:about=\\\"([^\\\"]+)\\\"[\\s\\S]*?</z:Attachment>", QRegularExpression::DotMatchesEverythingOption);
    QRegularExpression resourceRx("files/[^\"'\\s>]+");
    auto attachIt = attachRx.globalMatch(content);
    while (attachIt.hasNext()) {
        auto m = attachIt.next();
        QString about = m.captured(1); // e.g. #item_217
        QString block = m.captured(0);
        QRegularExpressionMatch resMatch = resourceRx.match(block);
        if (resMatch.hasMatch()) {
            QString rel = resMatch.captured(0);
            attachMap[about].append(rel);
        }
    }

    // Second pass: parse items
    QTextStream ts(&f);
    f.seek(0);
    Item cur;
    QString curAbout;
    QStringList pendingAttachIds;
    while (!ts.atEnd()) {
        QString line = ts.readLine();
        if (line.contains("<rdf:Description") && line.contains("rdf:about=")) {
            // New description block
            // push previous
            if (!cur.title.empty() || !cur.authors.empty() || !cur.doi.empty() || !cur.isbn.empty()) {
                // Attach any pending files
                for (const QString &aid : pendingAttachIds) {
                    if (attachMap.contains(aid)) {
                        for (const QString &rel : attachMap[aid]) {
                            QFileInfo rdfFi(path);
                            QDir rdfDir(rdfFi.absolutePath());
                            QString abs = rdfDir.absoluteFilePath(rel);
                            if (QFile::exists(abs)) {
                                if (!cur.pdf_path.empty()) cur.pdf_path += ";";
                                cur.pdf_path += abs.toStdString();
                            }
                        }
                    }
                }
                out.push_back(cur);
            }
            cur = Item{};
            curAbout.clear();
            pendingAttachIds.clear();
            // capture about id if present
            QRegularExpression aboutRx("rdf:about=\"([^\"]+)\"");
            QRegularExpressionMatch am = aboutRx.match(line);
            if (am.hasMatch()) curAbout = am.captured(1);
        }
        if (line.contains("<dc:title>")) {
            cur.title = line.section("<dc:title>", 1).section("</dc:title>", 0, 0).trimmed().toStdString();
        }
        if (line.contains("<dc:creator>")) {
            cur.authors = line.section("<dc:creator>", 1).section("</dc:creator>", 0, 0).trimmed().toStdString();
        }
        if (line.contains("<dc:date>")) {
            cur.year = line.section("<dc:date>", 1).section("</dc:date>", 0, 0).trimmed().left(4).toStdString();
        }
        if (line.contains("<dc:publisher>") || line.contains("<bib:publisher>") || line.contains("<dcterms:publisher>")) {
            QString v = line;
            v.remove(QRegularExpression("<[^>]+>"));
            cur.publisher = v.trimmed().toStdString();
        }
        if (line.contains("<bib:doi>") || line.contains("<dc:identifier>")) {
            // Try to pick DOI or ISBN-like identifier
            QString idval = line;
            idval.remove(QRegularExpression("<[^>]+>"));
            idval = idval.trimmed();
            if (idval.contains("ISBN", Qt::CaseInsensitive)) {
                // extract digits and hyphens
                QRegularExpression isbnRx("(97[89][- ]?[0-9][-0-9 ]+)");
                auto m = isbnRx.match(idval);
                if (m.hasMatch()) cur.isbn = m.captured(1).trimmed().toStdString();
            } else if (idval.contains("10.") || idval.contains("doi:" , Qt::CaseInsensitive)) {
                // crude DOI extraction
                QRegularExpression doiRx("(10\\.[^\\s]+)");
                auto m = doiRx.match(idval);
                if (m.hasMatch()) cur.doi = m.captured(1).trimmed().toStdString();
            }
        }
        if (line.contains("link:link") && line.contains("rdf:resource=")) {
            // references an attachment id e.g. rdf:resource="#item_217"
            QRegularExpression linkRx("rdf:resource=\"([^\"]+)\"");
            auto lm = linkRx.match(line);
            if (lm.hasMatch()) {
                QString aid = lm.captured(1);
                pendingAttachIds << aid;
            }
        }
    }
    // push last
    if (!cur.title.empty() || !cur.authors.empty() || !cur.doi.empty() || !cur.isbn.empty()) {
        for (const QString &aid : pendingAttachIds) {
            if (attachMap.contains(aid)) {
                for (const QString &rel : attachMap[aid]) {
                    QFileInfo rdfFi(path);
                    QDir rdfDir(rdfFi.absolutePath());
                    QString abs = rdfDir.absoluteFilePath(rel);
                    if (QFile::exists(abs)) {
                        if (!cur.pdf_path.empty()) cur.pdf_path += ";";
                        cur.pdf_path += abs.toStdString();
                    }
                }
            }
        }
        out.push_back(cur);
    }
    return out;
}

inline std::vector<Item> parseEndNoteXMLFile(const QString &path) {
    std::vector<Item> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    Item cur;
    while (!ts.atEnd()) {
        QString line = ts.readLine();
        if (line.contains("<record>")) {
            if (!cur.title.empty() || !cur.authors.empty()) out.push_back(cur);
            cur = Item{};
        }
        if (line.contains("<title>")) {
            cur.title = line.section("<title>", 1).section("</title>", 0, 0).trimmed().toStdString();
        }
        if (line.contains("<author>")) {
            cur.authors = line.section("<author>", 1).section("</author>", 0, 0).trimmed().toStdString();
        }
        if (line.contains("<year>")) {
            cur.year = line.section("<year>", 1).section("</year>", 0, 0).trimmed().toStdString();
        }
        if (line.contains("<publisher>")) {
            cur.publisher = line.section("<publisher>", 1).section("</publisher>", 0, 0).trimmed().toStdString();
        }
        if (line.contains("<electronic-resource-num>")) {
            cur.doi = line.section("<electronic-resource-num>", 1).section("</electronic-resource-num>", 0, 0).trimmed().toStdString();
        }
    }
    if (!cur.title.empty() || !cur.authors.empty()) out.push_back(cur);
    return out;
}

inline std::vector<Item> parseMendeleyXMLFile(const QString &path) {
    std::vector<Item> out;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return out;
    QTextStream ts(&f);
    Item cur;
    while (!ts.atEnd()) {
        QString line = ts.readLine();
        if (line.contains("<document>")) {
            if (!cur.title.empty() || !cur.authors.empty()) out.push_back(cur);
            cur = Item{};
        }
        if (line.contains("<title>")) {
            cur.title = line.section("<title>", 1).section("</title>", 0, 0).trimmed().toStdString();
        }
        if (line.contains("<authors>")) {
            QString a = line;
            a.remove("<authors>").remove("</authors>").remove("<author>").remove("</author>");
            cur.authors = a.trimmed().toStdString();
        }
        if (line.contains("<publisher>")) {
            cur.publisher = line.section("<publisher>", 1).section("</publisher>", 0, 0).trimmed().toStdString();
        }
        if (line.contains("<year>")) {
            cur.year = line.section("<year>", 1).section("</year>", 0, 0).trimmed().toStdString();
        }
        if (line.contains("<doi>")) {
            cur.doi = line.section("<doi>", 1).section("</doi>", 0, 0).trimmed().toStdString();
        }
    }
    if (!cur.title.empty() || !cur.authors.empty()) out.push_back(cur);
    return out;
}
