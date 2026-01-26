#pragma once
#include <QObject>
#include <QTimer>
#include <QVector>
#include <QString>
#include <QByteArray>
#include "plcsnap7client.h"

enum class Area { I, Q, M };

struct Point {
    Area area;
    int byteIndex;
    int bitIndex;
    QString text; // e.g. "I0.0"
};

class PlcWorker : public QObject {
    Q_OBJECT
public:
    explicit PlcWorker(QObject* parent=nullptr);

signals:
    void connectedResult(bool ok, const QString& err);
    void statusText(const QString& s);

    // 监视点更新（顺序与 points 一致）
    void pointsUpdated(const QVector<int>& values);

    // 单点读写结果
    void oneReadResult(bool ok, int value, const QString& err);
    void oneWriteResult(bool ok, const QString& err);

public slots:
    void connectToPlc(const QString& ip);
    void disconnectPlc();

    void setAutoRefresh(bool enabled, int intervalMs);
    void setInterval(int intervalMs);

    void addPoint(int areaInt, int byteIndex, int bitIndex, const QString& text); // areaInt: 0=I 1=Q 2=M
    void removePoint(int index);
    void clearPoints();

    void readOne(const QString& addr);
    void writeOne(const QString& addr, int value); // value: 0/1

private slots:
    void pollOnce();

private:
    bool parseAddr(const QString& in, Area& area, int& byteIndex, int& bitIndex, QString& normalized, QString& err);

private slots://加两个私有 slot 专门启动/停止定时器
    void startPolling();
    void stopPolling();


private:
    PlcSnap7Client m_plc;
    QTimer m_timer;
    int m_intervalMs = 200;
    bool m_auto = false;

    QVector<Point> m_points; // Max 16
};
