#pragma once

inline MainWindow::MainWindow(const std::string &dbPath, QWidget *parent) : QMainWindow(parent) {
    ui = new UI();
    db = new Database(dbPath);
    db->init();

    // Main layout
    auto *mainWidget = new QWidget();
    auto *h = new QHBoxLayout(mainWidget);
    this->setCentralWidget(mainWidget);

    // Left: collections
    ui->collectionsList = new QTreeWidget();
    ui->collectionsList->setMinimumWidth(200);
    ui->collectionsList->setHeaderHidden(true);
    ui->collectionsList->setIndentation(16);
    ui->collectionsList->setRootIsDecorated(false);
    ui->collectionsList->setColumnCount(1);
    ui->collectionsList->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->collectionsList->installEventFilter(this);
    ui->collectionsList->setAcceptDrops(true);
    ui->collectionsList->setDropIndicatorShown(true);
    h->addWidget(ui->collectionsList);

    // Center: items
    auto *centerWidget = new QWidget();
    auto *centerLayout = new QVBoxLayout(centerWidget);
    ui->itemsList = new QListWidget();
    ui->itemsList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    ui->itemsList->setContextMenuPolicy(Qt::CustomContextMenu);
    ui->itemsList->installEventFilter(this);
    ui->itemsList->setFocus();
    ui->itemsList->setDragEnabled(true);
    ui->itemsList->setDragDropMode(QAbstractItemView::DragOnly);
    centerLayout->addWidget(new QLabel("Items"));
    // Search bar: search by title, author, DOI or ISBN
    ui->search = new QLineEdit();
    ui->search->setPlaceholderText("Search title, author, DOI or ISBN");
    ui->search->setClearButtonEnabled(true);
    // Place search and controls in a horizontal row
    auto *searchRow = new QWidget();
    auto *searchRowLayout = new QHBoxLayout(searchRow);
    searchRowLayout->setContentsMargins(0,0,0,0);
    searchRowLayout->addWidget(ui->search, 1);

    // gear button to configure BibTeX export key preference
    QToolButton *bibSettingsBtn = new QToolButton();
    bibSettingsBtn->setText("⚙");
    bibSettingsBtn->setToolTip("BibTeX export key settings");
    QMenu *bibMenu = new QMenu(bibSettingsBtn);
    // explanatory section title
    bibMenu->addSection("BibTeX export identifier:");
    QAction *opt1 = bibMenu->addAction("Author + short title + year");
    opt1->setCheckable(true);
    QAction *opt2 = bibMenu->addAction("DOI or ISBN");
    opt2->setCheckable(true);
    // make them mutually exclusive
    QActionGroup *bibGroup = new QActionGroup(bibMenu);
    bibGroup->setExclusive(true);
    bibGroup->addAction(opt1);
    bibGroup->addAction(opt2);
    bibSettingsBtn->setMenu(bibMenu);
    bibSettingsBtn->setPopupMode(QToolButton::InstantPopup);
    searchRowLayout->addWidget(bibSettingsBtn);

    centerLayout->addWidget(searchRow);

    ui->addBtn = new QPushButton("Add New Item");
    centerLayout->addWidget(ui->addBtn);
    centerLayout->addWidget(ui->itemsList);
    h->addWidget(centerWidget, 1);

    // Right: details form
    auto *rightWidget = new QWidget();
    auto *form = new QFormLayout(rightWidget);
    ui->entryType = new QComboBox();
    struct TypeExample { const char *type; const char *example; } examples[] = {
        {"article", "@article{key, author={...}, title={...}, journal={...}, year={...}}"},
        {"book", "@book{key, author={...}, title={...}, publisher={...}, year={...}}"},
        {"booklet", "@booklet{key, title={...}, howpublished={...}}"},
        {"conference", "@inproceedings{key, author={...}, booktitle={...}, year={...}}"},
        {"inproceedings", "@inproceedings{key, author={...}, booktitle={...}, year={...}}"},
        {"inbook", "@inbook{key, author={...}, title={...}, pages={...}}"},
        {"incollection", "@incollection{key, author={...}, booktitle={...}, year={...}}"},
        {"manual", "@manual{key, title={...}, author={...}, organization={...}}"},
        {"mastersthesis", "@mastersthesis{key, author={...}, title={...}, school={...}, year={...}}"},
        {"misc", "@misc{key, title={...}, howpublished={...}, year={...}}"},
        {"phdthesis", "@phdthesis{key, author={...}, title={...}, school={...}, year={...}}"},
        {"proceedings", "@proceedings{key, editor={...}, title={...}, year={...}}"},
        {"techreport", "@techreport{key, title={...}, institution={...}, year={...}}"},
        {"unpublished", "@unpublished{key, author={...}, title={...}, year={...}}"}
    };
    for (const auto &te : examples) {
        ui->entryType->addItem(QString::fromUtf8(te.type));
        int idx = ui->entryType->count() - 1;
        ui->entryType->setItemData(idx, QString::fromUtf8(te.example), Qt::ToolTipRole);
    }
    ui->title = new QLineEdit();
    ui->authors = new QLineEdit();
    ui->year = new QLineEdit();
    ui->isbn = new QLineEdit();
    ui->doi = new QLineEdit();
    ui->collectionCheckList = new QListWidget();
    ui->collectionCheckList->setMaximumHeight(120);
    ui->attachmentsList = new QListWidget();
    ui->attachmentsList->setMaximumHeight(120);
    ui->attachmentsList->setAcceptDrops(true);
    ui->attachmentsList->setDragDropMode(QAbstractItemView::DropOnly);
    ui->attachmentsList->installEventFilter(this);
    form->addRow("Entry Type", ui->entryType);
    form->addRow("Title", ui->title);
    form->addRow("Authors", ui->authors);
    form->addRow("Year", ui->year);
    form->addRow("ISBN", ui->isbn);
    form->addRow("DOI", ui->doi);

    // Active dynamic fields layout: non-empty dynamic fields will be inserted
    // directly into the main form so they appear as regular labeled rows
    ui->dynamicActiveContainer = nullptr;
    ui->dynamicActiveLayout = form;
    // Remember insertion point so dynamic non-empty fields appear after these core fields
    ui->dynamicInsertIndex = form->rowCount();

    ui->dynamicFieldsScroll = new QScrollArea();
    ui->dynamicFieldsScroll->setWidgetResizable(true);
    QWidget *dfContainer = new QWidget();
    ui->dynamicFieldsLayout = new QFormLayout(dfContainer);
    dfContainer->setLayout(ui->dynamicFieldsLayout);
    ui->dynamicFieldsScroll->setWidget(dfContainer);
    ui->dynamicFieldsScroll->setMinimumHeight(120);
    ui->dynamicFieldsScroll->setMaximumHeight(320);
    form->addRow("Blank fields", ui->dynamicFieldsScroll);

    form->addRow("Collections", ui->collectionCheckList);
    form->addRow("Attachments", ui->attachmentsList);
    rightWidget->setMinimumWidth(300);
    h->addWidget(rightWidget);

    // Connect signals
    connect(ui->addBtn, &QPushButton::clicked, this, &MainWindow::onAdd);
    connect(ui->attachmentsList, &QListWidget::itemClicked, [this](QListWidgetItem *item){
        if (!item) return;
        if (item->data(Qt::UserRole).toString() != "__placeholder") return;
        QStringList files = QFileDialog::getOpenFileNames(this, "Add Attachments");
        if (files.isEmpty()) return;
        auto selectedItems = ui->itemsList->selectedItems();
        if (selectedItems.isEmpty()) return;
        std::string itemId = selectedItems.first()->data(Qt::UserRole).toString().toStdString();
        Item dbItem;
        if (!db->getItem(itemId, dbItem)) return;
        QStringList existing = QString::fromStdString(dbItem.pdf_path).split(';', Qt::SkipEmptyParts);
        for (const QString &f : files) {
            if (!existing.contains(f)) existing << f;
        }
        dbItem.pdf_path = existing.join(';').toStdString();
        db->updateItem(dbItem);
        onItemSelected();
    });
    connect(ui->itemsList, &QListWidget::itemDoubleClicked, this, &MainWindow::onOpenItem);
    connect(ui->itemsList, &QListWidget::customContextMenuRequested, this, &MainWindow::onItemContextMenuRequested);
    connect(ui->collectionsList, &QWidget::customContextMenuRequested, this, &MainWindow::onCollectionContextMenuRequested);
    connect(ui->itemsList, &QListWidget::itemClicked, this, &MainWindow::onItemSelected);
    connect(ui->itemsList, &QListWidget::itemSelectionChanged, this, &MainWindow::onItemSelected);
    connect(ui->collectionsList, &QTreeWidget::itemClicked, this, &MainWindow::onCollectionSelected);

    // Search filtering: show matching items when there's text, otherwise show current collection
    connect(ui->search, &QLineEdit::textChanged, [this](const QString &text){
        QString q = text.trimmed();
        ui->itemsList->clear();
        if (q.isEmpty()) {
            // restore normal view (current collection)
            onCollectionSelected();
            return;
        }

        auto items = db->listItems();
        for (const auto &it : items) {
            QString title = QString::fromStdString(it.title);
            QString authors = QString::fromStdString(it.authors);
            QString doi = QString::fromStdString(it.doi);
            QString isbn = QString::fromStdString(it.isbn);
            if (title.contains(q, Qt::CaseInsensitive) || authors.contains(q, Qt::CaseInsensitive)
                || doi.contains(q, Qt::CaseInsensitive) || isbn.contains(q, Qt::CaseInsensitive)) {
                auto *listItem = new QListWidgetItem(title);
                listItem->setData(Qt::UserRole, QString::fromStdString(it.id));
                listItem->setData(Qt::UserRole + 1, QString::fromStdString(it.pdf_path));
                if (!it.pdf_path.empty()) listItem->setToolTip(QString::fromStdString(it.pdf_path));
                ui->itemsList->addItem(listItem);
            }
        }
    });

    // Initialize bib settings menu state from QSettings and show as mutually-exclusive checks
    QSettings settings("bello", "bello");
    int pref = settings.value("export/bibkey", 1).toInt();
    if (pref == 1) opt1->setChecked(true); else opt2->setChecked(true);
    connect(opt1, &QAction::triggered, [this]() {
        QSettings("bello","bello").setValue("export/bibkey", 1);
    });
    connect(opt2, &QAction::triggered, [this]() {
        QSettings("bello","bello").setValue("export/bibkey", 2);
    });

    // Auto-save
    connect(ui->title, &QLineEdit::editingFinished, this, &MainWindow::onSaveItem);
    connect(ui->authors, &QLineEdit::editingFinished, this, &MainWindow::onSaveItem);
    connect(ui->year, &QLineEdit::editingFinished, this, &MainWindow::onSaveItem);
    connect(ui->isbn, &QLineEdit::editingFinished, this, &MainWindow::onSaveItem);
    connect(ui->doi, &QLineEdit::editingFinished, this, &MainWindow::onSaveItem);
    connect(ui->entryType, &QComboBox::currentTextChanged, [this](const QString &){
        if (!ui->entryType->signalsBlocked()) {
            populateDynamicFields(ui->entryType->currentText(), nullptr);
            onSaveItem();
        }
    });
    connect(ui->collectionCheckList, &QListWidget::itemChanged, this, &MainWindow::onCollectionCheckChanged);

    ui->attachmentsList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(ui->attachmentsList, &QListWidget::itemDoubleClicked, this, &MainWindow::onOpenAttachment);
    connect(ui->attachmentsList, &QListWidget::customContextMenuRequested, this, &MainWindow::onAttachmentContextMenuRequested);

    // Shortcuts
    auto *scCopy = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_C), this);
    connect(scCopy, &QShortcut::activated, [this](){
        auto selectedItems = ui->itemsList->selectedItems();
        if (selectedItems.isEmpty()) return;
        QStringList citations;
        for (auto *it : selectedItems) {
            Item item;
            if (db->getItem(it->data(Qt::UserRole).toString().toStdString(), item)) {
                citations << formatCitation(item);
            }
        }
        QApplication::clipboard()->setText(citations.join("\n\n"));
    });
    auto *scBib = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_B), this);
    connect(scBib, &QShortcut::activated, [this](){
        auto selectedItems = ui->itemsList->selectedItems();
        if (selectedItems.isEmpty()) return;
        QStringList bibTexEntries;
        for (auto *it : selectedItems) {
            Item item;
            if (db->getItem(it->data(Qt::UserRole).toString().toStdString(), item)) {
                bibTexEntries << itemToBibTeX(item);
            }
        }
        QApplication::clipboard()->setText(bibTexEntries.join("\n\n"));
    });

    auto *scSelectAll = new QShortcut(QKeySequence::SelectAll, ui->itemsList);
    connect(scSelectAll, &QShortcut::activated, [this](){
        ui->itemsList->selectAll();
    });

    // Initial population
    reload();

    // Start the connector as a BrowserConnector instance
    browserConnector = new BrowserConnector(db,
        [this]() { reload(); },
        [this](const std::string &createdId) {
            // Select the newly created/merged item in the UI
            // Ensure UI is updated first
            reload();
            for (int i = 0; i < ui->itemsList->count(); ++i) {
                auto *listItem = ui->itemsList->item(i);
                if (listItem->data(Qt::UserRole).toString().toStdString() == createdId) {
                    ui->itemsList->setCurrentItem(listItem);
                    listItem->setSelected(true);
                    onItemSelected();
                    break;
                }
            }
        }
    );
}

inline MainWindow::~MainWindow() {
    delete ui;
}
