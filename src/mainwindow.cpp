#include "mainwindow.h"
#include "ui_mainwindow.h"


#include <QFileDialog>
#include <QListWidget>
#include <QTableView>
#include <QSplitter>
#include <QHBoxLayout>
#include <QMessageBox>
#include <QDir>
#include <QSettings>
#include <QtDebug>
#include <QTime>
#include <QHeaderView>
#include <QWidgetAction>
#include <QTemporaryFile>
#include <QFile>
#include <QUndoStack>

#include <algorithm>
#include <unordered_map>
#include <vector>

#include "detailswidget.h"
#include "umpcrawler.h"
#include "umpmodel.h"

#define APP_MAGIC 0xA1B9E9F7
#define APP_VERSION 001

#include "globalLog.h"

class ViewUndoCommand : public QUndoCommand {
public:
    ViewUndoCommand(std::uint32_t redo, std::uint32_t undo)
        : QUndoCommand(), index_(redo), prevIndex_(undo) {}
    std::uint32_t index_;
    std::uint32_t prevIndex_;
};

struct SnapshotTabInfo {
    CrawledMemorySnapshot* snapshot_;
    QSplitter* spliter_;
    UMPTableProxyModel* snapshotModel_;
    UMPTableProxyModel* instanceModel_;
    QUndoStack* stack_;
    bool replaying_ = false;
    void BeginReplay() { replaying_ = true; }
    void EndReplay() { replaying_ = false; }
    void Push(std::uint32_t index) {
        if (replaying_)
            return;
        std::uint32_t undoIndex = std::numeric_limits<std::uint32_t>::max();
        if (stack_->index() > 0)
            undoIndex = static_cast<const ViewUndoCommand*>(stack_->command(stack_->index() - 1))->index_;
        stack_->push(new ViewUndoCommand(index, undoIndex));
    }
    std::uint32_t Prev() {
        if (stack_->canUndo()) {
            stack_->undo();
            return static_cast<const ViewUndoCommand*>(stack_->command(stack_->index()))->prevIndex_;
        } else {
            return std::numeric_limits<std::uint32_t>::max();
        }
    }
    std::uint32_t Next() {
        if (stack_->canRedo()) {
            auto index = static_cast<const ViewUndoCommand*>(stack_->command(stack_->index()))->index_;
            stack_->redo();
            return index;
        } else {
            return std::numeric_limits<std::uint32_t>::max();
        }
    }
};

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow) {
    ui->setupUi(this);

    ui->upperTabWidget->tabBar()->setContextMenuPolicy(Qt::ContextMenuPolicy::CustomContextMenu);
    connect(ui->upperTabWidget->tabBar(), &QTabBar::customContextMenuRequested, this, &MainWindow::OnTabBarContextMenuRequested);

    progressDialog_ = new QProgressDialog(this, Qt::WindowTitleHint | Qt::CustomizeWindowHint);
    progressDialog_->setWindowModality(Qt::WindowModal);
    progressDialog_->setAutoClose(true);
    progressDialog_->setCancelButton(nullptr);
    progressDialog_->close();

    startAppProcess_ = new StartAppProcess(this);
    connect(startAppProcess_, &StartAppProcess::ProcessFinished, this, &MainWindow::StartAppProcessFinished);
    connect(startAppProcess_, &StartAppProcess::ProcessErrorOccurred, this, &MainWindow::StartAppProcessErrorOccurred);

    remoteProcess_ = new RemoteProcess(this);
    connect(remoteProcess_, &RemoteProcess::DataReceived, this, &MainWindow::RemoteDataReceived);
    connect(remoteProcess_, &RemoteProcess::ConnectionLost, this, &MainWindow::RemoteConnectionLost);

    LoadSettings();

    mainTimer_ = new QTimer(this);
    connect(mainTimer_, SIGNAL(timeout()), this, SLOT(FixedUpdate()));
    mainTimer_->start(1000);

#ifndef Q_OS_WIN
    ui->pythonPushButton->setVisible(false);
#endif
}

MainWindow::~MainWindow() {
    delete ui;
}

const QString SETTINGS_WINDOW_W = "Window_W";
const QString SETTINGS_WINDOW_H = "Window_H";
const QString SETTINGS_APPNAME = "AppName";
const QString SETTINGS_SDKPATH = "SdkPath";
const QString SETTINGS_PYPATH = "PythonPath";
const QString SETTINGS_MAIN_SPLITER = "Main_Spliter";
const QString SETTINGS_LASTOPENDIR = "lastopen_dir";
const QString SETTINGS_ARCH = "target_arch";


