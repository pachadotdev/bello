#pragma once

#include <QApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QMenu>
#include <QInputDialog>
#include <QDebug>
#include <QIcon>
#include <QStyle>
#include <QShortcut>

// Forward declaration to avoid circular dependency
class MainWindow;

inline void MainWindow::onCollectionSelected() {
    // Ensure application-wide shortcuts are installed once
    ensureShortcuts();
    auto *item = ui->collectionsList->currentItem();
    if (!item) return;
    
    QString collection = item->data(0, Qt::UserRole).toString();
    ui->itemsList->clear();
    
    std::vector<Item> items;
    if (collection.isEmpty()) {
        items = db->listItems();
    } else {
        items = db->listItemsInCollection(collection.toStdString());
    }
    
    for (const auto &it : items) {
        auto *listItem = new QListWidgetItem(QString::fromStdString(it.title));
        listItem->setData(Qt::UserRole, QString::fromStdString(it.id));
        // Store raw pdf_path and expose it as a tooltip so users can see attached files.
        listItem->setData(Qt::UserRole + 1, QString::fromStdString(it.pdf_path));
        if (!it.pdf_path.empty()) {
            listItem->setToolTip(QString::fromStdString(it.pdf_path));
        }
        
        ui->itemsList->addItem(listItem);
    }
}

inline void MainWindow::onItemContextMenuRequested(const QPoint &pos) {
    auto *item = ui->itemsList->itemAt(pos);
    if (!item) return;
    
    // Make sure the right-clicked item is selected
    if (!ui->itemsList->selectedItems().contains(item)) {
        ui->itemsList->clearSelection();
        item->setSelected(true);
    }
    
    auto selectedItems = ui->itemsList->selectedItems();
    bool multipleSelected = selectedItems.size() > 1;
    
    QMenu menu;
    
        if (multipleSelected) {
        menu.addAction(QString("Open %1 PDFs").arg(selectedItems.size()), [this](){
            auto selectedItems = ui->itemsList->selectedItems();
            for (auto *item : selectedItems) {
                QString pdfPath = item->data(Qt::UserRole + 1).toString();
                if (pdfPath.isEmpty()) continue;
                // Support multiple attached files separated by ';'
                for (const QString &p : pdfPath.split(';', Qt::SkipEmptyParts)) {
                    QString trimmed = p.trimmed();
                    if (!trimmed.isEmpty() && QFile::exists(trimmed)) {
                        QDesktopServices::openUrl(QUrl::fromLocalFile(trimmed));
                    }
                }
            }
        });
        
        menu.addAction(QString("Copy %1 Citations").arg(selectedItems.size()), [this](){
            auto selectedItems = ui->itemsList->selectedItems();
            QStringList citations;
            for (auto *item : selectedItems) {
                Item it;
                if (db->getItem(item->data(Qt::UserRole).toString().toStdString(), it)) {
                    citations << formatCitation(it);
                }
            }
            QApplication::clipboard()->setText(citations.join("\n\n"));
        });
        
        menu.addAction(QString("Copy %1 BibTeX Entries").arg(selectedItems.size()), [this](){
            copySelectedAsBibTeX();
        });
        
        menu.addAction(QString("Delete %1 Items").arg(selectedItems.size()), [this](){
            auto selectedItems = ui->itemsList->selectedItems();
            if (QMessageBox::question(this, "Delete", QString("Delete %1 items?").arg(selectedItems.size())) == QMessageBox::Yes) {
                for (auto *item : selectedItems) {
                    db->deleteItem(item->data(Qt::UserRole).toString().toStdString());
                }
                reload();
            }
        });
        
        // Add "Move to Collection" submenu for multiple items (removes from all, adds to target)
        QMenu *moveMenu = menu.addMenu(QString("Move %1 to Collection...").arg(selectedItems.size()));
        // Add "Copy to Collection" submenu (adds as symbolic link)
        QMenu *copyMenu = menu.addMenu(QString("Copy %1 to Collection...").arg(selectedItems.size()));
        auto collections = db->listCollections();
        
        for (const auto &coll : collections) {
            QString collName = QString::fromStdString(coll);
            moveMenu->addAction(collName, [this, collName](){
                auto selectedItems = ui->itemsList->selectedItems();
                for (auto *listItem : selectedItems) {
                    std::string itemId = listItem->data(Qt::UserRole).toString().toStdString();
                    // Remove from ALL current collections
                    auto currentColls = db->getItemCollections(itemId);
                    for (const auto &c : currentColls) {
                        db->removeItemFromCollection(itemId, c);
                    }
                    // Add to target
                    db->addItemToCollection(itemId, collName.toStdString());
                }
                reload();
            });
            copyMenu->addAction(collName, [this, collName](){
                auto selectedItems = ui->itemsList->selectedItems();
                for (auto *listItem : selectedItems) {
                    std::string itemId = listItem->data(Qt::UserRole).toString().toStdString();
                    db->addItemToCollection(itemId, collName.toStdString());
                }
                reload();
            });
        }
    } else {
        menu.addAction("Open PDF", this, &MainWindow::onOpenItem);
        menu.addAction("Copy Citation", [this, item](){
            Item it; 
            if (!db->getItem(item->data(Qt::UserRole).toString().toStdString(), it)) return;
            QApplication::clipboard()->setText(formatCitation(it));
        });
        menu.addAction("Copy BibTeX", [this, item](){
            copySelectedAsBibTeX();
        });
        menu.addAction("Delete", [this, item](){
            if (QMessageBox::question(this, "Delete", "Delete this item?") == QMessageBox::Yes) {
                db->deleteItem(item->data(Qt::UserRole).toString().toStdString());
                reload();
            }
        });
        
        // Add "Move to Collection" submenu for single item
        QMenu *moveMenu = menu.addMenu("Move to Collection...");
        // Add "Copy to Collection" submenu
        QMenu *copyMenu = menu.addMenu("Copy to Collection...");
        auto collections = db->listCollections();
        for (const auto &coll : collections) {
            QString collName = QString::fromStdString(coll);
            moveMenu->addAction(collName, [this, item, collName](){
                std::string itemId = item->data(Qt::UserRole).toString().toStdString();
                // Remove from ALL current collections
                auto currentColls = db->getItemCollections(itemId);
                for (const auto &c : currentColls) {
                    db->removeItemFromCollection(itemId, c);
                }
                // Add to target
                db->addItemToCollection(itemId, collName.toStdString());
                reload();
            });
            copyMenu->addAction(collName, [this, item, collName](){
                std::string itemId = item->data(Qt::UserRole).toString().toStdString();
                db->addItemToCollection(itemId, collName.toStdString());
                reload();
            });
        }
    }
    
    menu.exec(ui->itemsList->mapToGlobal(pos));
}

