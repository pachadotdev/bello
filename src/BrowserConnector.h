#pragma once

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QList>
#include <QFile>
#include <QDir>
#include <functional>
#include <iostream>
#include <memory>
#include "UUID.h"
#include "Database.h"

class BrowserConnector : public QObject {
public:
    BrowserConnector(Database *db, std::function<void()> reloadCb, std::function<void(const std::string&)> selectCb, QObject *parent = nullptr)
        : QObject(parent), db(db), reloadCb(std::move(reloadCb)), selectCb(std::move(selectCb)) {
        server = new QTcpServer(this);
        const quint16 connectorPort = 1842;
        if (!server->listen(QHostAddress::LocalHost, connectorPort)) {
            qWarning("Connector server failed to listen on port %d", connectorPort);
        } else {
            qDebug("Connector server listening on port %d", connectorPort);
        }

        connect(server, &QTcpServer::newConnection, this, [this]() {
            while (server->hasPendingConnections()) {
                QTcpSocket *socket = server->nextPendingConnection();
                
                // Use a shared buffer to accumulate data for large requests
                auto buffer = std::make_shared<QByteArray>();
                auto processed = std::make_shared<bool>(false);
                
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, processed]() {
                    if (*processed) return; // Already handled this request
                    
                    buffer->append(socket->readAll());
                    
                    // Check if we have headers yet
                    const QByteArray sep = "\r\n\r\n";
                    int idx = buffer->indexOf(sep);
                    if (idx == -1) return; // wait for headers
                    
                    // Parse Content-Length from headers
                    QByteArray header = buffer->left(idx);
                    int contentLength = 0;
                    QList<QByteArray> headerLines = header.split('\n');
                    for (const QByteArray &line : headerLines) {
                        if (line.toLower().startsWith("content-length:")) {
                            contentLength = line.mid(15).trimmed().toInt();
                            break;
                        }
                    }
                    
                    // Check if we have the complete body
                    int bodyStart = idx + sep.size();
                    int receivedBody = buffer->size() - bodyStart;
                    if (receivedBody < contentLength) {
                        // Wait for more data
                        std::cerr << "  waiting for more data: have " << receivedBody << " of " << contentLength << std::endl;
                        return;
                    }
                    
                    *processed = true; // Mark as processed to avoid re-entry
                    
                    // We have the complete request - process it
                    QByteArray body = buffer->mid(bodyStart, contentLength);
                    QList<QByteArray> lines = header.split('\n');
                    QByteArray reqLine = lines.size() ? lines[0].trimmed() : QByteArray();
                    QList<QByteArray> parts = reqLine.split(' ');
                    if (parts.size() < 2) { socket->disconnectFromHost(); return; }
                    QByteArray method = parts[0];
                    QByteArray path = parts[1];

                    if (method == "GET" && path == "/connector/status") {
                        QJsonObject obj; obj["version"] = "1.0.0";
                        QJsonDocument doc(obj); QByteArray out = doc.toJson(QJsonDocument::Compact);
                        QByteArray resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(out.size()) + "\r\n\r\n" + out;
                        socket->write(resp); socket->flush(); socket->disconnectFromHost(); return;
                    }

                    if (method == "GET" && path.startsWith("/connector/items")) {
                        int qidx = path.indexOf('?');
                        int limit = 50;
                        if (qidx != -1) {
                            QByteArray qs = path.mid(qidx+1);
                            QList<QByteArray> parts = qs.split('&');
                            for (const QByteArray &p : parts) {
                                QList<QByteArray> kv = p.split('=');
                                if (kv.size() == 2 && kv[0] == "limit") {
                                    bool ok = false; int v = kv[1].toInt(&ok);
                                    if (ok && v > 0 && v <= 1000) limit = v;
                                }
                            }
                        }
                        QJsonArray arr;
                        auto items = this->db->listItems();
                        int count = 0;
                        for (const auto &it : items) {
                            if (count++ >= limit) break;
                            QJsonObject o;
                            o["id"] = QString::fromStdString(it.id);
                            o["title"] = QString::fromStdString(it.title);
                            o["authors"] = QString::fromStdString(it.authors);
                            o["year"] = QString::fromStdString(it.year);
                            o["doi"] = QString::fromStdString(it.doi);
                            o["url"] = QString::fromStdString(it.url);
                            o["collection"] = QString::fromStdString(it.collection);
                            arr.append(o);
                        }
                        QJsonDocument doc(arr); QByteArray out = doc.toJson(QJsonDocument::Compact);
                        QByteArray resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(out.size()) + "\r\n\r\n" + out;
                        socket->write(resp); socket->flush(); socket->disconnectFromHost(); return;
                    }

                    if (method == "POST" && path == "/connector/save") {
                        std::cerr << "=== BrowserConnector: POST /connector/save ===" << std::endl;
                        std::cerr << "  body length: " << body.size() << std::endl;
                        
                        QJsonParseError err; QJsonDocument reqDoc = QJsonDocument::fromJson(body, &err);
                        std::cerr << "  JSON parse error: " << err.errorString().toStdString() << " at offset " << err.offset << std::endl;
                        std::cerr << "  reqDoc.isNull: " << (reqDoc.isNull() ? "yes" : "no") << std::endl;
                        std::cerr << "  reqDoc.isObject: " << (reqDoc.isObject() ? "yes" : "no") << std::endl;
                        
                        bool ok = false; std::string createdId;
                        if (!reqDoc.isNull() && err.error == QJsonParseError::NoError && reqDoc.isObject()) {
                            QJsonObject root = reqDoc.object();
                            QJsonObject data = root.value("data").toObject();
                            std::cerr << "  data keys: ";
                            for (const QString &k : data.keys()) std::cerr << k.toStdString() << " ";
                            std::cerr << std::endl;
                            
                            // First, check if this is an update to an existing item
                            std::string incomingDoi = data.value("doi").toString().toStdString();
                            std::string incomingIsbn = data.value("isbn").toString().toStdString();
                            std::string incomingTitle = data.value("title").toString().toStdString();
                            std::string incomingAuthors = data.value("authors").toString().toStdString();
                            
                            Item existing; bool found = false;
                            if (!incomingDoi.empty()) found = this->db->findItemByDOI(incomingDoi, existing);
                            if (!found && !incomingIsbn.empty()) found = this->db->findItemByISBN(incomingIsbn, existing);
                            if (!found && !incomingTitle.empty() && !incomingAuthors.empty()) found = this->db->findItemByTitleAndAuthor(incomingTitle, incomingAuthors, existing);
                            
                            // Determine which ID to use for storage
                            std::string storageId = found ? existing.id : gen_uuid();
                            
                            Item it;
                            it.id = storageId;
                            it.title = incomingTitle;
                            it.authors = incomingAuthors;
                            it.year = data.value("year").toString().toStdString();
                            QString incomingType = data.value("type").toString();
                            QString incomingBibtex = data.value("bibtexType").toString();
                            it.type = incomingType.toStdString();
                            if ((it.type.empty() || incomingBibtex.size() > 0) && !incomingBibtex.isEmpty()) it.type = incomingBibtex.toStdString();
                            it.doi = incomingDoi;
                            it.isbn = incomingIsbn;
                            it.publisher = data.value("publisher").toString().toStdString();
                            it.pages = data.value("pages").toString().toStdString();
                            it.volume = data.value("volume").toString().toStdString();
                            it.number = data.value("number").toString().toStdString();
                            it.journal = data.value("journal").toString().toStdString();
                            it.url = data.value("url").toString().toStdString();
                            it.abstract = data.value("abstract").toString().toStdString();
                            it.pdf_path = data.value("pdf_path").toString().toStdString();
                            
                            // Debug: Log what we received
                            std::cerr << "BrowserConnector: received request" << std::endl;
                            std::cerr << "  doi: " << incomingDoi << std::endl;
                            std::cerr << "  title: " << incomingTitle << std::endl;
                            std::cerr << "  found existing: " << (found ? "yes" : "no") << std::endl;
                            if (found) std::cerr << "  existing.id: " << existing.id << std::endl;
                            std::cerr << "  storageId: " << storageId << std::endl;
                            std::cerr << "  has attachments: " << (data.contains("attachments") ? "yes" : "no") << std::endl;
                            
                            // Handle attachments embedded as base64 in `data.attachments` (optional)
                            if (data.contains("attachments") && data.value("attachments").isArray()) {
                                QJsonArray a = data.value("attachments").toArray();
                                std::cerr << "  attachments count: " << a.size() << std::endl;
                                if (!a.isEmpty()) {
                                    // Prepare storage directory: ~/.local/share/bello/storage/<item-id> (uses existing ID if updating)
                                    QString home = QString::fromLocal8Bit(std::getenv("HOME"));
                                    QString storageRoot = QDir::cleanPath(home + "/.local/share/bello/storage");
                                    QDir().mkpath(storageRoot);
                                    QString itemDir = storageRoot + "/" + QString::fromStdString(storageId);
                                    std::cerr << "  storage dir: " << itemDir.toStdString() << std::endl;
                                    QDir().mkpath(itemDir);
                                    QStringList savedPaths;
                                    for (int ai = 0; ai < a.size(); ++ai) {
                                        QJsonValue v = a.at(ai);
                                        if (!v.isObject()) continue;
                                        QJsonObject o = v.toObject();
                                        QString fname = o.value("filename").toString();
                                        QString b64 = o.value("data").toString();
                                        std::cerr << "  attachment " << ai << " filename: " << fname.toStdString() << " b64 length: " << b64.length() << std::endl;
                                        if (b64.isEmpty() || fname.isEmpty()) continue;
                                        QByteArray bytes = QByteArray::fromBase64(b64.toUtf8());
                                        std::cerr << "  decoded bytes: " << bytes.size() << std::endl;
                                        // Ensure unique filename
                                        QString outPath = itemDir + "/" + fname;
                                        QFile f(outPath);
                                        int idx = 1;
                                        while (f.exists()) {
                                            QString stem = QFileInfo(fname).completeBaseName();
                                            QString ext = QFileInfo(fname).suffix();
                                            QString candidate = QString("%1_%2%3").arg(stem).arg(idx).arg(ext.isEmpty()?QString():QString('.' + ext));
                                            outPath = itemDir + "/" + candidate;
                                            f.setFileName(outPath);
                                            ++idx;
                                        }
                                        std::cerr << "  writing to: " << outPath.toStdString() << std::endl;
                                        if (f.open(QIODevice::WriteOnly)) {
                                            f.write(bytes);
                                            f.close();
                                            savedPaths << outPath;
                                            std::cerr << "  wrote successfully" << std::endl;
                                        } else {
                                            std::cerr << "  FAILED to open file for writing: " << f.errorString().toStdString() << std::endl;
                                        }
                                    }
                                    if (!savedPaths.isEmpty()) {
                                        // join saved paths with semicolon to match existing pdf_path format
                                        QString joined = savedPaths.join(';');
                                        if (it.pdf_path.empty()) it.pdf_path = joined.toStdString();
                                        else {
                                            std::string existingPdf = it.pdf_path;
                                            existingPdf += ";" + joined.toStdString();
                                            it.pdf_path = existingPdf;
                                        }
                                        std::cerr << "  pdf_path set to: " << it.pdf_path << std::endl;
                                    }
                                }
                            }
                            it.extra = data.value("extra").toString().toStdString();

                            std::string coll = data.value("collection").toString().toStdString();
                            it.collection = coll;

                            // Use the 'found' and 'existing' from earlier lookup
                            if (found) {
                                std::cerr << "Merging with existing item: " << existing.id << std::endl;
                                std::cerr << "  existing.pdf_path before: " << existing.pdf_path << std::endl;
                                std::cerr << "  it.pdf_path: " << it.pdf_path << std::endl;
                                
                                auto mergeIfEmpty = [](std::string &dest, const std::string &src) { if (dest.empty() && !src.empty()) dest = src; };
                                mergeIfEmpty(existing.title, it.title);
                                mergeIfEmpty(existing.authors, it.authors);
                                mergeIfEmpty(existing.year, it.year);
                                mergeIfEmpty(existing.type, it.type);
                                mergeIfEmpty(existing.doi, it.doi);
                                mergeIfEmpty(existing.isbn, it.isbn);
                                mergeIfEmpty(existing.publisher, it.publisher);
                                mergeIfEmpty(existing.pages, it.pages);
                                mergeIfEmpty(existing.volume, it.volume);
                                mergeIfEmpty(existing.number, it.number);
                                mergeIfEmpty(existing.journal, it.journal);
                                mergeIfEmpty(existing.url, it.url);
                                mergeIfEmpty(existing.abstract, it.abstract);
                                // For pdf_path: append new attachments (they're already saved using existing.id)
                                if (!it.pdf_path.empty()) {
                                    if (existing.pdf_path.empty()) {
                                        existing.pdf_path = it.pdf_path;
                                    } else {
                                        existing.pdf_path += ";" + it.pdf_path;
                                    }
                                }
                                std::cerr << "  existing.pdf_path after: " << existing.pdf_path << std::endl;

                                // merge extras
                                QJsonParseError perr; QJsonObject exOld; if (!existing.extra.empty()) { QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(existing.extra), &perr); if (!d.isNull() && d.isObject()) exOld = d.object(); }
                                QJsonObject exNew; if (!it.extra.empty()) { QJsonDocument d2 = QJsonDocument::fromJson(QByteArray::fromStdString(it.extra), &perr); if (!d2.isNull() && d2.isObject()) exNew = d2.object(); }
                                for (const QString &k : exNew.keys()) { if (!exOld.contains(k) || exOld.value(k).toString().trimmed().isEmpty()) exOld.insert(k, exNew.value(k)); }
                                if (!exOld.isEmpty()) { QJsonDocument dd(exOld); existing.extra = dd.toJson(QJsonDocument::Compact).toStdString(); }

                                if (!it.collection.empty()) this->db->addItemToCollection(existing.id, it.collection);
                                this->db->updateItem(existing);
                                std::cerr << "Updated existing item, setting ok=true, createdId=" << existing.id << std::endl;
                                ok = true; createdId = existing.id;
                                if (this->reloadCb) this->reloadCb();
                                if (this->selectCb) this->selectCb(createdId);
                            } else {
                                this->db->addItem(it);
                                ok = true; createdId = it.id;
                                if (this->reloadCb) this->reloadCb();
                                if (this->selectCb) this->selectCb(createdId);
                            }
                        }
                        QJsonObject respObj; respObj["success"] = ok; respObj["id"] = QJsonValue(QString::fromStdString(createdId)); QJsonDocument respDoc(respObj);
                        QByteArray out = respDoc.toJson(QJsonDocument::Compact);
                        QByteArray resp = "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(out.size()) + "\r\n\r\n" + out;
                        socket->write(resp); socket->flush(); socket->disconnectFromHost(); return;
                    }

                    QByteArray out = "{\"error\":\"not found\"}";
                    QByteArray resp = "HTTP/1.1 404 Not Found\r\nContent-Type: application/json\r\nContent-Length: " + QByteArray::number(out.size()) + "\r\n\r\n" + out;
                    socket->write(resp); socket->flush(); socket->disconnectFromHost();
                });
            }
        });
    }

private:
    QTcpServer *server{nullptr};
    Database *db{nullptr};
    std::function<void()> reloadCb;
    std::function<void(const std::string&)> selectCb;

        
};
