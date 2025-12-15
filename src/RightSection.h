#pragma once

#include <QLineEdit>
#include <QTextEdit>
#include <QFormLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QFileIconProvider>
#include <QListWidgetItem>
#include <QMenu>
#include <QMessageBox>
#include <QCoreApplication>

// Forward declaration to avoid circular dependency
class MainWindow;

#include <QFormLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>

inline void MainWindow::onItemSelected() {
    auto selectedItems = ui->itemsList->selectedItems();
    
    // Block signals during programmatic updates to avoid triggering auto-save
    ui->collectionCheckList->blockSignals(true);
    ui->title->blockSignals(true);
    ui->authors->blockSignals(true);
    ui->year->blockSignals(true);
    ui->isbn->blockSignals(true);
    ui->entryType->blockSignals(true);
    ui->doi->blockSignals(true);
    
    // Uncheck all collections first
    for (int i = 0; i < ui->collectionCheckList->count(); ++i) {
        ui->collectionCheckList->item(i)->setCheckState(Qt::Unchecked);
    }
    
    if (selectedItems.isEmpty()) {
        // Clear form when nothing is selected
        ui->title->clear();
        ui->authors->clear();
        ui->year->clear();
        ui->doi->clear();
        ui->entryType->setCurrentText("");
        ui->attachmentsList->clear();
        // show placeholder when empty
        QListWidgetItem *ph = new QListWidgetItem("Drag files here or click to add");
        ph->setData(Qt::UserRole, "__placeholder");
        ph->setFlags(ph->flags() & ~Qt::ItemIsSelectable);
        QFont f = ph->font(); f.setItalic(true); ph->setFont(f);
        ph->setForeground(Qt::gray);
        ui->attachmentsList->addItem(ph);
        
        // Unblock signals before returning
        ui->collectionCheckList->blockSignals(false);
        ui->title->blockSignals(false);
        ui->authors->blockSignals(false);
        ui->year->blockSignals(false);
        ui->entryType->blockSignals(false);
        ui->doi->blockSignals(false);
        return;
    }
    
    if (selectedItems.size() == 1) {
        // Single item selected - show its details
        auto *it = selectedItems.first();
        std::string itemId = it->data(Qt::UserRole).toString().toStdString();
        Item item;
        if (!db->getItem(itemId, item)) {
            ui->collectionCheckList->blockSignals(false);
            ui->title->blockSignals(false);
            ui->authors->blockSignals(false);
            ui->year->blockSignals(false);
            ui->doi->blockSignals(false);
            return;
        }

        ui->title->setText(QString::fromStdString(item.title));
        ui->authors->setText(QString::fromStdString(item.authors));
        ui->year->setText(QString::fromStdString(item.year));
        ui->isbn->setText(QString::fromStdString(item.isbn));
        ui->doi->setText(QString::fromStdString(item.doi));
        ui->entryType->setCurrentText(QString::fromStdString(item.type));
        // Populate dynamic fields area for this entry type (UI-only display)
        populateDynamicFields(ui->entryType->currentText(), &item);
        
        // Check ALL collections this item belongs to (using new multi-collection support)
        auto itemCollections = db->getItemCollections(itemId);
        QSet<QString> collSet;
        for (const auto &c : itemCollections) {
            collSet.insert(QString::fromStdString(c));
        }
        
        for (int i = 0; i < ui->collectionCheckList->count(); ++i) {
            auto *checkItem = ui->collectionCheckList->item(i);
            if (collSet.contains(checkItem->data(Qt::UserRole).toString())) {
                checkItem->setCheckState(Qt::Checked);
            }
        }

        // Populate attachments list from item's pdf_path (semicolon-separated)
        ui->attachmentsList->clear();
        if (!item.pdf_path.empty()) {
            QStringList parts = QString::fromStdString(item.pdf_path).split(';', Qt::SkipEmptyParts);
            QFileIconProvider provider;
            for (const QString &p : parts) {
                QString trimmed = p.trimmed();
                if (trimmed.isEmpty()) continue;
                QFileInfo fi(trimmed);
                QListWidgetItem *ait = new QListWidgetItem(fi.fileName());
                ait->setData(Qt::UserRole, trimmed);
                ait->setToolTip(trimmed);
                ait->setIcon(provider.icon(fi));
                ui->attachmentsList->addItem(ait);
            }
        }
        if (ui->attachmentsList->count() == 0) {
            QListWidgetItem *ph = new QListWidgetItem("Drag files here or click to add");
            ph->setData(Qt::UserRole, "__placeholder");
            ph->setFlags(ph->flags() & ~Qt::ItemIsSelectable);
            QFont f = ph->font(); f.setItalic(true); ph->setFont(f);
            ph->setForeground(Qt::gray);
            ui->attachmentsList->addItem(ph);
        }
    } else {
        // Multiple items selected - show summary
        ui->title->setText(QString("(%1 items selected)").arg(selectedItems.size()));
        ui->authors->clear();
        ui->year->clear();
        ui->doi->clear();
        ui->entryType->setCurrentText("");
        populateDynamicFields("", nullptr);
        
        // For multiple selection, check collections that ALL selected items belong to
        // and partially check those that only some items belong to
        QMap<QString, int> collectionCounts;
        for (auto *listItem : selectedItems) {
            std::string itemId = listItem->data(Qt::UserRole).toString().toStdString();
            auto itemCollections = db->getItemCollections(itemId);
            for (const auto &c : itemCollections) {
                QString coll = QString::fromStdString(c);
                collectionCounts[coll] = collectionCounts.value(coll, 0) + 1;
            }
        }
        
        for (int i = 0; i < ui->collectionCheckList->count(); ++i) {
            auto *checkItem = ui->collectionCheckList->item(i);
            QString collName = checkItem->data(Qt::UserRole).toString();
            int count = collectionCounts.value(collName, 0);
            if (count == selectedItems.size()) {
                checkItem->setCheckState(Qt::Checked);
            } else if (count > 0) {
                checkItem->setCheckState(Qt::PartiallyChecked);
            }
        }
    }
    
    // Unblock signals
    ui->collectionCheckList->blockSignals(false);
    ui->title->blockSignals(false);
    ui->authors->blockSignals(false);
    ui->year->blockSignals(false);
    ui->isbn->blockSignals(false);
    ui->entryType->blockSignals(false);
    ui->doi->blockSignals(false);
}