inline void MainWindow::onAdd() {
    Item it;
    it.id = gen_uuid();
    it.title = "New Item";
    it.authors = "";
    it.year = "";
    it.doi = "";
    
    // Use the currently selected collection from tree view
    auto *selItem = ui->collectionsList->currentItem();
    if (selItem) {
        it.collection = selItem->data(0, Qt::UserRole).toString().toStdString();
    }
    
    db->addItem(it);
    reload();
}

inline void MainWindow::onUpload() {
    QString filename = QFileDialog::getOpenFileName(this, "Select PDF", "", "PDF Files (*.pdf)");
    if (filename.isEmpty()) return;
    
    Item it;
    it.id = gen_uuid();
    it.title = QFileInfo(filename).baseName().toStdString();
    it.authors = "";
    it.year = "";
    it.doi = "";
    
    // Use the currently selected collection from tree view
    auto *selItem = ui->collectionsList->currentItem();
    if (selItem) {
        it.collection = selItem->data(0, Qt::UserRole).toString().toStdString();
    }
    
    // Copy file to storage
    std::filesystem::path storage = std::filesystem::path(std::getenv("HOME")) / ".local" / "share" / "bello" / "storage";
    std::filesystem::create_directories(storage);
    std::string newId = gen_uuid();
    std::filesystem::path dest = storage / (newId + ".pdf");
    try {
        std::filesystem::copy_file(filename.toStdString(), dest);
        it.pdf_path = dest.string();
    } catch (...) {
        QMessageBox::warning(this, "Error", "Failed to copy PDF file");
        return;
    }
    
    db->addItem(it);
    reload();
}

inline void MainWindow::onOpenItem() {
    auto selectedItems = ui->itemsList->selectedItems();
    if (selectedItems.isEmpty()) return;
    
    for (auto *item : selectedItems) {
        QString pdf = item->data(Qt::UserRole + 1).toString();
        if (pdf.isEmpty()) continue;
        for (const QString &p : pdf.split(';', Qt::SkipEmptyParts)) {
            QString trimmed = p.trimmed();
            if (!trimmed.isEmpty() && QFile::exists(trimmed)) {
                QDesktopServices::openUrl(QUrl::fromLocalFile(trimmed));
            }
        }
    }
    
    // Show message if some items don't have PDFs
    int itemsWithoutPdf = 0;
    for (auto *item : selectedItems) {
        if (item->data(Qt::UserRole + 1).toString().isEmpty()) {
            itemsWithoutPdf++;
        }
    }
    
    if (itemsWithoutPdf > 0) {
        QString message = itemsWithoutPdf == selectedItems.size() 
            ? "No PDFs attached to selected items." 
            : QString("%1 of %2 selected items have no PDF attached.").arg(itemsWithoutPdf).arg(selectedItems.size());
        QMessageBox::information(this, "PDF Status", message);
    }
}

