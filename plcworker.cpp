#include "plcworker.h"
#include <QDateTime>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
PlcWorker::PlcWorker(QObject* parent):QObject(parent) {
    QString dir=QCoreApplication::applicationDirPath()+"/logs"; QDir().mkpath(dir);
    m_logPath=dir+"/operations-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")+".log";
    QFile file(m_logPath); m_logReady=file.open(QIODevice::WriteOnly|QIODevice::Append); file.close();
    m_timer.setTimerType(Qt::PreciseTimer);
    m_timer.setParent(this); m_pulseTimer.setParent(this);
    m_pulseTimer.setSingleShot(true); m_pulseTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_timer,&QTimer::timeout,this,&PlcWorker::pollOnce);
    connect(&m_pulseTimer,&QTimer::timeout,this,&PlcWorker::pulseEdge);
}
void PlcWorker::log(const QString& message) {
    const QString line=QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")+" | "+message;
    QFile f(m_logPath); const QByteArray bytes=(line+"\n").toUtf8();
    m_logReady=f.open(QIODevice::WriteOnly|QIODevice::Append)&&f.write(bytes)==bytes.size()&&f.flush();
    emit actionLog(line);
    if(!m_logReady) { m_writeEnabled=false; m_feedback=false; emit writeModeReset(); emit actionLog(QStringLiteral("日志保存失败，写入权限关闭")); }
}
bool PlcWorker::fresh() const {
    return m_plc.isConnected()&&m_healthy&&m_lastRead.isValid()&&m_lastRead.elapsed()<=qMax(1000,m_intervalMs*3);
}
void PlcWorker::revoke(const QString& reason) {
    stopPulse(); m_writeEnabled=false; m_feedback=false; emit writeModeReset(); log(reason);
}
void PlcWorker::connectToPlc(const QString& ip) {
    m_timer.stop(); revoke(QStringLiteral("建立新连接，写入权限已清除")); m_healthy=false; m_lastRead.invalidate(); m_pollStart.invalidate(); m_plc.disconnect();
    emit faultUpdated(-1);
    bool ok=m_plc.connectRackSlot(ip,0,1);
    if(!ok) ok=m_plc.connectTSAP(ip,0x1000,0x0200,2);
    emit connectedResult(ok,ok?QString():m_plc.lastErrorText());
    if(ok) { log(QStringLiteral("已连接 ")+ip+QStringLiteral("；写入模式关闭")); pollOnce(); if(m_auto) m_timer.start(m_intervalMs); }
}
void PlcWorker::disconnectPlc() {
    revoke(QStringLiteral("停止连接，写入权限已清除")); m_timer.stop(); m_healthy=false; m_lastRead.invalidate(); m_pollStart.invalidate();
    m_plc.disconnect(); emit faultUpdated(-1); emit pointsUpdated(QVector<int>(m_points.size(),-1)); emit statusText("Disconnected");
}
void PlcWorker::setAutoRefresh(bool enabled,int intervalMs) {
    m_auto=enabled; setInterval(intervalMs);
    if(!enabled) { m_timer.stop(); revoke(QStringLiteral("监控暂停，写入权限已清除")); }
    else if(m_plc.isConnected()) m_timer.start(m_intervalMs);
}
void PlcWorker::setInterval(int intervalMs) {
    m_pollStart.invalidate();
    m_intervalMs=qBound(5,intervalMs,5000); if(m_timer.isActive()) m_timer.start(m_intervalMs);
}
void PlcWorker::addPoint(int areaInt,int byteIndex,int bitIndex,const QString& text) {
    if(areaInt==2&&m_points.size()<256) m_points.push_back({Area::M,byteIndex,bitIndex,text});
}
void PlcWorker::pollOnce() {
    if(!m_plc.isConnected()||m_points.isEmpty()) return;
    const double actualInterval=m_pollStart.isValid()?m_pollStart.nsecsElapsed()/1000000.0:-1.0;
    m_pollStart.restart();
    QElapsedTimer readClock; readClock.start();
    // Independent, read-only indicator; its value does not control writes or pulses.
    QByteArray fault;
    bool faultOk=m_plc.readM(5,1,fault);
    emit faultUpdated(faultOk ? (static_cast<unsigned char>(fault[0])&1) : -1);
    QByteArray bytes;
    if(!m_plc.readM(20,9,bytes)) {
        emit pollingTiming(actualInterval,readClock.nsecsElapsed()/1000000.0,false);
        emit faultUpdated(-1);
        m_healthy=false; emit pointsUpdated(QVector<int>(m_points.size(),-1));
        const QString error=m_plc.lastErrorText();
        if(m_writeEnabled||m_pulsing) revoke(QStringLiteral("读取失败，停止写入：")+error);
        emit statusText(QStringLiteral("读取失败：")+error); return;
    }
    emit pollingTiming(actualInterval,readClock.nsecsElapsed()/1000000.0,faultOk);
    m_healthy=true; m_lastRead.restart(); QVector<int> values;
    for(const auto& p:m_points) values.push_back(p.byteIndex>=20&&p.byteIndex<=28?((static_cast<unsigned char>(bytes[p.byteIndex-20])>>p.bitIndex)&1):-1);
    emit pointsUpdated(values);
}
void PlcWorker::setWriteMode(bool enabled,bool feedback) {
    if(!enabled) { revoke(QStringLiteral("手动关闭写入模式")); return; }
    if(!fresh()||!m_logReady) { emit writeModeReset(); log(QStringLiteral("拒绝启用：连接或监控数据无效")); return; }
    m_writeEnabled=true; m_feedback=feedback;
    log(feedback?QStringLiteral("写入模式开启，反馈位已解锁（可能被 PLC 覆盖）"):QStringLiteral("写入模式开启，仅控制位可写"));
}
bool PlcWorker::writeBit(int byte,int bit,int value,const QString& source) {
    log(QString("%1 请求 M%2.%3 = %4").arg(source).arg(byte).arg(bit).arg(value));
    if(!m_logReady) return false;
    QByteArray before,after;
    QString old=QStringLiteral("未知");
    if(m_plc.readM(byte,1,before)) old=QString::number((static_cast<unsigned char>(before[0])>>bit)&1);
    else { log(QString("%1 M%2.%3 原值=未知 目标=%4 未发送：预读失败 %5").arg(source).arg(byte).arg(bit).arg(value).arg(m_plc.lastErrorText())); return false; }
    const bool sent=m_plc.writeMBit(byte,bit,value!=0);
    const QString error=sent?QString():m_plc.lastErrorText();
    int back=-1;
    if(m_plc.readM(byte,1,after)) back=(static_cast<unsigned char>(after[0])>>bit)&1;
    log(QString("%1 M%2.%3 原值=%4 目标=%5 写入=%6 读回=%7 %8")
        .arg(source).arg(byte).arg(bit).arg(old).arg(value).arg(sent?QStringLiteral("成功"):QStringLiteral("失败"))
        .arg(back<0?QStringLiteral("未知"):QString::number(back)).arg(error));
    return sent&&back==value&&m_logReady;
}
void PlcWorker::writeOne(const QString& address,int value) {
    auto reject=[this](const QString& text){ log(text); emit oneWriteResult(false,text); };
    if(!m_writeEnabled||!fresh()) { reject(QStringLiteral("拒绝写入：请检查写入模式及有效连接")); return; }
    const auto match=QRegularExpression("^M(\\d+)\\.([0-7])$").match(address);
    if(!match.hasMatch()||(value!=0&&value!=1)) { reject(QStringLiteral("无效地址或值")); return; }
    const int byte=match.captured(1).toInt(),bit=match.captured(2).toInt(); bool listed=false;
    if(m_pulsing&&byte==22&&bit==1) { reject(QStringLiteral("FeedingDog 正由脉冲控制，请先停止脉冲再手动写入")); return; }
    if(byte==5&&bit==0) { reject(QStringLiteral("M5.0 为固定只读故障标志，禁止写入")); return; }
    for(const auto& p:m_points) listed=listed||(p.byteIndex==byte&&p.bitIndex==bit);
    const bool control=(byte==20||byte==21||(byte==22&&bit<=3));
    if(!listed||(!control&&!m_feedback)) { reject(QStringLiteral("拒绝写入：点位未预置或反馈位未解锁")); return; }
    bool ok=writeBit(byte,bit,value,QStringLiteral("手动"));
    if(!ok) revoke(QStringLiteral("写入或读回不一致，已关闭写入模式；未自动重试"));
    emit oneWriteResult(ok,ok?QStringLiteral("写入成功且读回一致"):QStringLiteral("写入未确认，请查看日志；PLC 可能覆盖该位")); pollOnce();
}
void PlcWorker::startPulse(int periodMs,int highMs) {
    if(m_pulsing) return;
    if(!m_writeEnabled||!fresh()||highMs<1||periodMs<5||periodMs>600000||highMs>=periodMs) {
        emit pulseState(false,QStringLiteral("无法启动：需有效连接及写入模式，高/低电平各至少 1 ms，周期为 5–600000 ms")); return;
    }
    m_periodMs=periodMs; m_highMs=highMs;
    // Establish a known low state before the first rising edge.
    if(!writeBit(22,1,0,QStringLiteral("脉冲初始化"))) { revoke(QStringLiteral("脉冲初始化失败")); return; }
    m_pulsing=true; m_highPossible=false; m_nextHigh=true;
    log(QStringLiteral("启动 FeedingDog：周期=%1 ms，高电平=%2 ms；不会改写 EnableDog").arg(periodMs).arg(highMs));
    emit pulseState(true,QStringLiteral("脉冲运行中")); m_pulseTimer.start(m_periodMs-m_highMs);
}
void PlcWorker::pulseEdge() {
    if(!m_pulsing) return;
    if(!m_writeEnabled||!fresh()) { revoke(QStringLiteral("脉冲停止：连接或数据失效")); return; }
    QElapsedTimer cost; cost.start(); const bool high=m_nextHigh;
    if(high) m_highPossible=true; // A failed acknowledgement can still mean the PLC received 1.
    if(!writeBit(22,1,high?1:0,QStringLiteral("喂狗脉冲"))) { revoke(QStringLiteral("脉冲写入或读回失败，停止且不重发")); return; }
    if(!high) m_highPossible=false;
    m_nextHigh=!high; int duration=high?m_highMs:m_periodMs-m_highMs;
    if(cost.elapsed()>=duration) { revoke(QStringLiteral("通信耗时超过设定电平时长，脉冲已停止")); return; }
    m_pulseTimer.start(duration-static_cast<int>(cost.elapsed()));
}
void PlcWorker::stopPulse() {
    const bool wasRunning=m_pulsing; m_pulseTimer.stop(); m_pulsing=false;
    if(m_highPossible) {
        // One best-effort low write even when the pre-read/transport is failing.
        bool sent=m_plc.writeMBit(22,1,false); QByteArray back;
        bool confirmed=sent&&m_plc.readM(22,1,back)&&!(static_cast<unsigned char>(back[0])&2);
        log(confirmed?QStringLiteral("停止脉冲：M22.1 写 0 且读回确认"):QStringLiteral("停止脉冲：M22.1 复位为 0 未确认，请现场检查；不会在重连后补发"));
        m_highPossible=false;
        emit pulseState(false,confirmed?QStringLiteral("已停止，读回 0"):QStringLiteral("已停止发送，但 M22.1 状态未确认，请现场检查"));
    } else emit pulseState(false,wasRunning?QStringLiteral("脉冲已停止（最近确认低电平）"):QStringLiteral("脉冲未运行"));
}
