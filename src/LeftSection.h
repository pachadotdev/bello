#pragma once

#include <QMenu>
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QTextStream>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDialogButtonBox>
#include <QCheckBox>
#include <QLineEdit>
#include <QLabel>
#include <QDir>

#include "Importers.h"

// Forward declaration to avoid circular dependency
class MainWindow;

#include <functional>

inline void MainWindow::onCollectionContextMenuRequested(const QPoint &pos) {
    auto *item = ui->collectionsList->itemAt(pos);
    if (!item) return;
    
    QString collection = item->data(0, Qt::UserRole).toString();
    QMenu menu;
    // make the right-clicked item the current selection so actions operate on it
    ui->collectionsList->setCurrentItem(item);

    // Add/rename/delete actions
    if (collection.isEmpty()) {
        menu.addAction("Add Collection…", [this](){ createCollection(); });
    } else {
        menu.addAction("Add Subcollection…", [this, collection](){ createSubcollection(collection); });
        menu.addAction("Rename…", [this, collection](){ renameCollection(collection); });
        menu.addAction("Delete…", [this, collection](){ deleteCollection(collection); });
    }

    menu.addSeparator();
    menu.addAction("Import Items…", [this, collection](){ importItemsDialog(collection); });

    menu.exec(ui->collectionsList->viewport()->mapToGlobal(pos));
}
inline void MainWindow::deleteCollection(const QString &name) {
    if (name.isEmpty()) {
        QMessageBox::warning(this, "Delete Collection", "Cannot delete the root collection.");
        return;
    }
    if (QMessageBox::question(this, "Delete Collection", "Delete collection '" + name + "'?") == QMessageBox::Yes) {
        // Collect expanded paths BEFORE deleting, but remove paths related to the deleted collection
        QStringList expanded = collectExpandedPaths();
        QString selectedPath;
        if (auto *sel = ui->collectionsList->currentItem()) selectedPath = sel->data(0, Qt::UserRole).toString();
        
        // Remove expanded paths that are the deleted collection or its subcollections
        QStringList filteredExpanded;
        for (const QString& path : expanded) {
            if (path != name && !path.startsWith(name + "/")) {
                filteredExpanded.append(path);
            }
        }
        
        // If the selected item is being deleted or is a subcollection, select root
        if (selectedPath == name || selectedPath.startsWith(name + "/")) {
            selectedPath = "";
        }
        
        // Perform the delete operation
        db->deleteCollection(name.toStdString());
        
        // Manual reload with updated paths to avoid issues
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

        restoreExpandedPaths(filteredExpanded);
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
}

inline void MainWindow::renameCollection(const QString &oldName) {
    if (oldName.isEmpty()) {
        QMessageBox::warning(this, "Rename Collection", "Cannot rename the root collection.");
        return;
    }
    
    // Extract just the collection name (not the full path) for editing
    QString displayName = oldName;
    if (oldName.contains('/')) {
        displayName = oldName.split('/').last();
    }
    
    bool ok;
    QString newDisplayName = QInputDialog::getText(this, "Rename Collection", "New name:", QLineEdit::Normal, displayName, &ok);
    if (ok && !newDisplayName.isEmpty() && newDisplayName != displayName) {
        // Build the new full path
        QString newName;
        if (oldName.contains('/')) {
            QStringList parts = oldName.split('/');
            parts.last() = newDisplayName;
            newName = parts.join('/');
        } else {
            newName = newDisplayName;
        }
        
        // Collect expanded paths BEFORE renaming, but update them for the new name
        QStringList expanded = collectExpandedPaths();
        QString selectedPath;
        if (auto *sel = ui->collectionsList->currentItem()) selectedPath = sel->data(0, Qt::UserRole).toString();
        
        // Update expanded paths to reflect the rename
        for (int i = 0; i < expanded.size(); ++i) {
            if (expanded[i] == oldName) {
                expanded[i] = newName;
            } else if (expanded[i].startsWith(oldName + "/")) {
                expanded[i] = newName + expanded[i].mid(oldName.length());
            }
        }
        
        // Update selected path to reflect the rename
        if (selectedPath == oldName) {
            selectedPath = newName;
        } else if (selectedPath.startsWith(oldName + "/")) {
            selectedPath = newName + selectedPath.mid(oldName.length());
        }
        
        // Perform the rename operation
        db->renameCollection(oldName.toStdString(), newName.toStdString());
        
        // Manual reload with updated paths to avoid creating duplicate entries
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
}

inline void MainWindow::exportCollection(const QString &name) {
    QString filename = QFileDialog::getSaveFileName(this, "Export Collection", name + ".txt", "Text Files (*.txt)");
    if (filename.isEmpty()) return;
    
    QFile file(filename);
    if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&file);
        auto items = db->listItemsInCollection(name.toStdString());
        for (const auto &it : items) {
            out << formatCitation(it) << "\n\n";
        }
    }
}