inline void MainWindow::onRenameItem() {
    auto *item = ui->itemsList->currentItem();
    if (!item) return;
    
    Item it;
    if (!db->getItem(item->data(Qt::UserRole).toString().toStdString(), it)) return;
    
    bool ok;
    QString newTitle = QInputDialog::getText(this, "Rename Item", "New title:", 
                                              QLineEdit::Normal, QString::fromStdString(it.title), &ok);
    if (ok && !newTitle.trimmed().isEmpty()) {
        it.title = newTitle.trimmed().toStdString();
        db->updateItem(it);
        reload();
    }
}

inline void MainWindow::onDeleteItem() {
    auto selectedItems = ui->itemsList->selectedItems();
    if (selectedItems.isEmpty()) return;
    
    QString message = selectedItems.size() == 1 
        ? "Delete this item?" 
        : QString("Delete %1 items?").arg(selectedItems.size());
    
    if (QMessageBox::question(this, "Delete", message) == QMessageBox::Yes) {
        for (auto *item : selectedItems) {
            db->deleteItem(item->data(Qt::UserRole).toString().toStdString());
        }
        reload();
    }
}

inline void MainWindow::copySelectedAsBibTeX() {
    auto selectedItems = ui->itemsList->selectedItems();
    if (selectedItems.isEmpty()) return;
    QStringList bibTexEntries;
    for (auto *item : selectedItems) {
        Item it;
        if (db->getItem(item->data(Qt::UserRole).toString().toStdString(), it)) {
            bibTexEntries << itemToBibTeX(it);
        }
    }
    QApplication::clipboard()->setText(bibTexEntries.join("\n\n"));
}

inline void MainWindow::ensureShortcuts() {
    static bool installed = false;
    if (installed) return;
    installed = true;
    auto *sc1 = new QShortcut(QKeySequence("Ctrl+Shift+C"), this);
    connect(sc1, &QShortcut::activated, this, &MainWindow::copySelectedAsBibTeX);
    auto *sc2 = new QShortcut(QKeySequence("Meta+Shift+C"), this);
    connect(sc2, &QShortcut::activated, this, &MainWindow::copySelectedAsBibTeX);
    // Import items: Ctrl+Shift+I / Cmd+Shift+I
    auto *sc3 = new QShortcut(QKeySequence("Ctrl+Shift+I"), this);
    connect(sc3, &QShortcut::activated, this, [this](){
        QString target;
        if (auto *sel = ui->collectionsList->currentItem()) target = sel->data(0, Qt::UserRole).toString();
        importItemsDialog(target);
    });
    auto *sc4 = new QShortcut(QKeySequence("Meta+Shift+I"), this);
    connect(sc4, &QShortcut::activated, this, [this](){
        QString target;
        if (auto *sel = ui->collectionsList->currentItem()) target = sel->data(0, Qt::UserRole).toString();
        importItemsDialog(target);
    });
    // Add collection: Ctrl+Shift+A / Cmd+Shift+A
    auto *sc5 = new QShortcut(QKeySequence("Ctrl+Shift+A"), this);
    connect(sc5, &QShortcut::activated, this, [this](){ createCollection(); });
    auto *sc6 = new QShortcut(QKeySequence("Meta+Shift+A"), this);
    connect(sc6, &QShortcut::activated, this, [this](){ createCollection(); });
    // Add subcollection: Ctrl+Shift+S / Cmd+Shift+S
    auto *sc7 = new QShortcut(QKeySequence("Ctrl+Shift+S"), this);
    connect(sc7, &QShortcut::activated, this, [this](){
        QString target;
        if (auto *sel = ui->collectionsList->currentItem()) target = sel->data(0, Qt::UserRole).toString();
        if (target.isEmpty()) createCollection(); else createSubcollection(target);
    });
    auto *sc8 = new QShortcut(QKeySequence("Meta+Shift+S"), this);
    connect(sc8, &QShortcut::activated, this, [this](){
        QString target;
        if (auto *sel = ui->collectionsList->currentItem()) target = sel->data(0, Qt::UserRole).toString();
        if (target.isEmpty()) createCollection(); else createSubcollection(target);
    });
}