inline void MainWindow::onCollectionCheckChanged(QListWidgetItem *changedItem) {
    // Multi-collection support: checking adds to collection, unchecking removes
    QString collection = changedItem->data(Qt::UserRole).toString();
    auto selectedItems = ui->itemsList->selectedItems();
    if (selectedItems.isEmpty()) return;
    
    // Save current selection to restore after any refresh
    QStringList selectedIds;
    for (auto *listItem : selectedItems) {
        selectedIds << listItem->data(Qt::UserRole).toString();
    }
    
    if (changedItem->checkState() == Qt::Checked) {
        // Add items to this collection
        for (auto *listItem : selectedItems) {
            std::string itemId = listItem->data(Qt::UserRole).toString().toStdString();
            db->addItemToCollection(itemId, collection.toStdString());
        }
    } else if (changedItem->checkState() == Qt::Unchecked) {
        // Prevent unchecking if this is the last collection for any selected item
        bool wouldOrphan = false;
        for (auto *listItem : selectedItems) {
            std::string itemId = listItem->data(Qt::UserRole).toString().toStdString();
            auto colls = db->getItemCollections(itemId);
            if (colls.size() <= 1) {
                wouldOrphan = true;
                break;
            }
        }
        
        if (wouldOrphan) {
            // Re-check the item - can't remove the last collection
            ui->collectionCheckList->blockSignals(true);
            changedItem->setCheckState(Qt::Checked);
            ui->collectionCheckList->blockSignals(false);
            return;
        }
        
        // Remove items from this collection
        for (auto *listItem : selectedItems) {
            std::string itemId = listItem->data(Qt::UserRole).toString().toStdString();
            db->removeItemFromCollection(itemId, collection.toStdString());
        }
    }
    
    // Only refresh list if viewing the affected collection (item might disappear from view)
    auto *currentColl = ui->collectionsList->currentItem();
    QString viewingCollection = currentColl ? currentColl->data(0, Qt::UserRole).toString() : "";
    
    // If we're viewing the collection we just unchecked, the item will disappear from list
    // But we want to keep showing its details in the right panel
    bool itemWillDisappear = (changedItem->checkState() == Qt::Unchecked) &&
                             ((viewingCollection == collection) || 
                              (!viewingCollection.isEmpty() && collection.startsWith(viewingCollection + "/")));
    
    if (itemWillDisappear) {
        // Refresh the items list but DON'T clear the right panel
        // The item is no longer in this collection view, but we keep showing its details
        onCollectionSelected();
        
        // The item won't be in the list anymore, but that's OK - we keep the right panel as-is
        // User can see the item's remaining collections and modify them
    }
    // If just adding to a collection, or removing from a different collection than we're viewing,
    // no need to refresh - the checkbox state is already correct
}

