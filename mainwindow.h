#pragma once
#include <QMainWindow>
#include <QVector>
#include <QLabel>
#include <QFrame>
#include <QGridLayout>
#include <QThread>
#include "plcworker.h"

QT_BEGIN_NAMESPACE
namespace Ui { class MainWindow; }
QT_END_NAMESPACE

struct Tile {
    QFrame* frame = nullptr;
    QLabel* lblAddr = nullptr;
    QLabel* lblVal = nullptr;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
protected:
    void closeEvent(QCloseEvent* e) override;

private slots:
    void onConnectClicked();
    void onAutoChanged(int state);
    void onIntervalChanged(int v);

    void onAddClicked();
    void onRemoveClicked();

    void onReadOneClicked();
    void onWriteOneClicked();

    void onWorkerConnected(bool ok, const QString& err);
    void onStatusText(const QString& s);
    void onPointsUpdated(const QVector<int>& values);
    void onOneReadResult(bool ok, int value, const QString& err);
    void onOneWriteResult(bool ok, const QString& err);

private:
    bool parseAddrLocal(const QString& in, Area& area, int& byteIndex, int& bitIndex, QString& normalized, QString& err);
    void initTiles();
    void refreshTiles();
    void setTileValue(int idx, int v);
    void setUiConnected(bool connected);

    // --- Points config (points.json in exe folder) ---
    QString configFilePath() const;
    bool loadPointConfig(QString* errOut = nullptr);
    bool savePointConfig(QString* errOut = nullptr) const;
    void syncPointsToWorker();


private:
    Ui::MainWindow *ui;

    QVector<Point> m_points;     // UI端保存一份（用于显示地址）
    QVector<Tile>  m_tiles;      // 固定 16 个
    QGridLayout*   m_pointsGrid = nullptr;

    QThread* m_thread = nullptr;
    PlcWorker* m_worker = nullptr;
};