void MainWindow::LoadSettings() {
    QSettings settings("MoreFun", "UnityMemPerf");
    int windowWidth = settings.value(SETTINGS_WINDOW_W).toInt();
    int windowHeight = settings.value(SETTINGS_WINDOW_H).toInt();
    if (windowWidth > 0 && windowHeight > 0) {
        this->resize(windowWidth, windowHeight);
    }
    ui->appNameLineEdit->setText(settings.value(SETTINGS_APPNAME).toString());
    ui->sdkPathLineEdit->setText(settings.value(SETTINGS_SDKPATH).toString());
    pythonPath_ = settings.value(SETTINGS_PYPATH).toString();
    ui->archComboBox->setCurrentText(settings.value(SETTINGS_ARCH, "armeabi-v7a").toString());
    ui->main_splitter->restoreState(settings.value(SETTINGS_MAIN_SPLITER).toByteArray());
    auto lastOpenDir = settings.value(SETTINGS_LASTOPENDIR).toString();
    if (QDir(lastOpenDir).exists())
        lastOpenDir_ = lastOpenDir;
}

void MainWindow::SaveSettings() {
    QSettings settings("MoreFun", "UnityMemPerf");
    settings.setValue(SETTINGS_WINDOW_W, this->width());
    settings.setValue(SETTINGS_WINDOW_H, this->height());
    settings.setValue(SETTINGS_APPNAME, ui->appNameLineEdit->text());
    settings.setValue(SETTINGS_SDKPATH, ui->sdkPathLineEdit->text());
    settings.setValue(SETTINGS_PYPATH, pythonPath_);
    settings.setValue(SETTINGS_ARCH, ui->archComboBox->currentText());
    settings.setValue(SETTINGS_MAIN_SPLITER, ui->main_splitter->saveState());
    if (QDir(lastOpenDir_).exists())
        settings.setValue(SETTINGS_LASTOPENDIR, lastOpenDir_);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    SaveSettings();
    QMainWindow::closeEvent(event);
}

void MainWindow::SaveToFile(QFile *file) {
    QDataStream stream(file);
    stream.setVersion(QDataStream::Qt_5_12);
    stream << static_cast<quint32>(APP_MAGIC);
    stream << static_cast<quint32>(APP_VERSION);
    stream << static_cast<quint32>(snapShots_.size());
    auto saveThing = [&](ThingInMemory* thing) {
        stream << thing->index_ << thing->size_;
        stream << static_cast<quint8>(thing->diff_) << thing->caption_;
    };
    for (int i = 0; i < snapShots_.size(); i++) {
        auto& snapshot = snapShots_[ui->upperTabWidget->widget(i)].snapshot_;
        stream << ui->upperTabWidget->tabText(i) << snapshot->isDiff_;
        // typeDescriptions
        stream << static_cast<quint32>(snapshot->typeDescriptions_.size());
        for (auto& type : snapshot->typeDescriptions_) {
            stream << static_cast<quint32>(type.flags_);
            if (!type.IsArray()) {
                stream << static_cast<quint32>(type.fields_.size());
                for (auto& field : type.fields_) {
                    stream << field.offset_ << field.typeIndex_ << field.name_ << field.isStatic_;
                }
                stream << type.staticsSize_;
                if (type.staticsSize_ > 0)
                    stream.writeRawData(reinterpret_cast<char*>(type.statics_), static_cast<int>(type.staticsSize_));
            }
            stream << type.baseOrElementTypeIndex_ << type.name_ << type.assemblyName_ <<
                      type.typeInfoAddress_ << type.size_ << type.typeIndex_;
        }
        // gcHandles
        stream << static_cast<quint32>(snapshot->gcHandles_.size());
        for (auto& gcHandle : snapshot->gcHandles_) {
            saveThing(&gcHandle);
        }
        // managed
        stream << static_cast<quint32>(snapshot->managedObjects_.size());
        for (auto& managed : snapshot->managedObjects_) {
            saveThing(&managed);
            stream << managed.address_;
            stream << managed.typeDescription_->typeIndex_;
        }
        // statics
        stream << static_cast<quint32>(snapshot->staticFields_.size());
        for (auto& statics : snapshot->staticFields_) {
            saveThing(&statics);
            stream << statics.typeDescription_->typeIndex_;
            stream << statics.nameHash_;
        }
        // refs refBys
        stream << static_cast<quint32>(snapshot->allObjects_.size());
        for (auto& thing : snapshot->allObjects_) {
            stream << static_cast<quint32>(thing->references_.size());
            for (auto& ref : thing->references_)
                stream << ref->index_;
            stream << static_cast<quint32>(thing->referencedBy_.size());
            for (auto& refBy : thing->referencedBy_)
                stream << refBy->index_;
        }
        // memory sections
        stream << static_cast<quint32>(snapshot->managedHeap_.size());
        for (auto& section : snapshot->managedHeap_) {
            stream << section.sectionStartAddress_;
            stream << section.sectionSize_;
            if (section.sectionSize_ > 0)
                stream.writeRawData(reinterpret_cast<char*>(section.sectionBytes_), static_cast<int>(section.sectionSize_));
        }
        // runtime
        stream << snapshot->runtimeInformation_.pointerSize;
        stream << snapshot->runtimeInformation_.objectHeaderSize;
        stream << snapshot->runtimeInformation_.arrayHeaderSize;
        stream << snapshot->runtimeInformation_.arrayBoundsOffsetInHeader;
        stream << snapshot->runtimeInformation_.arraySizeOffsetInHeader;
        stream << snapshot->runtimeInformation_.allocationGranularity;
    }
}

