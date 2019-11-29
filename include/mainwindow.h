#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QProgressDialog>
#include <QTimer>
#include <QMap>
#include <QTableView>
#include "startappprocess.h"
#include "remoteprocess.h"


namespace Ui {
class MainWindow;
}

class QFile;
struct CrawledMemorySnapshot;
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void LoadSettings();
    void SaveSettings();

    void closeEvent(QCloseEvent *event) override;
    static QString globalLog ;

private:
    void SaveToFile(QFile *file);
    int LoadFromFile(QFile *file);
    void CleanWorkSpace();
    void Print(const QString& str);
    QString GetLastOpenDir() const;

    void ConnectionFailed();
    void ShowSnapshot(CrawledMemorySnapshot* snapshot);
    void UpdateShowNextPrev();
    QString _cacheCsvContent;
    bool exportExecl( QString &fileName, QString &datas);


private slots:
    void FixedUpdate();
    void StartAppProcessFinished(AdbProcess* process);
    void StartAppProcessErrorOccurred();
    void RemoteDataReceived();
    void RemoteConnectionLost();

    void OnTabBarContextMenuRequested(const QPoint& pos);
    void OnThingSelected(std::uint32_t index);

    void on_actionOpen_triggered();
    void on_actionSave_triggered();
    void on_actionExit_triggered();
    void on_actionAbout_triggered();
    void on_sdkPushButton_clicked();
    void on_pythonPushButton_clicked();
    void on_selectAppToolButton_clicked();
    void on_launchPushButton_clicked();
    void on_actionCapture_Snapshot_triggered();
    void on_upperTabWidget_tabCloseRequested(int index);
    void on_upperTabWidget_currentChanged(int index);
    void on_actionJump_Back_triggered();
    void on_actionJump_Forward_triggered();
    void on_actionMark_First_triggered();
    void on_actionMark_Second_triggered();

private:
    Ui::MainWindow *ui;
    QProgressDialog *progressDialog_;
    QString adbPath_;
    QString pythonPath_;
    QString appPid_;
    QString lastOpenDir_;
    QTimer* mainTimer_;

    QMap<QWidget*, struct SnapshotTabInfo> snapShots_;
    QWidget* firstDiffPage_ = nullptr;

    // adb shell monkey -p packagename -c android.intent.category.LAUNCHER 1
    StartAppProcess *startAppProcess_;

    RemoteProcess *remoteProcess_;
    int remoteRetryCount_ = 0;

    bool isConnected_ = false;
};

#endif // MAINWINDOW_H
