#pragma once
#include <QObject>
#include <QTimer>
#include <QVector>
#include <QElapsedTimer>
#include "plcsnap7client.h"
enum class Area { I, Q, M };
struct Point { Area area; int byteIndex; int bitIndex; QString text; };
class PlcWorker : public QObject {
    Q_OBJECT
public:
    explicit PlcWorker(QObject* parent=nullptr);
    QString logFilePath() const { return m_logPath; }
signals:
    void connectedResult(bool ok,const QString& err);
    void statusText(const QString& text);
    void pollingTiming(double intervalMs,double readMs,bool success);
    void faultUpdated(int value); // M5.0: -1 unknown, 0 clear, 1 fault
    void pointsUpdated(const QVector<int>& values);
    void oneWriteResult(bool ok,const QString& text);
    void actionLog(const QString& text);
    void writeModeReset();
    void pulseState(bool running,const QString& text);
public slots:
    void connectToPlc(const QString& ip);
    void disconnectPlc();
    void setAutoRefresh(bool enabled,int intervalMs);
    void setInterval(int intervalMs);
    void addPoint(int areaInt,int byteIndex,int bitIndex,const QString& text);
    void setWriteMode(bool enabled,bool feedback);
    void writeOne(const QString& address,int value);
    void startPulse(int periodMs,int highMs);
    void stopPulse();
private slots:
    void pollOnce();
    void pulseEdge();
private:
    bool fresh() const;
    bool writeBit(int byte,int bit,int value,const QString& source);
    void revoke(const QString& reason);
    void log(const QString& message);
    QString m_logPath;
    bool m_logReady=false;
    PlcSnap7Client m_plc;
    QTimer m_timer,m_pulseTimer;
    QElapsedTimer m_lastRead,m_pollStart;
    QVector<Point> m_points;
    bool m_auto=false,m_healthy=false,m_writeEnabled=false,m_feedback=false;
    bool m_pulsing=false,m_highPossible=false,m_nextHigh=true;
    int m_intervalMs=200,m_periodMs=1000,m_highMs=100;
};
