#pragma once

#include <QMainWindow>
#include <QTreeWidget>
#include <QListWidget>
#include <QLineEdit>
#include <QFormLayout>
#include <QScrollArea>
#include <QTextEdit>
#include <QGroupBox>
#include <QComboBox>
#include <QMap>
#include <QPushButton>
#include <QWidget>
#include <QMimeData>
#include <QDropEvent>
#include <QMenu>
#include <QToolButton>
#include <QActionGroup>
#include <memory>
#include "Database.h"
#include "BrowserConnector.h"

#include <QTcpServer>
#include <QTcpSocket>
#include <QHostAddress>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

// Cross-platform UUID support
#ifdef _WIN32
#include <windows.h>
#endif

#include <QLabel>
#include <QFileDialog>
#include <QShortcut>
#include <QApplication>
#include <QFileInfo>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QFont>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QKeyEvent>
#include <QDragEnterEvent>
#include <QClipboard>
#include <QFileIconProvider>

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(const std::string &dbPath, QWidget *parent = nullptr);
    ~MainWindow();

    bool eventFilter(QObject *watched, QEvent *event) override;
    void reload();
    QStringList fieldsForType(const QString &type);
    void populateDynamicFields(const QString &type, const Item *item);
    void onItemSelected();
    void onCollectionCheckChanged(QListWidgetItem *changedItem);
    void onSaveItem();
    void onOpenAttachment(QListWidgetItem *item);
    void onAttachmentContextMenuRequested(const QPoint &pos);
    void onRemoveAttachment();
    void onCollectionSelected();
    void onItemContextMenuRequested(const QPoint &pos);
    void onAdd();
    void onUpload();
    void onOpenItem();
    void onRenameItem();
    void onDeleteItem();
    void copySelectedAsBibTeX();
    void ensureShortcuts();
    void onCollectionContextMenuRequested(const QPoint &pos);
    void deleteCollection(const QString &name);
    void renameCollection(const QString &oldName);
    void exportCollection(const QString &name);
    void createCollection();
    void createSubcollection(const QString &parent);
    QString itemPath(QTreeWidgetItem* item) const;
    QStringList collectExpandedPaths() const;
    void restoreExpandedPaths(const QStringList &paths);
    QTreeWidgetItem* ensureChild(QTreeWidgetItem* parent, const QString &name);
    void importToCollection(const QString &name);
    void importItemsDialog(const QString &targetCollection);
    int importBibTeX(const QString &path, const QString &collection);
    int importZoteroRDF(const QString &path, const QString &collection);
    int importEndNoteXML(const QString &path, const QString &collection);
    int importMendeleyXML(const QString &path, const QString &collection);
    QString formatCitation(const Item &it);
    QString itemToBibTeX(const Item &it);

    struct UI {
        QTreeWidget *collectionsList = nullptr;
        QListWidget *itemsList = nullptr;
        QListWidget *collectionCheckList = nullptr;
        QListWidget *attachmentsList = nullptr;
        QComboBox *entryType = nullptr;
        QLineEdit *title = nullptr;
        QLineEdit *authors = nullptr;
        QLineEdit *search = nullptr;
        QLineEdit *year = nullptr;
        QLineEdit *isbn = nullptr;
        QLineEdit *doi = nullptr;
        QWidget *dynamicActiveContainer = nullptr;
        QFormLayout *dynamicActiveLayout = nullptr;
        QScrollArea *dynamicFieldsScroll = nullptr;
        QFormLayout *dynamicFieldsLayout = nullptr;
        int dynamicInsertIndex = 0;
        QMap<QString, QWidget*> dynamicFieldEdits;
        QPushButton *addBtn = nullptr;
    } *ui = nullptr;

private:
    Database *db = nullptr;
    QTcpServer *connectorServer = nullptr;
    BrowserConnector *browserConnector = nullptr;
    void startConnectorServer();
};

#include "Helpers.h"
#include "LeftSection.h"
#include "CenterSection.h"
#include "RightSection.h"

#include "EventHandlers.h"
#include "UI.h"