int MainWindow::LoadFromFile(QFile *file) {
    QDataStream stream(file);
    quint32 magic;
    stream >> magic;
    if (magic != APP_MAGIC)
        return -1;
    quint32 version;
    stream >> version;
    if (version != APP_VERSION)
        return -1;
    CleanWorkSpace();
    auto loadThing = [&](ThingInMemory* thing) {
        quint8 flag;
        stream >> thing->index_ >> thing->size_ >> flag >> thing->caption_;
        thing->diff_ = static_cast<CrawledDiffFlags>(flag);
    };
    quint32 size;
    stream >> size;
    for (quint32 i = 0; i < size; i++) {
        CrawledMemorySnapshot* snapshot = new CrawledMemorySnapshot();
        stream >> snapshot->name_ >> snapshot->isDiff_;
        // typeDescriptions
        quint32 count;
        stream >> count;
        snapshot->typeDescriptions_.resize(count);
        for (auto& type : snapshot->typeDescriptions_) {
            quint32 flag;
            stream >> flag;
            type.flags_ = static_cast<Il2CppMetadataTypeFlags>(flag);
            if (!type.IsArray()) {
                stream >> flag;
                type.fields_.resize(flag);
                for (auto& field : type.fields_) {
                    stream >> field.offset_ >> field.typeIndex_ >> field.name_ >> field.isStatic_;
                }
                stream >> type.staticsSize_;
                if (type.staticsSize_ > 0) {
                    type.statics_ = new quint8[type.staticsSize_];
                    stream.readRawData(reinterpret_cast<char*>(type.statics_), static_cast<int>(type.staticsSize_));
                }
            }
            stream >> type.baseOrElementTypeIndex_ >> type.name_ >> type.assemblyName_ >>
                    type.typeInfoAddress_ >> type.size_ >> type.typeIndex_;
        }
        // gcHandles
        stream >> count;
        snapshot->gcHandles_.resize(count);
        for (auto& gcHandle : snapshot->gcHandles_) {
            loadThing(&gcHandle);
        }
        // managed
        stream >> count;
        snapshot->managedObjects_.resize(count);
        for (auto& managed : snapshot->managedObjects_) {
            loadThing(&managed);
            stream >> managed.address_;
            quint32 typeIndex;
            stream >> typeIndex;
            managed.typeDescription_ = &snapshot->typeDescriptions_[typeIndex];
        }
        // statics
        stream >> count;
        snapshot->staticFields_.resize(count);
        for (auto& statics : snapshot->staticFields_) {
            loadThing(&statics);
            quint32 typeIndex;
            stream >> typeIndex;
            statics.typeDescription_ = &snapshot->typeDescriptions_[typeIndex];
            stream >> statics.nameHash_;
        }
        // allObjects
        snapshot->allObjects_.reserve(snapshot->gcHandles_.size() + snapshot->managedObjects_.size() + snapshot->staticFields_.size());
        std::uint32_t index = 0;
        for (auto& obj : snapshot->gcHandles_) {
            obj.index_ = index++;
            snapshot->allObjects_.push_back(&obj);
        }
        for (auto& obj : snapshot->staticFields_) {
            obj.index_ = index++;
            snapshot->allObjects_.push_back(&obj);
        }
        for (auto& obj : snapshot->managedObjects_) {
            obj.index_ = index++;
            snapshot->allObjects_.push_back(&obj);
        }
        // refs refBys
        stream >> count;
        for (auto& thing : snapshot->allObjects_) {
            quint32 refCount;
            stream >> refCount;
            thing->references_.resize(refCount);
            for (quint32 j = 0; j < refCount; j++) {
                quint32 refIndex;
                stream >> refIndex;
                thing->references_[j] = snapshot->allObjects_[refIndex];
            }
            stream >> refCount;
            thing->referencedBy_.resize(refCount);
            for (quint32 j = 0; j < refCount; j++) {
                quint32 refIndex;
                stream >> refIndex;
                thing->referencedBy_[j] = snapshot->allObjects_[refIndex];
            }
        }
        // memory sections
        stream >> count;
        snapshot->managedHeap_.resize(count);
        for (auto& section : snapshot->managedHeap_) {
            stream >> section.sectionStartAddress_;
            stream >> section.sectionSize_;
            if (section.sectionSize_ > 0) {
                section.sectionBytes_ = new quint8[section.sectionSize_];
                stream.readRawData(reinterpret_cast<char*>(section.sectionBytes_), static_cast<int>(section.sectionSize_));
            }
        }
        // runtime
        stream >> snapshot->runtimeInformation_.pointerSize;
        stream >> snapshot->runtimeInformation_.objectHeaderSize;
        stream >> snapshot->runtimeInformation_.arrayHeaderSize;
        stream >> snapshot->runtimeInformation_.arrayBoundsOffsetInHeader;
        stream >> snapshot->runtimeInformation_.arraySizeOffsetInHeader;
        stream >> snapshot->runtimeInformation_.allocationGranularity;
        ShowSnapshot(snapshot);

    }
    return 0;
}