inline void MainWindow::createCollection() {
    bool ok;
    QString name = QInputDialog::getText(this, "Create Collection", "Collection name:", QLineEdit::Normal, "", &ok);
    if (ok && !name.isEmpty()) {
        db->addCollection(name.toStdString());
        reload();
    }
}

inline void MainWindow::createSubcollection(const QString &parent) {
    bool ok;
    QString name = QInputDialog::getText(this, "Create Subcollection", "Subcollection name:", QLineEdit::Normal, "", &ok);
    if (ok && !name.isEmpty()) {
        QString fullName = parent + "/" + name;
        db->addCollection(fullName.toStdString());
        // Keep expanded state and selection on reload
        reload();
        // After reload, select and expand the newly created subcollection
        const auto parts = fullName.split('/', Qt::SkipEmptyParts);
        auto *root = ui->collectionsList->topLevelItem(0); // All Items
        QTreeWidgetItem *cur = root;
        QString accum;
        for (const auto &p : parts) {
            cur = ensureChild(cur, p);
            accum = accum.isEmpty()? p : accum + "/" + p;
            cur->setData(0, Qt::UserRole, accum);
            ui->collectionsList->expandItem(cur);
        }
        ui->collectionsList->setCurrentItem(cur);
    }
}

inline QString MainWindow::itemPath(QTreeWidgetItem* item) const {
    if (!item) return "";
    QStringList parts;
    while (item && item->parent()) { // skip root "All Items"
        parts.prepend(item->text(0));
        item = item->parent();
    }
    return parts.join('/');
}

inline QStringList MainWindow::collectExpandedPaths() const {
    QStringList paths;
    auto *root = ui->collectionsList->topLevelItem(0);
    if (!root) return paths;
    std::function<void(QTreeWidgetItem*)> dfs = [&](QTreeWidgetItem *n){
        if (!n) return;
        if (n->isExpanded()) {
            QString p = itemPath(n);
            if (!p.isEmpty()) paths << p;
        }
        for (int i=0;i<n->childCount();++i) dfs(n->child(i));
    };
    dfs(root);
    return paths;
}

inline void MainWindow::restoreExpandedPaths(const QStringList &paths) {
    auto *root = ui->collectionsList->topLevelItem(0);
    if (!root) return;
    for (const auto &p : paths) {
        QTreeWidgetItem *n = root;
        const auto parts = p.split('/', Qt::SkipEmptyParts);
        for (const auto &seg : parts) {
            n = ensureChild(n, seg);
        }
        ui->collectionsList->expandItem(n);
    }
}

inline QTreeWidgetItem* MainWindow::ensureChild(QTreeWidgetItem* parent, const QString &name) {
    for (int i = 0; i < parent->childCount(); ++i) {
        auto *ch = parent->child(i);
        if (ch->text(0) == name) return ch;
    }
    auto *created = new QTreeWidgetItem(parent);
    created->setText(0, name);
    return created;
}

inline void MainWindow::importToCollection(const QString &name) {
    QString dir = QFileDialog::getExistingDirectory(this, "Select folder with PDFs to import");
    if (dir.isEmpty()) return;
    
    QDir directory(dir);
    QStringList files = directory.entryList(QStringList() << "*.pdf", QDir::Files);
    
    for (const QString &filename : files) {
        Item it;
        it.id = gen_uuid();
        it.title = QFileInfo(filename).baseName().toStdString();
        it.collection = name.toStdString();
        
        // Copy to storage
        std::filesystem::path storage = std::filesystem::path(std::getenv("HOME")) / ".local" / "share" / "bello" / "storage";
        std::filesystem::create_directories(storage);
        std::string newId = gen_uuid();
        std::filesystem::path dest = storage / (newId + ".pdf");
        try {
            std::filesystem::copy_file(directory.filePath(filename).toStdString(), dest);
            it.pdf_path = dest.string();
        } catch (...) {
            continue; // Skip this file on error
        }
        
        db->addItem(it);
    }
    reload();
}

