#pragma once

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QByteArray>
#include <QList>
#include <functional>
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
                connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
                    QByteArray req = socket->readAll();
                    const QByteArray sep = "\r\n\r\n";
                    int idx = req.indexOf(sep);
                    if (idx == -1) return; // wait for more data
                    QByteArray header = req.left(idx);
                    QByteArray body = req.mid(idx + sep.size());
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
                        QJsonParseError err; QJsonDocument reqDoc = QJsonDocument::fromJson(body, &err);
                        bool ok = false; std::string createdId;
                        if (!reqDoc.isNull() && err.error == QJsonParseError::NoError && reqDoc.isObject()) {
                            QJsonObject root = reqDoc.object();
                            QJsonObject data = root.value("data").toObject();
                            Item it;
                            it.id = gen_uuid();
                            it.title = data.value("title").toString().toStdString();
                            it.authors = data.value("authors").toString().toStdString();
                            it.year = data.value("year").toString().toStdString();
                            QString incomingType = data.value("type").toString();
                            QString incomingBibtex = data.value("bibtexType").toString();
                            it.type = incomingType.toStdString();
                            if ((it.type.empty() || incomingBibtex.size() > 0) && !incomingBibtex.isEmpty()) it.type = incomingBibtex.toStdString();
                            it.doi = data.value("doi").toString().toStdString();
                            it.isbn = data.value("isbn").toString().toStdString();
                            it.publisher = data.value("publisher").toString().toStdString();
                            it.pages = data.value("pages").toString().toStdString();
                            it.volume = data.value("volume").toString().toStdString();
                            it.number = data.value("number").toString().toStdString();
                            it.journal = data.value("journal").toString().toStdString();
                            it.url = data.value("url").toString().toStdString();
                            it.abstract = data.value("abstract").toString().toStdString();
                            it.pdf_path = data.value("pdf_path").toString().toStdString();
                            it.extra = data.value("extra").toString().toStdString();

                            std::string coll = data.value("collection").toString().toStdString();
                            it.collection = coll;

                            // Try to find an existing item to merge with
                            Item existing; bool found = false;
                            if (!it.doi.empty()) found = this->db->findItemByDOI(it.doi, existing);
                            if (!found && !it.isbn.empty()) found = this->db->findItemByISBN(it.isbn, existing);
                            if (!found && !it.title.empty() && !it.authors.empty()) found = this->db->findItemByTitleAndAuthor(it.title, it.authors, existing);

                            if (found) {
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
                                mergeIfEmpty(existing.pdf_path, it.pdf_path);

                                // merge extras
                                QJsonParseError perr; QJsonObject exOld; if (!existing.extra.empty()) { QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(existing.extra), &perr); if (!d.isNull() && d.isObject()) exOld = d.object(); }
                                QJsonObject exNew; if (!it.extra.empty()) { QJsonDocument d2 = QJsonDocument::fromJson(QByteArray::fromStdString(it.extra), &perr); if (!d2.isNull() && d2.isObject()) exNew = d2.object(); }
                                for (const QString &k : exNew.keys()) { if (!exOld.contains(k) || exOld.value(k).toString().trimmed().isEmpty()) exOld.insert(k, exNew.value(k)); }
                                if (!exOld.isEmpty()) { QJsonDocument dd(exOld); existing.extra = dd.toJson(QJsonDocument::Compact).toStdString(); }

                                if (!it.collection.empty()) this->db->addItemToCollection(existing.id, it.collection);
                                this->db->updateItem(existing);
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