void MainWindow::CleanWorkSpace() {
    while (ui->upperTabWidget->count() > 0) {
        ui->upperTabWidget->removeTab(0);
    }
    snapShots_.clear();
    firstDiffPage_ = nullptr;
}

void MainWindow::Print(const QString& str) {
    ui->consolePlainTextEdit->appendPlainText(str);
}

QString MainWindow::GetLastOpenDir() const {
    return QDir(lastOpenDir_).exists() ? lastOpenDir_ : QDir::homePath();
}

void MainWindow::ConnectionFailed() {
    isConnected_ = false;
    remoteProcess_->Disconnect();
    ui->appNameLineEdit->setEnabled(true);
    ui->launchPushButton->setText("Launch");
    ui->sdkPushButton->setEnabled(true);
    ui->actionOpen->setEnabled(true);
    ui->actionCapture_Snapshot->setEnabled(false);
}

void MainWindow::FixedUpdate() {
    if (!isConnected_)
        return;
    if (!remoteProcess_->IsConnecting() && !remoteProcess_->IsConnected()) {
        static int port = 8000;
        remoteProcess_->ConnectToServer(port++);
        Print("Connecting to application server ... ");
    }
    if (remoteProcess_->IsConnected()) {
        if (!ui->actionCapture_Snapshot->isEnabled())
            ui->actionCapture_Snapshot->setEnabled(true);
    } else {
        if (ui->actionCapture_Snapshot->isEnabled())
            ui->actionCapture_Snapshot->setEnabled(false);
    }
}

void MainWindow::StartAppProcessFinished(AdbProcess* process) {
    progressDialog_->setValue(progressDialog_->maximum());
    progressDialog_->close();
    auto startAppProcess = static_cast<StartAppProcess*>(process);
    if (!startAppProcess->Result()) {
        ConnectionFailed();
        Print("Error starting app by adb monkey");
        return;
    }
    isConnected_ = true;
    remoteProcess_->SetExecutablePath(adbPath_);
    Print("Application Started!");
    remoteRetryCount_ = 30;
}

