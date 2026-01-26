#include "plcsnap7client.h"

PlcSnap7Client::PlcSnap7Client()
    : m_cli(0)
    , m_connected(false)
    , m_lastErr(0)
{
    m_cli = Cli_Create();
}

PlcSnap7Client::~PlcSnap7Client() {
    disconnect();
    if (m_cli) Cli_Destroy(&m_cli);
    m_cli = 0;
}

bool PlcSnap7Client::connectRackSlot(const QString& ip, int rack, int slot) {
    m_lastErr = Cli_ConnectTo(m_cli, ip.toLocal8Bit().constData(), rack, slot);
    m_connected = (m_lastErr == 0);
    return m_connected;
}

bool PlcSnap7Client::connectTSAP(const QString& ip, quint16 localTSAP, quint16 remoteTSAP, int connType) {
    m_lastErr = Cli_SetConnectionType(m_cli, connType);
    if (m_lastErr != 0) { m_connected = false; return false; }
    m_lastErr = Cli_SetConnectionParams(m_cli, ip.toLocal8Bit().constData(), localTSAP, remoteTSAP);
    if (m_lastErr != 0) { m_connected = false; return false; }
    m_lastErr = Cli_Connect(m_cli);
    m_connected = (m_lastErr == 0);
    return m_connected;
}

void PlcSnap7Client::disconnect() {
    if (m_cli && m_connected) Cli_Disconnect(m_cli);
    m_connected = false;
}

bool PlcSnap7Client::isConnected() const { return m_connected; }

bool PlcSnap7Client::readM(int startByte, int sizeBytes, QByteArray& out) {
    if (!m_connected) return false;
    out.resize(sizeBytes);
    m_lastErr = Cli_MBRead(m_cli, startByte, sizeBytes, out.data());
    return (m_lastErr == 0);
}

bool PlcSnap7Client::readI(int startByte, int sizeBytes, QByteArray& out) {
    if (!m_connected) return false;
    out.resize(sizeBytes);
    m_lastErr = Cli_EBRead(m_cli, startByte, sizeBytes, out.data());
    return (m_lastErr == 0);
}

bool PlcSnap7Client::readQ(int startByte, int sizeBytes, QByteArray& out) {
    if (!m_connected) return false;
    out.resize(sizeBytes);
    m_lastErr = Cli_ABRead(m_cli, startByte, sizeBytes, out.data());
    return (m_lastErr == 0);
}

bool PlcSnap7Client::writeM(int startByte, const QByteArray& in) {
    if (!m_connected) return false;
    m_lastErr = Cli_MBWrite(m_cli, startByte, in.size(), (void*)in.constData());
    return (m_lastErr == 0);
}

bool PlcSnap7Client::writeMBit(int mByte, int bit, bool value) {
    if (!m_connected) return false;
    if (bit < 0 || bit > 7) return false;

    unsigned char b = 0;
    m_lastErr = Cli_MBRead(m_cli, mByte, 1, &b);
    if (m_lastErr != 0) return false;

    unsigned char mask = (unsigned char)(1u << bit);
    if (value) b |= mask;
    else b &= (unsigned char)(~mask);

    m_lastErr = Cli_MBWrite(m_cli, mByte, 1, &b);
    return (m_lastErr == 0);
}

bool PlcSnap7Client::writeQBit(int qByte, int bit, bool value) {
    if (!m_connected) return false;
    if (bit < 0 || bit > 7) return false;

    unsigned char b = 0;
    m_lastErr = Cli_ABRead(m_cli, qByte, 1, &b);
    if (m_lastErr != 0) return false;

    unsigned char mask = (unsigned char)(1u << bit);
    if (value) b |= mask;
    else b &= (unsigned char)(~mask);

    m_lastErr = Cli_ABWrite(m_cli, qByte, 1, &b);
    return (m_lastErr == 0);
}

QString PlcSnap7Client::lastErrorText() const {
    char buf[256] = {0};
    Cli_ErrorText(m_lastErr, buf, 256);
    return QString::fromLocal8Bit(buf);
}
