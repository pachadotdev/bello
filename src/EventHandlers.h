#pragma once

#include <QJsonDocument>
#include <QJsonObject>

inline bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent*>(event);

        // Handle keys when itemsList has focus
        if (watched == ui->itemsList) {
            if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
                onDeleteItem();
                return true;
            }
            if (ke->key() == Qt::Key_F2) {
                onRenameItem();
                return true;
            }
        }

        // Handle keys when collectionsList has focus
        if (watched == ui->collectionsList) {
            if (ke->key() == Qt::Key_Delete) {
                auto *it = ui->collectionsList->currentItem();
                if (it) {
                    QString coll = it->data(0, Qt::UserRole).toString();
                    if (!coll.isEmpty()) deleteCollection(coll);
                }
                return true;
            }
            if (ke->key() == Qt::Key_F2) {
                auto *it = ui->collectionsList->currentItem();
                if (it) {
                    QString coll = it->data(0, Qt::UserRole).toString();
                    if (coll.isEmpty()) createCollection();
                    else renameCollection(coll);
                }
                return true;
            }
        }
    }

    // Handle drag-drop on collections tree viewport
    if (watched == ui->collectionsList->viewport()) {
        if (event->type() == QEvent::DragEnter) {
            auto *de = static_cast<QDragEnterEvent*>(event);
            if (de->mimeData()->hasFormat("application/x-qabstractitemmodeldatalist")) {
                de->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::DragMove) {
            auto *de = static_cast<QDragMoveEvent*>(event);
            de->acceptProposedAction();
            return true;
        }
        if (event->type() == QEvent::Drop) {
            auto *de = static_cast<QDropEvent*>(event);
            auto *targetItem = ui->collectionsList->itemAt(de->position().toPoint());
            if (!targetItem) return true;

            QString targetCollection = targetItem->data(0, Qt::UserRole).toString();
            auto selectedItems = ui->itemsList->selectedItems();
            if (selectedItems.isEmpty()) return true;

            // Don't do anything if target is root (empty)
            if (targetCollection.isEmpty()) return true;

            // Show context menu with Move / Copy options
            QMenu menu;
            QString label = targetCollection;
            int count = selectedItems.size();

            menu.addAction(QString("Move %1 item(s) to '%2'").arg(count).arg(label), [this, selectedItems, targetCollection, targetItem](){
                // Save item IDs to restore selection after reload
                QStringList movedIds;
                for (auto *listItem : selectedItems) {
                    std::string itemId = listItem->data(Qt::UserRole).toString().toStdString();
                    movedIds << QString::fromStdString(itemId);
                    // Remove from ALL current collections
                    auto currentColls = db->getItemCollections(itemId);
                    for (const auto &c : currentColls) {
                        db->removeItemFromCollection(itemId, c);
                    }
                    // Add to target collection
                    db->addItemToCollection(itemId, targetCollection.toStdString());
                }

                // Switch to target collection and reload
                reload();

                // Find and select the target collection in the tree
                std::function<QTreeWidgetItem*(QTreeWidgetItem*, const QString&)> findItem = [&](QTreeWidgetItem* parent, const QString& path) -> QTreeWidgetItem* {
                    if (!parent) return nullptr;
                    if (parent->data(0, Qt::UserRole).toString() == path) return parent;
                    for (int i = 0; i < parent->childCount(); ++i) {
                        if (auto *found = findItem(parent->child(i), path)) return found;
                    }
                    return nullptr;
                };

                if (auto *root = ui->collectionsList->topLevelItem(0)) {
                    if (auto *targetTreeItem = findItem(root, targetCollection)) {
                        ui->collectionsList->setCurrentItem(targetTreeItem);
                        onCollectionSelected();

                        // Restore selection of moved items
                        for (int i = 0; i < ui->itemsList->count(); ++i) {
                            auto *item = ui->itemsList->item(i);
                            if (movedIds.contains(item->data(Qt::UserRole).toString())) {
                                item->setSelected(true);
                            }
                        }
                        // Update right panel with selection
                        if (!movedIds.isEmpty()) {
                            onItemSelected();
                        }
                    }
                }
            });

            // "Copy to collection" - add as symbolic link (keep in existing collections)
            menu.addAction(QString("Copy %1 item(s) to '%2'").arg(count).arg(label), [this, selectedItems, targetCollection](){
                for (auto *listItem : selectedItems) {
                    std::string itemId = listItem->data(Qt::UserRole).toString().toStdString();
                    db->addItemToCollection(itemId, targetCollection.toStdString());
                }
                reload();
            });

            menu.addAction("Cancel");
            menu.exec(ui->collectionsList->mapToGlobal(de->position().toPoint()));

            de->acceptProposedAction();
            return true;
        }
    }
    // Handle drag-drop on attachments list viewport
    if (ui->attachmentsList && watched == ui->attachmentsList) {
        if (event->type() == QEvent::DragEnter) {
            auto *de = static_cast<QDragEnterEvent*>(event);
            if (de->mimeData()->hasUrls()) {
                de->acceptProposedAction();
                return true;
            }
        }
        if (event->type() == QEvent::DragMove) {
            auto *de = static_cast<QDragMoveEvent*>(event);
            de->acceptProposedAction();
            return true;
        }
        if (event->type() == QEvent::Drop) {
            auto *de = static_cast<QDropEvent*>(event);
            const QMimeData *md = de->mimeData();
            if (!md->hasUrls()) return true;
            QList<QUrl> urls = md->urls();
            if (urls.isEmpty()) return true;

            auto selectedItems = ui->itemsList->selectedItems();
            if (selectedItems.isEmpty()) return true;

            std::string itemId = selectedItems.first()->data(Qt::UserRole).toString().toStdString();
            Item dbItem;
            if (!db->getItem(itemId, dbItem)) return true;

            QStringList existing = QString::fromStdString(dbItem.pdf_path).split(';', Qt::SkipEmptyParts);
            for (const QUrl &u : urls) {
                if (!u.isLocalFile()) continue;
                QString path = u.toLocalFile();
                if (!existing.contains(path)) existing << path;
            }
            dbItem.pdf_path = existing.join(';').toStdString();
            db->updateItem(dbItem);

            onItemSelected();
            de->acceptProposedAction();
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

inline void MainWindow::reload() {
    QStringList expanded = collectExpandedPaths();
    QString selectedPath;
    if (auto *sel = ui->collectionsList->currentItem()) selectedPath = sel->data(0, Qt::UserRole).toString();

    ui->collectionsList->clear();
    ui->itemsList->clear();
    ui->collectionCheckList->clear();

    auto collections = db->listCollections();

    // Populate checkable collections list
    for (const auto &collection : collections) {
        QString path = QString::fromStdString(collection);
        auto *checkItem = new QListWidgetItem(path);
        checkItem->setFlags(checkItem->flags() | Qt::ItemIsUserCheckable);
        checkItem->setCheckState(Qt::Unchecked);
        checkItem->setData(Qt::UserRole, path);
        ui->collectionCheckList->addItem(checkItem);
    }

    auto *allItems = new QTreeWidgetItem(ui->collectionsList);
    allItems->setText(0, "All Items");
    allItems->setData(0, Qt::UserRole, "");

    for (const auto &collection : collections) {
        QString path = QString::fromStdString(collection);
        QTreeWidgetItem *parent = allItems;
        const auto parts = path.split('/', Qt::SkipEmptyParts);
        QString accum;
        for (int i = 0; i < parts.size(); ++i) {
            accum = accum.isEmpty() ? parts[i] : accum + "/" + parts[i];
            parent = ensureChild(parent, parts[i]);
            parent->setData(0, Qt::UserRole, accum);
        }
    }

    restoreExpandedPaths(expanded);
    ui->collectionsList->expandItem(allItems);

    QTreeWidgetItem *selectItem = allItems;
    if (!selectedPath.isEmpty()) {
        QTreeWidgetItem *parent = allItems;
        const auto parts = selectedPath.split('/', Qt::SkipEmptyParts);
        QString accum;
        for (int i = 0; i < parts.size(); ++i) {
            parent = ensureChild(parent, parts[i]);
            accum = accum.isEmpty()? parts[i] : accum + "/" + parts[i];
            parent->setData(0, Qt::UserRole, accum);
        }
        selectItem = parent;
    }
    ui->collectionsList->setCurrentItem(selectItem);
    onCollectionSelected();
}

inline QStringList MainWindow::fieldsForType(const QString &type) {
    QString t = type.toLower();
    if (t == "article") return {"author","title","journal","year","volume","number","pages","month","note","key","doi"};
    if (t == "book") return {"author","editor","title","publisher","year","address","edition","month","note","isbn"};
    if (t == "booklet") return {"title","author","howpublished","month","year","note"};
    if (t == "conference" || t == "inproceedings") return {"author","title","booktitle","year","editor","pages","organization","publisher","address","month","note"};
    if (t == "inbook") return {"author","title","chapter","pages","publisher","year","address","edition","month","note"};
    if (t == "incollection") return {"author","title","booktitle","publisher","year","pages","editor","address","month","note"};
    if (t == "manual") return {"title","author","organization","address","edition","month","year","note"};
    if (t == "mastersthesis" || t == "phdthesis") return {"author","title","school","year","address","month","note"};
    if (t == "misc") return {"title","author","howpublished","month","year","note"};
    if (t == "proceedings") return {"editor","title","year","publisher","address","volume","series","note"};
    if (t == "techreport") return {"author","title","institution","number","year","address","month","note"};
    if (t == "unpublished") return {"author","title","note","year"};
    // Fallback: common fields
    return {"author","title","year","note","pages","publisher","address","doi"};
}

inline void MainWindow::populateDynamicFields(const QString &type, const Item *item) {
    // Remove existing dynamic fields and clear both active and blank layouts completely
    QFormLayout *blankLayout = ui->dynamicFieldsLayout;
    QFormLayout *activeLayout = ui->dynamicActiveLayout;
    // delete editor widgets referenced in map and remove them from layouts
    for (auto it = ui->dynamicFieldEdits.begin(); it != ui->dynamicFieldEdits.end(); ++it) {
        QWidget *w = it.value();
        if (!w) continue;
        // If the widget is in the active/main form, remove its label first
        if (activeLayout) {
            QWidget *lab = activeLayout->labelForField(w);
            if (lab) {
                activeLayout->removeWidget(lab);
                lab->deleteLater();
            }
            activeLayout->removeWidget(w);
        }
        // Also ensure it's removed from the blank dynamic layout if present
        if (blankLayout) {
            QWidget *bl = blankLayout->labelForField(w);
            if (bl) {
                blankLayout->removeWidget(bl);
                bl->deleteLater();
            }
            blankLayout->removeWidget(w);
        }
        w->deleteLater();
    }
    ui->dynamicFieldEdits.clear();
    // helper to clear a layout fully (avoid takeAt on empty layout)
    auto clearLayout = [](QFormLayout *lay){
        while (lay->count() > 0) {
            QLayoutItem *child = lay->takeAt(0);
            if (child->widget()) child->widget()->deleteLater();
            delete child;
        }
    };
    if (blankLayout) clearLayout(blankLayout);
    // Do NOT clear the active/main form layout here: clearing the main form
    // would delete static widgets like Title/Authors. We already removed and
    // deleted dynamic widgets above; leave the main form intact.

    QStringList fields = fieldsForType(type);
    QJsonObject extraObj;
    if (item && !QString::fromStdString(item->extra).trimmed().isEmpty()) {
        QJsonDocument d = QJsonDocument::fromJson(QByteArray::fromStdString(item->extra));
        if (d.isObject()) extraObj = d.object();
    }

    // helper: get extra JSON value case-insensitively for field name
    auto getExtraValue = [&](const QString &field) -> QString {
        if (extraObj.isEmpty()) return QString();
        QString lf = field.toLower();
        for (const QString &k : extraObj.keys()) {
            if (k.toLower() == lf && extraObj.value(k).isString()) return extraObj.value(k).toString();
        }
        return QString();
    };

    // helper: parse note-formatted pairs like "key = {value}; key2 = {value2}" into map (case-insensitive keys)
    QMap<QString, QString> notePairs;
    if (item && !QString::fromStdString(item->note).trimmed().isEmpty()) {
        QString note = QString::fromStdString(item->note);
        auto parts = note.split(';', Qt::SkipEmptyParts);
        QRegularExpression rx("^\\s*([^=\\s]+)\\s*=\\s*\\{(.*)\\}\\s*$");
        for (const QString &p : parts) {
            QString part = p.trimmed();
            QRegularExpressionMatch m = rx.match(part);
            if (m.hasMatch()) {
                QString k = m.captured(1).trimmed().toLower();
                QString v = m.captured(2).trimmed();
                notePairs.insert(k, v);
            }
        }
    }

    auto isMultiline = [](const QString &n) {
        QString ln = n.toLower();
        return (ln == "abstract" || ln == "note" || ln == "keywords" || ln == "annotation");
    };

    // Render each field as a single labeled row (preserves ordering), prefilling from Item, extra JSON, or note-pairs
    int insertedPos = 0;
    for (const QString &f : fields) {
        QString lname = f.toLower();
        if (lname == "title" || lname == "author" || lname == "authors" || lname == "year" || lname == "isbn" || lname == "doi") continue;

        QString value;
        if (item) {
            if (lname == "publisher") value = QString::fromStdString(item->publisher);
            else if (lname == "editor") value = QString::fromStdString(item->editor);
            else if (lname == "booktitle") value = QString::fromStdString(item->booktitle);
            else if (lname == "series") value = QString::fromStdString(item->series);
            else if (lname == "edition") value = QString::fromStdString(item->edition);
            else if (lname == "chapter") value = QString::fromStdString(item->chapter);
            else if (lname == "school") value = QString::fromStdString(item->school);
            else if (lname == "institution") value = QString::fromStdString(item->institution);
            else if (lname == "organization") value = QString::fromStdString(item->organization);
            else if (lname == "howpublished") value = QString::fromStdString(item->howpublished);
            else if (lname == "language") value = QString::fromStdString(item->language);
            else if (lname == "journal") value = QString::fromStdString(item->journal);
            else if (lname == "pages") value = QString::fromStdString(item->pages);
            else if (lname == "volume") value = QString::fromStdString(item->volume);
            else if (lname == "number") value = QString::fromStdString(item->number);
            else if (lname == "keywords") value = QString::fromStdString(item->keywords);
            else if (lname == "month") value = QString::fromStdString(item->month);
            else if (lname == "address") value = QString::fromStdString(item->address);
            else if (lname == "note") value = QString::fromStdString(item->note);
        }
        // fallback to extra JSON (case-insensitive)
        if (value.trimmed().isEmpty()) {
            QString ev = getExtraValue(f);
            if (!ev.isEmpty()) value = ev;
        }
        // fallback to note-parsed pairs
        if (value.trimmed().isEmpty()) {
            QString np = notePairs.value(lname, QString());
            if (!np.isEmpty()) value = np;
        }

        QWidget *editor = nullptr;
        if (isMultiline(f)) {
            QTextEdit *te = new QTextEdit();
            te->setPlaceholderText(f);
            te->setMaximumHeight(120);
            if (!value.isEmpty()) te->setPlainText(value);
            // when editing a blank multiline field, save immediately so populateDynamicFields
            // will move it into the active layout on the next render
            connect(te, &QTextEdit::textChanged, [this]() { onSaveItem(); });
            editor = te;
        } else {
            QLineEdit *le = new QLineEdit();
            le->setPlaceholderText(f);
            if (!value.isEmpty()) le->setText(value);
            // Save on edit; populateDynamicFields will re-render and place non-empty fields
            connect(le, &QLineEdit::editingFinished, [this]() { onSaveItem(); });
            editor = le;
        }

        // Build a human-friendly label (capitalize, replace underscores/dashes)
        QString displayLabel = f;
        displayLabel.replace('_', ' ');
        displayLabel.replace('-', ' ');
        if (!displayLabel.isEmpty()) displayLabel[0] = displayLabel[0].toUpper();

        // Decide which layout to add to: active (non-empty) or blank (empty)
        if (!value.trimmed().isEmpty() && ui->dynamicActiveLayout) {
            // Insert at the stored dynamic insert index so active fields appear in the desired order
            int idx = ui->dynamicInsertIndex + insertedPos;
            ui->dynamicActiveLayout->insertRow(idx, displayLabel, editor);
            insertedPos++;
        } else {
            ui->dynamicFieldsLayout->addRow(displayLabel, editor);
        }
        ui->dynamicFieldEdits.insert(f, editor);
    }
}