inline void MainWindow::onSaveItem() {
    auto selectedItems = ui->itemsList->selectedItems();
    if (selectedItems.isEmpty()) return;
    
    // Get the checked collection
    QString targetCollection;
    for (int i = 0; i < ui->collectionCheckList->count(); ++i) {
        auto *checkItem = ui->collectionCheckList->item(i);
        if (checkItem->checkState() == Qt::Checked) {
            targetCollection = checkItem->data(Qt::UserRole).toString();
            break;
        }
    }
    
    if (selectedItems.size() == 1) {
        // Single item - update all fields
        auto *it = selectedItems.first();
        Item item;
        if (!db->getItem(it->data(Qt::UserRole).toString().toStdString(), item)) return;

        item.title = ui->title->text().toStdString();
        item.authors = ui->authors->text().toStdString();
        item.year = ui->year->text().toStdString();
        item.isbn = ui->isbn->text().toStdString();
        item.doi = ui->doi->text().toStdString();
        item.type = ui->entryType->currentText().toStdString();
        // Serialize dynamic fields (QLineEdit or QTextEdit) into JSON and persist to item.extra
        QJsonObject extraObj;
        for (auto iter = ui->dynamicFieldEdits.begin(); iter != ui->dynamicFieldEdits.end(); ++iter) {
            QString key = iter.key();
            QWidget *w = iter.value();
            if (!w) continue;
            QString v;
            if (auto le = qobject_cast<QLineEdit*>(w)) {
                v = le->text().trimmed();
            } else if (auto te = qobject_cast<QTextEdit*>(w)) {
                v = te->toPlainText().trimmed();
            }
            if (v.isEmpty()) continue;

            // Map common structured fields back to Item members, otherwise put into extra JSON
            if (key == "publisher") item.publisher = v.toStdString();
            else if (key == "editor") item.editor = v.toStdString();
            else if (key == "booktitle") item.booktitle = v.toStdString();
            else if (key == "series") item.series = v.toStdString();
            else if (key == "edition") item.edition = v.toStdString();
            else if (key == "chapter") item.chapter = v.toStdString();
            else if (key == "school") item.school = v.toStdString();
            else if (key == "institution") item.institution = v.toStdString();
            else if (key == "organization") item.organization = v.toStdString();
            else if (key == "howpublished") item.howpublished = v.toStdString();
            else if (key == "language") item.language = v.toStdString();
            else if (key == "journal") item.journal = v.toStdString();
            else if (key == "pages") item.pages = v.toStdString();
            else if (key == "volume") item.volume = v.toStdString();
            else if (key == "number") item.number = v.toStdString();
            else if (key == "keywords") item.keywords = v.toStdString();
            else if (key == "month") item.month = v.toStdString();
            else if (key == "address") item.address = v.toStdString();
            else if (key == "note") item.note = v.toStdString();
            else extraObj.insert(key, QJsonValue(v));
        }
        QJsonDocument doc(extraObj);
        item.extra = doc.toJson(QJsonDocument::Compact).toStdString();
        item.collection = targetCollection.toStdString();

        db->updateItem(item);
        // Refresh right panel in-place without full reload to preserve selection and focus
        onItemSelected();
        // Done for single-item case
    } else {
        // Multiple items - only update collection membership
        for (auto *listItem : selectedItems) {
            Item item;
            if (db->getItem(listItem->data(Qt::UserRole).toString().toStdString(), item)) {
                item.collection = targetCollection.toStdString();
                db->updateItem(item);
            }
        }
        // For multi-select updates, refresh the list since items may move
        reload();
    }
}

inline void MainWindow::onOpenAttachment(QListWidgetItem *item) {
    if (!item) return;
    QString path = item->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;
    if (!QFile::exists(path)) {
        QMessageBox::warning(this, "Open Attachment", QString("File does not exist: %1").arg(path));
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

inline void MainWindow::onAttachmentContextMenuRequested(const QPoint &pos) {
    auto *it = ui->attachmentsList->itemAt(pos);
    if (!it) return;
    QMenu m;
    m.addAction("Open", [this, it](){ onOpenAttachment(it); });
    m.addAction("Remove Reference", [this, it](){
        // select the item and call removal
        ui->attachmentsList->setCurrentItem(it);
        onRemoveAttachment();
    });
    m.exec(ui->attachmentsList->mapToGlobal(pos));
}

inline void MainWindow::onRemoveAttachment() {
    auto *ait = ui->attachmentsList->currentItem();
    if (!ait) return;
    QString path = ait->data(Qt::UserRole).toString();
    if (path.isEmpty()) return;

    auto selectedItems = ui->itemsList->selectedItems();
    if (selectedItems.isEmpty()) return;

    // Confirm removal of reference
    if (QMessageBox::question(this, "Remove Attachment", QString("Remove attachment reference '%1' from this item?").arg(path)) != QMessageBox::Yes) return;

    // Ask if they want to delete the file from disk
    bool deleteFile = false;
    if (QFile::exists(path)) {
        auto resp = QMessageBox::question(this, "Delete File", "Also delete the file from disk?", QMessageBox::Yes | QMessageBox::No);
        deleteFile = (resp == QMessageBox::Yes);
    }

    // Update DB for the first selected item (for multi-select we'd update each, but this is per-item action)
    auto sel = selectedItems.first();
    std::string itemId = sel->data(Qt::UserRole).toString().toStdString();
    Item item;
    if (!db->getItem(itemId, item)) return;

    QStringList parts = QString::fromStdString(item.pdf_path).split(';', Qt::SkipEmptyParts);
    QStringList keep;
    for (const QString &p : parts) {
        if (p.trimmed() != path) keep << p.trimmed();
    }
    item.pdf_path = keep.join(';').toStdString();
    db->updateItem(item);

    if (deleteFile) {
        try { std::filesystem::remove(path.toStdString()); } catch(...) {}
    }

    // Refresh right pane without losing selection
    onItemSelected();
}