void MainWindow::StartAppProcessErrorOccurred() {
    ConnectionFailed();
    progressDialog_->setValue(progressDialog_->maximum());
    progressDialog_->close();
    Print("Error starting app: " + startAppProcess_->ErrorStr());
}
bool MainWindow::exportExecl(QString &fileName, QString &datas){
        QString fullDirName = QDir::currentPath() ;
        QString dirFile = fullDirName + "/" + fileName + ".csv";

        dirFile = fileName;
        Print(fileName);
        QFile file(dirFile);
        bool ret = file.open(QIODevice::Truncate | QIODevice::ReadWrite);
        if(!ret)
        {
            qDebug() << "open failure";
            return ret;
        }

        QTextStream stream(&file);
        QString conTents =datas;
        // 写入头
        //conTents += "\n";

        // 写内容

        stream << conTents;

        file.close();
        return true;

}
//show memory  data
void MainWindow::ShowSnapshot(CrawledMemorySnapshot* crawled) {
    GlobalLogDef::log.append( "....");
    auto baseWidget = new QWidget();
    baseWidget->setLayout(new QHBoxLayout());
    auto getTableView = [](QWidget* parent) {
        auto view = new QTableView(parent);
        view->setSortingEnabled(true);
        view->setSelectionMode(QAbstractItemView::SelectionMode::SingleSelection);
        view->setSelectionBehavior(QAbstractItemView::SelectRows);
        view->setHorizontalScrollMode(QTableView::ScrollMode::ScrollPerPixel);
        view->setVerticalScrollMode(QTableView::ScrollMode::ScrollPerItem);
        view->verticalHeader()->setEnabled(false);
        view->setMinimumWidth(200);
        view->setWordWrap(false);
        //view->setColumnWidth(0,50); // auto set  row width
        return view;
    };
    auto typeTable = getTableView(baseWidget);
    auto instanceTable = getTableView(baseWidget);

    auto spliter = new QSplitter(baseWidget);
    spliter->addWidget(typeTable);
    spliter->addWidget(instanceTable);
    baseWidget->layout()->addWidget(spliter);

    auto snapshotModel = new UMPTypeGroupModel(crawled, typeTable);
    auto snapshotProxyModel = new UMPTableProxyModel(snapshotModel, typeTable);
    typeTable->setModel(snapshotProxyModel);
    typeTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeMode::ResizeToContents);//QHeaderView::ResizeMode::Stretch
    typeTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeMode::ResizeToContents);
    typeTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeMode::ResizeToContents);


    auto instanceModel = new UMPThingInMemoryModel(instanceTable);
    auto instanceProxyModel = new UMPTableProxyModel(instanceModel, instanceTable);
    instanceTable->setModel(instanceProxyModel);
    instanceTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeMode::ResizeToContents);//QHeaderView::ResizeMode::Stretch
    instanceTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeMode::ResizeToContents);
    instanceTable->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeMode::ResizeToContents);
    instanceTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeMode::ResizeToContents);
    instanceTable->horizontalHeader()->setSectionHidden(3, !crawled->isDiff_);

    auto detailPanel = new DetailsWidget(crawled);
    spliter->addWidget(detailPanel);
    spliter->setCollapsible(0, false);
    spliter->setCollapsible(1, false);
    spliter->setCollapsible(2, false);
    connect(detailPanel, &DetailsWidget::ThingSelected, this, &MainWindow::OnThingSelected);

    connect(typeTable->selectionModel(), &QItemSelectionModel::selectionChanged, [=](const QItemSelection &selected, const QItemSelection &) {
        if (selected.indexes().size() > 0) {
            auto index = selected.indexes()[0];
            if (index.isValid()) {
                auto row = snapshotProxyModel->mapToSource(index).row();
                auto data = snapshotModel->getSubModel(row);
                instanceTable->clearSelection();
                instanceModel->reset(data, snapshotModel->getSnapshot()->isDiff_);
            }
        }
    });

    connect(instanceTable->selectionModel(), &QItemSelectionModel::selectionChanged, [=](const QItemSelection &selected, const QItemSelection &) {
        if (selected.indexes().size() > 0) {
            auto index = selected.indexes()[0];
            if (index.isValid()) {
                auto row = instanceProxyModel->mapToSource(index).row();
                auto thing = instanceModel->thingAt(row);
                detailPanel->ShowThing(thing, thing->type());
                if (thing) {
                    snapShots_[baseWidget].Push(thing->index_);
                    UpdateShowNextPrev();
                }
            }
        }
    });

    SnapshotTabInfo tabInfo;
    tabInfo.stack_ = new QUndoStack(baseWidget);
    tabInfo.snapshot_ = crawled;
    tabInfo.spliter_ = spliter;
    tabInfo.snapshotModel_ = snapshotProxyModel;
    tabInfo.instanceModel_ = instanceProxyModel;
    snapShots_[baseWidget] = tabInfo;

    ui->upperTabWidget->addTab(baseWidget, crawled->name_);
    ui->upperTabWidget->setCurrentWidget(baseWidget);
    ui->upperTabWidget->setTabToolTip(
                ui->upperTabWidget->currentIndex(),
                "Total: " + sizeToString(snapshotModel->getTotalSize()));


    Print(GlobalLogDef::log);
    Print("88888888888888888.");

    QAbstractItemModel *model = typeTable->model ();
    QModelIndex index = model->index(3,3);
    QVariant data = model->data(index);
    QString str = "";
    str.append("No,Type,Count,Size\r\n");
    for(int i=0 ;i<model->rowCount();i++  ){
        for(int j = 0 ;j<model->columnCount() ;j++  ){
            index = model->index(i,j);
            str.append( model->data(index).toString() );
            if(j != model->columnCount()-1  )str.append(",");
        }
        str.append("\r\n");
    }
    ui->consolePlainTextEdit->clear();
    Print(crawled->name_);
    Print(str);
    _cacheCsvContent = str;
    //exportExecl(crawled->name_, str);
}

void MainWindow::UpdateShowNextPrev() {
    if (auto widget = ui->upperTabWidget->widget(ui->upperTabWidget->currentIndex())) {
        auto& info = snapShots_[widget];
        this->ui->actionJump_Back->setEnabled(info.stack_->canUndo());
        this->ui->actionJump_Forward->setEnabled(info.stack_->canRedo());
    } else {
        this->ui->actionJump_Back->setEnabled(false);
        this->ui->actionJump_Forward->setEnabled(false);
    }
}

void MainWindow::RemoteDataReceived() {
    remoteRetryCount_ = 5;

    Crawler crawler;
    auto snapshot = remoteProcess_->GetSnapShot();
    auto packedCrawlerData = new PackedCrawlerData(snapshot);
    crawler.Crawl(*packedCrawlerData, snapshot);
    auto crawled = new CrawledMemorySnapshot();
    crawled->Unpack(*crawled, snapshot, *packedCrawlerData);
    delete packedCrawlerData;

    crawled->name_ = "Snapshot_" + QTime::currentTime().toString("H_m_s");
    ShowSnapshot(crawled);
    Print("Snapshot Received And Unpacked.");
}

void MainWindow::RemoteConnectionLost() {
    if (!isConnected_)
        return;
    Print(QString("Connection failed, retrying %1").arg(remoteRetryCount_));
    remoteRetryCount_--;
    if (remoteRetryCount_ <= 0)
        ConnectionFailed();
}