inline void MainWindow::importItemsDialog(const QString &targetCollection) {
    // Show import options allowing user to create a new collection or subcollection
    QDialog dlg(this);
    dlg.setWindowTitle("Import Items");
    QVBoxLayout *v = new QVBoxLayout(&dlg);
    QString curLabel = targetCollection.isEmpty() ? "All Items (root)" : targetCollection;
    v->addWidget(new QLabel(QString("Import target: %1").arg(curLabel)));

    // File chooser embedded in dialog
    QHBoxLayout *hfile = new QHBoxLayout();
    QLineEdit *fileEdit = new QLineEdit();
    fileEdit->setReadOnly(true);
    QPushButton *browse = new QPushButton("Choose file...");
    hfile->addWidget(fileEdit);
    hfile->addWidget(browse);
    v->addLayout(hfile);

    QCheckBox *cbNew = new QCheckBox(targetCollection.isEmpty() ? "Create new collection" : "Create new subcollection");
    v->addWidget(cbNew);
    QLineEdit *newName = new QLineEdit();
    newName->setPlaceholderText("Name for new collection/subcollection");
    newName->setEnabled(false);
    v->addWidget(newName);
    connect(cbNew, &QCheckBox::toggled, newName, &QLineEdit::setEnabled);

    // Instructions
    v->addWidget(new QLabel("Supported: .bib, .rdf, .xml"));

    // Buttons: Import / Cancel
    QDialogButtonBox *bbs = new QDialogButtonBox(QDialogButtonBox::Cancel);
    QPushButton *importBtn = new QPushButton("Import");
    importBtn->setEnabled(false);
    bbs->addButton(importBtn, QDialogButtonBox::AcceptRole);
    v->addWidget(bbs);

    // Browse action
    connect(browse, &QPushButton::clicked, this, [fileEdit, importBtn, this](){
        QString filename = QFileDialog::getOpenFileName(this, "Select bibliography file", "", "Bibliography Files (*.bib *.rdf *.xml);;All Files (*.*)");
        if (filename.isEmpty()) return;
        fileEdit->setText(filename);
        importBtn->setEnabled(true);
    });

    // Cancel closes
    connect(bbs, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);

    // Import logic
    connect(importBtn, &QPushButton::clicked, this, [this, &dlg, fileEdit, cbNew, newName, targetCollection](){
        QString filename = fileEdit->text().trimmed();
        if (filename.isEmpty()) { QMessageBox::information(this, "No file", "Please choose a file to import."); return; }

        QString collection = targetCollection;
        if (cbNew->isChecked()) {
            QString name = newName->text().trimmed();
            if (name.isEmpty()) { QMessageBox::information(this, "Missing name", "Please enter a name for the new collection/subcollection."); return; }
            if (targetCollection.isEmpty()) {
                db->addCollection(name.toStdString());
                collection = name;
            } else {
                QString full = targetCollection + "/" + name;
                db->addCollection(full.toStdString());
                collection = full;
            }
        }

        QFileInfo fi(filename);
        QString ext = fi.suffix().toLower();
        int imported = 0;
        if (ext == "bib") {
            imported = importBibTeX(filename, collection);
        } else if (ext == "rdf") {
            imported = importZoteroRDF(filename, collection);
        } else if (ext == "xml") {
            imported = importEndNoteXML(filename, collection);
            if (imported == 0) imported = importMendeleyXML(filename, collection);
        } else {
            QMessageBox::information(this, "Unsupported", "Unsupported file type: " + ext);
            return;
        }

        QMessageBox::information(this, "Import", QString("Imported %1 items").arg(imported));
        dlg.accept();
        reload();
    });

    // Make dialog wider by default so file chooser and labels are comfortable
    dlg.resize(800, dlg.sizeHint().height());
    dlg.exec();
}

inline int MainWindow::importBibTeX(const QString &path, const QString &collection) {
    auto items = parseBibTeXFile(path);
    int count = 0;
    for (auto &it : items) {
        it.id = gen_uuid();
        it.collection = collection.toStdString();
        db->addItem(it);
        ++count;
    }
    return count;
}

inline int MainWindow::importZoteroRDF(const QString &path, const QString &collection) {
    auto items = parseZoteroRDFFile(path);
    int count = 0;
    for (auto &it : items) { it.id = gen_uuid(); it.collection = collection.toStdString(); db->addItem(it); ++count; }
    return count;
}

inline int MainWindow::importEndNoteXML(const QString &path, const QString &collection) {
    auto items = parseEndNoteXMLFile(path);
    int count = 0;
    for (auto &it : items) { it.id = gen_uuid(); it.collection = collection.toStdString(); db->addItem(it); ++count; }
    return count;
}

inline int MainWindow::importMendeleyXML(const QString &path, const QString &collection) {
    auto items = parseMendeleyXMLFile(path);
    int count = 0;
    for (auto &it : items) { it.id = gen_uuid(); it.collection = collection.toStdString(); db->addItem(it); ++count; }
    return count;
}
