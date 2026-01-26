#pragma once
#include <QString>
#include <QByteArray>
#include "snap7.h"

class PlcSnap7Client {
public:
    PlcSnap7Client();
    ~PlcSnap7Client();

    bool connectRackSlot(const QString& ip, int rack = 0, int slot = 1);
    bool connectTSAP(const QString& ip, quint16 localTSAP, quint16 remoteTSAP, int connType /*1=PG 2=OP*/);
    void disconnect();
    bool isConnected() const;

    bool readM(int startByte, int sizeBytes, QByteArray& out);
    bool readI(int startByte, int sizeBytes, QByteArray& out);
    bool readQ(int startByte, int sizeBytes, QByteArray& out);

    bool writeM(int startByte, const QByteArray& in);

    bool writeMBit(int mByte, int bit, bool value);
    bool writeQBit(int qByte, int bit, bool value);

    QString lastErrorText() const;

private:
    S7Object m_cli;
    bool m_connected;
    int  m_lastErr;

};
