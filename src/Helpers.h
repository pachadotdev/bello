#pragma once

#include <algorithm>
#include <string>
#include <QSettings>
#include <QRegularExpression>
#include "UUID.h"

inline QString MainWindow::formatCitation(const Item &it) {
    QString s;
    if (!it.authors.empty()) s += QString::fromStdString(it.authors) + ". ";
    if (!it.title.empty()) s += QString::fromStdString(it.title);
    if (!it.year.empty()) s += " (" + QString::fromStdString(it.year) + ")";
    return s;
}

inline QString MainWindow::itemToBibTeX(const Item &it) {
    QString type = QString::fromStdString(it.type).toLower();
    if (type.isEmpty()) type = "misc";

    // Determine citation key based on user preference stored in QSettings
    QSettings settings("bello", "bello");
    int pref = settings.value("export/bibkey", 1).toInt();

    auto sanitizeKey = [](const QString &s) {
        QString k = s.toLower();
        // keep alnum and underscore
        k = k.replace(QRegularExpression("[^a-z0-9_]+"), "_");
        // collapse underscores
        k = k.replace(QRegularExpression("_+"), "_");
        // trim underscores
        k = k.trimmed();
        while (k.startsWith('_')) k.remove(0,1);
        while (k.endsWith('_')) k.chop(1);
        if (k.isEmpty()) k = "key";
        return k;
    };

    QString key;
    if (pref == 2) {
        // prefer DOI or ISBN
        if (!QString::fromStdString(it.doi).trimmed().isEmpty()) key = sanitizeKey(QString::fromStdString(it.doi));
        else if (!QString::fromStdString(it.isbn).trimmed().isEmpty()) key = sanitizeKey(QString::fromStdString(it.isbn));
    }
    if (key.isEmpty()) {
        // fallback: author + simplified title + year
        QString author = QString::fromStdString(it.authors).trimmed();
        QString authorLast = "";
        if (!author.isEmpty()) {
            // try to extract last name from formats like "Last, First" or "First Last"
            if (author.contains(',')) {
                authorLast = author.split(',').first().trimmed();
            } else {
                QStringList parts = author.split(' ', Qt::SkipEmptyParts);
                if (!parts.isEmpty()) authorLast = parts.last();
            }
        }
        authorLast = sanitizeKey(authorLast);

        QString title = QString::fromStdString(it.title).trimmed();
        QString titleToken = "";
        if (!title.isEmpty()) {
            // take first alphanumeric token
            QString t = title.toLower();
            t = t.replace(QRegularExpression("[^a-z0-9\\s]+"), " ");
            QStringList toks = t.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            if (!toks.isEmpty()) titleToken = sanitizeKey(toks.first());
        }
        QString year = QString::fromStdString(it.year).trimmed();

        QStringList parts;
        if (!authorLast.isEmpty()) parts << authorLast;
        if (!titleToken.isEmpty()) parts << titleToken;
        if (!year.isEmpty()) parts << sanitizeKey(year);
        key = parts.join("_");
        if (key.isEmpty()) key = sanitizeKey(QString::fromStdString(it.id));
    }

    // Build fields without trailing commas, preferring canonical order per entry type
    QStringList fieldOrder;
    if (type == "article") {
        fieldOrder = {"author","title","journal","year","volume","number","pages","doi","url","abstract","keywords","note"};
    } else if (type == "book") {
        fieldOrder = {"author","title","publisher","address","year","volume","series","edition","isbn","url","abstract","keywords","note"};
    } else if (type == "inproceedings" || type == "conference") {
        fieldOrder = {"author","title","booktitle","year","pages","publisher","address","doi","url","abstract","keywords","note"};
    } else if (type == "techreport") {
        fieldOrder = {"author","title","institution","year","number","address","url","note"};
    } else if (type == "phdthesis" || type == "mastersthesis") {
        fieldOrder = {"author","title","school","year","address","month","note","url"};
    } else {
        // misc and fallback
        fieldOrder = {"author","title","howpublished","year","month","note","url","doi","isbn","abstract","keywords"};
    }

    // Helper to append a field if present
    auto appendField = [&](const QString &fname) {
        if (fname == "author" && !it.authors.empty()) return QString("  author = {%1}").arg(QString::fromStdString(it.authors));
        if (fname == "title" && !it.title.empty()) return QString("  title = {%1}").arg(QString::fromStdString(it.title));
        if (fname == "journal" && !it.journal.empty()) return QString("  journal = {%1}").arg(QString::fromStdString(it.journal));
        if (fname == "year" && !it.year.empty()) return QString("  year = {%1}").arg(QString::fromStdString(it.year));
        if (fname == "volume" && !it.volume.empty()) return QString("  volume = {%1}").arg(QString::fromStdString(it.volume));
        if (fname == "number" && !it.number.empty()) return QString("  number = {%1}").arg(QString::fromStdString(it.number));
        if (fname == "pages" && !it.pages.empty()) return QString("  pages = {%1}").arg(QString::fromStdString(it.pages));
        if (fname == "doi" && !it.doi.empty()) return QString("  doi = {%1}").arg(QString::fromStdString(it.doi));
        if (fname == "isbn" && !it.isbn.empty()) return QString("  isbn = {%1}").arg(QString::fromStdString(it.isbn));
        if (fname == "publisher" && !it.publisher.empty()) return QString("  publisher = {%1}").arg(QString::fromStdString(it.publisher));
        if (fname == "address" && !it.address.empty()) return QString("  address = {%1}").arg(QString::fromStdString(it.address));
        if (fname == "institution" && !it.publisher.empty()) return QString("  institution = {%1}").arg(QString::fromStdString(it.publisher));
        if (fname == "booktitle" && !it.journal.empty()) return QString("  booktitle = {%1}").arg(QString::fromStdString(it.journal));
        if (fname == "school" && !it.publisher.empty()) return QString("  school = {%1}").arg(QString::fromStdString(it.publisher));
        if (fname == "howpublished" && !it.url.empty()) return QString("  howpublished = {%1}").arg(QString::fromStdString(it.url));
        if (fname == "url" && !it.url.empty()) return QString("  url = {%1}").arg(QString::fromStdString(it.url));
        if (fname == "abstract" && !it.abstract.empty()) return QString("  abstract = {%1}").arg(QString::fromStdString(it.abstract));
        if (fname == "keywords" && !it.keywords.empty()) return QString("  keywords = {%1}").arg(QString::fromStdString(it.keywords));
        if (fname == "note" && !it.note.empty()) return QString("  note = {%1}").arg(QString::fromStdString(it.note));
        return QString();
    };

    QList<QString> fields;
    for (const QString &f : fieldOrder) {
        QString built = appendField(f);
        if (!built.isEmpty()) fields << built;
    }

    // Include any extra JSON fields (preserve insertion order by key sort)
    if (!it.extra.empty()) {
        QJsonParseError perr; QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(it.extra), &perr);
        if (!d.isNull() && d.isObject()) {
            QJsonObject obj = d.object();
            QStringList keys = obj.keys();
            std::sort(keys.begin(), keys.end());
            for (const QString &k : keys) {
                QJsonValue v = obj.value(k);
                if (v.isString()) fields << QString("  %1 = {%2}").arg(k, v.toString());
                else fields << QString("  %1 = {%2}").arg(k, QString::fromUtf8(QJsonDocument(v.toObject()).toJson(QJsonDocument::Compact)));
            }
        }
    }

    QString out = QString("@%1{%2,\n").arg(type, key);
    for (int i = 0; i < fields.size(); ++i) {
        out += fields[i];
        if (i != fields.size() - 1) out += ",\n";
        else out += "\n";
    }
    out += "}";
    return out;
}
