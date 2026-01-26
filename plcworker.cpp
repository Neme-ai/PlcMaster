#include "plcworker.h"

PlcWorker::PlcWorker(QObject* parent) : QObject(parent) {
    m_timer.setParent(this);
    connect(&m_timer, &QTimer::timeout, this, &PlcWorker::pollOnce);
}


void PlcWorker::connectToPlc(const QString& ip) {
    if (m_plc.isConnected()) m_plc.disconnect();

    bool ok = m_plc.connectRackSlot(ip, 0, 1);
    if (!ok) ok = m_plc.connectTSAP(ip, 0x1000, 0x0200, 2);

    if (!ok) {
        emit connectedResult(false, m_plc.lastErrorText());
        emit statusText("Disconnected");
        return;
    }

    emit connectedResult(true, "");
    emit statusText("Connected");

    if (m_auto) QMetaObject::invokeMethod(this, "startPolling", Qt::QueuedConnection);

}

void PlcWorker::disconnectPlc() {
    QMetaObject::invokeMethod(this, "stopPolling", Qt::QueuedConnection);
    m_plc.disconnect();
    emit statusText("Disconnected");
}

void PlcWorker::setAutoRefresh(bool enabled, int intervalMs) {
    m_auto = enabled;
    m_intervalMs = intervalMs;

    if (!m_plc.isConnected()) return;
    if (m_auto) QMetaObject::invokeMethod(this, "startPolling", Qt::QueuedConnection);
    else QMetaObject::invokeMethod(this, "stopPolling", Qt::QueuedConnection);

}

void PlcWorker::setInterval(int intervalMs) {
    m_intervalMs = intervalMs;
    if (m_timer.isActive())
        QMetaObject::invokeMethod(this, "startPolling", Qt::QueuedConnection);

}

void PlcWorker::addPoint(int areaInt, int byteIndex, int bitIndex, const QString& text) {
    if (m_points.size() >= 16) return;

    Point p;
    p.area = (areaInt == 0) ? Area::I : (areaInt == 1) ? Area::Q : Area::M;
    p.byteIndex = byteIndex;
    p.bitIndex = bitIndex;
    p.text = text;
    m_points.push_back(p);
}

void PlcWorker::removePoint(int index) {
    if (index < 0 || index >= m_points.size()) return;
    m_points.removeAt(index);
}

void PlcWorker::clearPoints() {
    m_points.clear();
}

bool PlcWorker::parseAddr(const QString& in, Area& area, int& byteIndex, int& bitIndex, QString& normalized, QString& err) {
    QString s = in.trimmed();
    if (s.size() < 4) { err = "Address too short."; return false; }

    QChar t = s[0].toUpper();
    if (t != 'I' && t != 'Q' && t != 'M') { err = "Only I/Q/M supported."; return false; }

    int dot = s.indexOf('.');
    if (dot < 2) { err = "Format should be like I0.0 / Q1.3 / M10.7"; return false; }

    bool ok1=false, ok2=false;
    int b = s.mid(1, dot-1).toInt(&ok1);
    int bit = s.mid(dot+1).toInt(&ok2);
    if (!ok1 || !ok2) { err = "Byte/bit parse failed."; return false; }
    if (b < 0) { err = "Byte must be >=0."; return false; }
    if (bit < 0 || bit > 7) { err = "Bit must be 0..7."; return false; }

    if (t=='I') area = Area::I;
    else if (t=='Q') area = Area::Q;
    else area = Area::M;

    byteIndex = b;
    bitIndex = bit;
    normalized = QString("%1%2.%3").arg(t).arg(byteIndex).arg(bitIndex);
    return true;
}

void PlcWorker::pollOnce() {
    if (!m_plc.isConnected()) return;
    if (m_points.isEmpty()) return;

    int maxI=-1, maxQ=-1, maxM=-1;
    for (const auto& p : m_points) {
        if (p.area == Area::I) maxI = qMax(maxI, p.byteIndex);
        else if (p.area == Area::Q) maxQ = qMax(maxQ, p.byteIndex);
        else maxM = qMax(maxM, p.byteIndex);
    }

    QByteArray iBytes, qBytes, mBytes;
    if (maxI >= 0) m_plc.readI(0, maxI + 1, iBytes);
    if (maxQ >= 0) m_plc.readQ(0, maxQ + 1, qBytes);
    if (maxM >= 0) m_plc.readM(0, maxM + 1, mBytes);

    QVector<int> values;
    values.reserve(m_points.size());

    for (const auto& p : m_points) {
        int v = 0;
        const QByteArray* src = nullptr;

        if (p.area == Area::I) src = &iBytes;
        else if (p.area == Area::Q) src = &qBytes;
        else src = &mBytes;

        if (src && p.byteIndex >= 0 && p.byteIndex < src->size()) {
            unsigned char b = (unsigned char)(*src)[p.byteIndex];
            v = ((b >> p.bitIndex) & 1) ? 1 : 0;
        }
        values.push_back(v);
    }

    emit pointsUpdated(values);
}

void PlcWorker::readOne(const QString& addr) {
    if (!m_plc.isConnected()) { emit oneReadResult(false, 0, "Not connected."); return; }

    Area area; int byteIndex=0, bitIndex=0; QString norm, err;
    if (!parseAddr(addr, area, byteIndex, bitIndex, norm, err)) {
        emit oneReadResult(false, 0, err);
        return;
    }

    QByteArray buf;
    bool ok = false;
    if (area == Area::I) ok = m_plc.readI(byteIndex, 1, buf);
    else if (area == Area::Q) ok = m_plc.readQ(byteIndex, 1, buf);
    else ok = m_plc.readM(byteIndex, 1, buf);

    if (!ok || buf.size() < 1) {
        emit oneReadResult(false, 0, m_plc.lastErrorText());
        return;
    }

    unsigned char b = (unsigned char)buf[0];
    int v = ((b >> bitIndex) & 1) ? 1 : 0;
    emit oneReadResult(true, v, "");
}

void PlcWorker::writeOne(const QString& addr, int value) {
    if (!m_plc.isConnected()) { emit oneWriteResult(false, "Not connected."); return; }
    if (!(value == 0 || value == 1)) { emit oneWriteResult(false, "Value must be 0 or 1."); return; }

    Area area; int byteIndex=0, bitIndex=0; QString norm, err;
    if (!parseAddr(addr, area, byteIndex, bitIndex, norm, err)) {
        emit oneWriteResult(false, err);
        return;
    }

    // 写入只允许 Q/M
    if (area == Area::I) {
        emit oneWriteResult(false, "I area is read-only. Write only allowed for Q/M.");
        return;
    }

    bool ok = false;
    if (area == Area::M) ok = m_plc.writeMBit(byteIndex, bitIndex, value == 1);
    else ok = m_plc.writeQBit(byteIndex, bitIndex, value == 1);

    if (!ok) {
        emit oneWriteResult(false, m_plc.lastErrorText());
        return;
    }
    emit oneWriteResult(true, "");
}

void PlcWorker::startPolling() {//加两个私有 slot 专门启动/停止定时器
    if (!m_plc.isConnected()) return;
    m_timer.start(m_intervalMs);
}

void PlcWorker::stopPolling() {
    m_timer.stop();
}