void MainWindow::OnTabBarContextMenuRequested(const QPoint& pos) {
    int index = ui->upperTabWidget->tabBar()->tabAt(pos);
    if (index < 0)
        return;
    ui->upperTabWidget->setCurrentIndex(index);
    auto widget = ui->upperTabWidget->currentWidget();
    auto text = ui->upperTabWidget->tabText(index);
    QMenu menu(this);
    auto widgetAction = new QWidgetAction(&menu);
    auto lineEdit = new QLineEdit(text);
    connect(lineEdit, &QLineEdit::editingFinished, [&](){
        ui->upperTabWidget->setTabText(index, lineEdit->text());
    });
    widgetAction->setDefaultWidget(lineEdit);
    menu.addAction(widgetAction);
    if (widget != firstDiffPage_ && !snapShots_[widget].snapshot_->isDiff_) {
        menu.addAction(ui->actionMark_First);
        menu.addAction(ui->actionMark_Second);
    }
    lineEdit->setFocus();
    menu.exec(ui->upperTabWidget->tabBar()->mapToGlobal(pos));
}

void MainWindow::OnThingSelected(std::uint32_t index) {
    auto& info = snapShots_[ui->upperTabWidget->currentWidget()];
    if (index >= info.snapshot_->allObjects_.size())
        return;
    auto& thing = info.snapshot_->allObjects_[index];
    TypeDescription* type = nullptr;
    if (thing->type() == ThingType::MANAGED) {
        type = static_cast<ManagedObject*>(thing)->typeDescription_;
    } else if (thing->type() == ThingType::STATIC) {
        type = static_cast<StaticFields*>(thing)->typeDescription_;
    } else {
        return;
    }
    auto typeTable = static_cast<QTableView*>(info.spliter_->widget(0));
    auto typeGroupModel = static_cast<UMPTypeGroupModel*>(info.snapshotModel_->sourceModel());
    auto selectTypeIndex = info.snapshotModel_->mapFromSource(typeGroupModel->index(static_cast<int>(type->typeIndex_), 0));
    typeTable->selectRow(selectTypeIndex.row());
    auto instanceTable = static_cast<QTableView*>(info.spliter_->widget(1));
    auto thingModel = static_cast<UMPThingInMemoryModel*>(info.instanceModel_->sourceModel());
    auto thingModelIndex = thingModel->indexOf(thing);
    if (thingModelIndex != -1) {
        auto selectThingIndex = info.instanceModel_->mapFromSource(thingModel->index(thingModelIndex, 0));
        instanceTable->selectRow(selectThingIndex.row());
        return;
    }
}

void MainWindow::on_actionOpen_triggered() {
    QString fileName = QFileDialog::getOpenFileName(nullptr, tr("Open UnityMemPerf File"),
                                                    GetLastOpenDir(), tr("UnityMemPerf Files (*.uss)"));
    if (QFileInfo::exists(fileName)) {
        QFile file(fileName);
        if (file.open(QIODevice::ReadOnly)) {
            lastOpenDir_ = QFileInfo(fileName).dir().absolutePath();
            auto ecode = LoadFromFile(&file);
            if (ecode != 0) {
                QMessageBox::warning(this, "Warning", QString("Error reading file, ecode %1").arg(static_cast<int>(ecode)),
                                     QMessageBox::StandardButton::Ok);
            }
        }
        else {
            QMessageBox::warning(this, "Warning", "File not found!", QMessageBox::StandardButton::Ok);
        }
    }
}

void MainWindow::on_actionSave_triggered() {
    QString fileName = QFileDialog::getSaveFileName(nullptr, tr("Save UnityMemPerf File"),
                                                    GetLastOpenDir(), tr("UnityMemPerf Files (*.uss)"));
    if (fileName.isEmpty())
        return;
    QString csvFile = fileName;
    if (!fileName.endsWith("uss", Qt::CaseInsensitive))
        fileName += ".uss";
    QTemporaryFile tempFile;
    if (!tempFile.open()) {
        QMessageBox::warning(this, "Warning", "Can't create file!", QMessageBox::StandardButton::Ok);
        return;
    }
    SaveToFile(&tempFile);
    if (QFileInfo::exists(fileName) && !QFile(fileName).remove()) {
        QMessageBox::warning(this, "Warning", "Error removing file!", QMessageBox::StandardButton::Ok);
        return;
    }
    if (!tempFile.rename(fileName)) {
        QMessageBox::warning(this, "Warning", "Error renaming file!", QMessageBox::StandardButton::Ok);
        return;
    }
    tempFile.setAutoRemove(false);
    exportExecl(csvFile.append(".csv"), _cacheCsvContent);
}

void MainWindow::on_actionExit_triggered() {
    this->close();
}

void MainWindow::on_actionAbout_triggered() {
    QMessageBox::about(this, "About MoreFun UnityMemPerf", "Copyright 2019 MoreFun Studios, Tencent.");
}

void MainWindow::on_sdkPushButton_clicked() {
    auto currentPath = ui->sdkPathLineEdit->text();
    auto path = QFileDialog::getExistingDirectory(this, tr("Select Directory"), QDir(currentPath).exists() ? currentPath : QDir::homePath(),
                                      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (path.size() != 0)
        ui->sdkPathLineEdit->setText(path);
}

void MainWindow::on_pythonPushButton_clicked() {
    auto path = QFileDialog::getExistingDirectory(this, tr("Select Directory"), QDir(pythonPath_).exists() ? pythonPath_ : QDir::homePath(),
                                      QFileDialog::ShowDirsOnly | QFileDialog::DontResolveSymlinks);
    if (path.size() != 0)
        pythonPath_ = path;
}

class ArrowLineEdit : public QLineEdit {
public:
    ArrowLineEdit(QListWidget* listView, QWidget *parent = nullptr) :
        QLineEdit(parent), listView_(listView) { }
    void keyPressEvent(QKeyEvent *event);
private:
    QListWidget* listView_;
};

void ArrowLineEdit::keyPressEvent(QKeyEvent *event) {
    if (event->key() == Qt::Key_Up || event->key() == Qt::Key_Down) {
        auto selectedItems = listView_->selectedItems();
        QListWidgetItem* item = nullptr;
        if (selectedItems.count() >= 0) {
            auto currentRow = listView_->row(selectedItems[0]);
            if (event->key() == Qt::Key_Down) {
                while (currentRow + 1 < listView_->count()) {
                    currentRow++;
                    auto curItem = listView_->item(currentRow);
                    if (!curItem->isHidden()) {
                        item = curItem;
                        break;
                    }
                }
            } else {
                while (currentRow - 1 >= 0) {
                    currentRow--;
                    auto curItem = listView_->item(currentRow);
                    if (!curItem->isHidden()) {
                        item = curItem;
                        break;
                    }
                }
            }
        }
        if (item)
            listView_->setCurrentItem(item);
    } else {
        QLineEdit::keyPressEvent(event);
    }
}

void MainWindow::on_selectAppToolButton_clicked() {
    adbPath_ = ui->sdkPathLineEdit->text();
    adbPath_ = adbPath_.size() == 0 ? "adb" : adbPath_ + "/platform-tools/adb";
    QProcess process;
    process.setProgram(adbPath_);
    QStringList arguments;
    arguments << "shell" << "pm" << "list" << "packages";

#ifdef Q_OS_WIN
    process.setNativeArguments(arguments.join(' '));
#else
    process.setArguments(arguments);
#endif
    process.start();
    if (!process.waitForStarted()) {
        Print("error start adb shell pm list packages, make sure your device is connected!");
        return;
    }
    if (!process.waitForFinished()) {
        Print("error finishing adb shell pm list packages!");
        return;
    }
    auto pkgStrs = QString(process.readAll());
    auto lines = pkgStrs.split('\n', QString::SkipEmptyParts);
    if (lines.count() == 0) {
        return;
    }
    QDialog dialog(this, Qt::WindowTitleHint | Qt::WindowCloseButtonHint);
    auto layout = new QVBoxLayout();
    layout->setMargin(2);
    layout->setSpacing(2);
    auto listWidget = new QListWidget();
    listWidget->setSelectionMode(QListWidget::SelectionMode::SingleSelection);
    for (auto& line : lines) {
        auto lineParts = line.split(':');
        if (lineParts.count() > 1) {
            listWidget->addItem(lineParts[1].trimmed());
        }
    }
    listWidget->setCurrentItem(listWidget->item(0));
    auto searchLineEdit = new ArrowLineEdit(listWidget);
    // text filtering
    connect(searchLineEdit, &QLineEdit::textChanged, [&](const QString &text) {
        // Use regular expression to search fuzzily
        // "Hello\n" -> ".*H.*e.*l.*l.*o.*\\.*n"
        QString pattern;
        for (auto i = 0; i < text.size(); i++) {
            pattern += QRegularExpression::escape(text[i]);
            if (i != text.size() - 1)
                pattern += ".*";
        }
        QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        auto first = true;
        for (int i = 0; i < listWidget->count(); ++i) {
            auto item = listWidget->item(i);
            if (item->text().contains(re)) {
                item->setHidden(false);
                item->setSelected(first);
                first = false;
            } else {
                item->setHidden(true);
                item->setSelected(false);
            }
        }
    });
    connect(searchLineEdit, &QLineEdit::returnPressed, [&]() {
        auto selected = listWidget->selectedItems();
        if (selected.count() > 0)
            ui->appNameLineEdit->setText(selected[0]->text());
        dialog.close();
    });
    connect(listWidget, &QListWidget::itemClicked, [&](QListWidgetItem *item) {
        ui->appNameLineEdit->setText(item->text());
        dialog.close();
    });
    layout->addWidget(searchLineEdit);
    layout->addWidget(listWidget);
    searchLineEdit->setFocus();
    dialog.setLayout(layout);
    dialog.setWindowModality(Qt::WindowModal);
    dialog.setWindowTitle("Select Application");
    dialog.setMinimumSize(400, 300);
    dialog.resize(400, 300);
    dialog.exec();
}

void MainWindow::on_launchPushButton_clicked() {
    adbPath_ = ui->sdkPathLineEdit->text();
    adbPath_ = adbPath_.size() == 0 ? "adb" : adbPath_ + "/platform-tools/adb";

#ifdef Q_OS_WIN
//    auto pythonPath = QCoreApplication::applicationDirPath() + "/python.exe";
    auto pythonPath = pythonPath_ + "/python.exe";
#else
    auto pythonPath = QCoreApplication::applicationDirPath() + "/python";
#endif
    if (!QFile::exists(pythonPath)) {
        QMessageBox::warning(this, "Warning", "Python executable not found!");
        return;
    }

    if (isConnected_) {
        progressDialog_->setWindowTitle("Stop Capture Progress");
        progressDialog_->setLabelText(QString("Stopping capture ..."));
        progressDialog_->setMinimum(0);
        progressDialog_->setMaximum(1);
        progressDialog_->setValue(0);
        progressDialog_->show();
        ConnectionFailed();
        QStringList arguments;
        arguments << "shell" << "am" << "force-stop" << ui->appNameLineEdit->text();
        QProcess process;
        process.setProgram(adbPath_);
#ifdef Q_OS_WIN
        process.setNativeArguments(arguments.join(' '));
#else
        process.setArguments(arguments);
#endif
        process.start();
        process.waitForStarted();
        process.waitForFinished();
        progressDialog_->setValue(1);
        return;
    }

    CleanWorkSpace();

    progressDialog_->setWindowTitle("Launch Progress");
    progressDialog_->setLabelText("Preparing ...");
    progressDialog_->setMinimum(0);
    progressDialog_->setMaximum(7);
    progressDialog_->setValue(0);
    progressDialog_->show();

    ui->sdkPushButton->setEnabled(true);
    ui->appNameLineEdit->setEnabled(false);
    ui->launchPushButton->setText("Stop Capture");
    ui->actionOpen->setEnabled(false);
    ui->statusBar->clearMessage();

    startAppProcess_->SetPythonPath(pythonPath);
    startAppProcess_->SetExecutablePath(adbPath_);
    startAppProcess_->StartApp(ui->appNameLineEdit->text(), ui->archComboBox->currentText(), progressDialog_);

    Print("Starting application ...");
}

void MainWindow::on_actionCapture_Snapshot_triggered() {
    if (remoteProcess_->IsConnected()) {
        remoteProcess_->Send(UMPMessageType::CAPTURE_SNAPSHOT);
        Print("Requesting Memory Snapshot ... ");
    }
}

void MainWindow::on_upperTabWidget_tabCloseRequested(int index) {
    auto widget = ui->upperTabWidget->widget(index);
    if (widget == firstDiffPage_)
        firstDiffPage_ = nullptr;
    snapShots_.remove(widget);
    ui->upperTabWidget->removeTab(index);
    widget->deleteLater();
}

void MainWindow::on_upperTabWidget_currentChanged(int) {
    UpdateShowNextPrev();
}

void MainWindow::on_actionJump_Back_triggered() {
    auto& info = snapShots_[ui->upperTabWidget->currentWidget()];
    info.BeginReplay();
    OnThingSelected(info.Prev());
    info.EndReplay();
    UpdateShowNextPrev();
}

void MainWindow::on_actionJump_Forward_triggered() {
    auto& info = snapShots_[ui->upperTabWidget->currentWidget()];
    info.BeginReplay();
    OnThingSelected(info.Next());
    info.EndReplay();
    UpdateShowNextPrev();
}

void MainWindow::on_actionMark_First_triggered() {
    if (ui->upperTabWidget->count() == 0)
        return;
    firstDiffPage_ = ui->upperTabWidget->currentWidget();
}

void MainWindow::on_actionMark_Second_triggered() {
    if (ui->upperTabWidget->count() == 0)
        return;
    if (firstDiffPage_ == nullptr) {
        QMessageBox::warning(this, "Warning", "Please select first diff page then select the second one!");
        return;
    }
    auto secondDiffPage = ui->upperTabWidget->currentWidget();
    auto firstTitle = ui->upperTabWidget->tabText(ui->upperTabWidget->indexOf(firstDiffPage_));
    auto secondTitle = ui->upperTabWidget->tabText(ui->upperTabWidget->indexOf(secondDiffPage));
    if (QMessageBox::information(
                this, "Message", QString("Diff %1 with %2?").arg(firstTitle).arg(secondTitle),
                QMessageBox::StandardButton::Yes | QMessageBox::StandardButton::No) == QMessageBox::StandardButton::Yes) {
        ShowSnapshot(CrawledMemorySnapshot::Diff(snapShots_[firstDiffPage_].snapshot_, snapShots_[secondDiffPage].snapshot_));
    }
}
