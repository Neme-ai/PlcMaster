#include "mainwindow.h"
#include "ui_mainwindow.h"
#include <QMessageBox>
#include <QMetaObject>
#include <QStyle>
#include <QApplication>

#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonParseError>
#include <QCoreApplication>
#include <QDir>
#include <QDebug>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
    ui->setupUi(this);

    ui->btnConnect->setProperty("btnType","primary");
    ui->btnReadOne->setProperty("btnType","primary");

    ui->btnWriteOne->setProperty("btnType","secondary");
    ui->btnAdd->setProperty("btnType","secondary");
    ui->btnRemove->setProperty("btnType","danger");



    // 初始化 16 个 tile
    initTiles();
    loadPointConfig();  // 自动从 exe 同级 points.json 恢复


    // Worker 线程
    m_thread = new QThread(this);
    m_worker = new PlcWorker();
    m_worker->moveToThread(m_thread);
    m_thread->start();
    syncPointsToWorker();

    // UI signals
    connect(ui->btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(ui->chkAuto, &QCheckBox::stateChanged, this, &MainWindow::onAutoChanged);
    connect(ui->spinInterval, qOverload<int>(&QSpinBox::valueChanged), this, &MainWindow::onIntervalChanged);

    connect(ui->btnAdd, &QPushButton::clicked, this, &MainWindow::onAddClicked);
    connect(ui->btnRemove, &QPushButton::clicked, this, &MainWindow::onRemoveClicked);

    connect(ui->btnReadOne, &QPushButton::clicked, this, &MainWindow::onReadOneClicked);
    connect(ui->btnWriteOne, &QPushButton::clicked, this, &MainWindow::onWriteOneClicked);

    // Worker -> UI
    connect(m_worker, &PlcWorker::connectedResult, this, &MainWindow::onWorkerConnected);
    connect(m_worker, &PlcWorker::statusText, this, &MainWindow::onStatusText);
    connect(m_worker, &PlcWorker::pointsUpdated, this, &MainWindow::onPointsUpdated);
    connect(m_worker, &PlcWorker::oneReadResult, this, &MainWindow::onOneReadResult);
    connect(m_worker, &PlcWorker::oneWriteResult, this, &MainWindow::onOneWriteResult);

    ui->lblStatus->setText("Disconnected");
    ui->lblRwResult->setText("-");
    setUiConnected(false);
}

MainWindow::~MainWindow()
{
    // 1) 先要求 worker 停止定时器 + 断开PLC（用 queued，确保在worker线程执行）
    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "setAutoRefresh", Qt::QueuedConnection,
                                  Q_ARG(bool, false), Q_ARG(int, ui->spinInterval->value()));
        QMetaObject::invokeMethod(m_worker, "disconnectPlc", Qt::QueuedConnection);
    }

    // 2) 让 worker 在所属线程里销毁
    if (m_worker) {
        m_worker->deleteLater();
        m_worker = nullptr;
    }

    // 3) 退出线程并等待真正结束
    if (m_thread) {
        m_thread->quit();
        m_thread->wait();   // 这里不要给500ms，直接等，关闭时就等一下是正常的
        m_thread = nullptr;
    }

    delete ui;
}

void MainWindow::setUiConnected(bool connected) {
    ui->btnConnect->setText(connected ? "Disconnect" : "Connect");
}
QString MainWindow::configFilePath() const
{
    // points.json 放在 exe 同级目录
    return QCoreApplication::applicationDirPath() + "/points.json";
}

void MainWindow::syncPointsToWorker()
{
    if (!m_worker) return;

    // 先清空 worker 侧点位，再按 m_points 顺序重新灌入
    QMetaObject::invokeMethod(m_worker, "clearPoints", Qt::QueuedConnection);

    for (const auto& p : m_points) {
        int areaInt = (p.area==Area::I) ? 0 : (p.area==Area::Q) ? 1 : 2;
        QMetaObject::invokeMethod(m_worker, "addPoint", Qt::QueuedConnection,
                                  Q_ARG(int, areaInt),
                                  Q_ARG(int, p.byteIndex),
                                  Q_ARG(int, p.bitIndex),
                                  Q_ARG(QString, p.text));
    }
}

bool MainWindow::savePointConfig(QString* errOut) const
{
    QJsonObject root;
    root["version"] = 1;

    QJsonArray arr;
    for (const auto& p : m_points) {
        arr.append(p.text); // 规范化后的 "I0.0"/"Q1.3"/"M10.7"
    }
    root["points"] = arr;

    const QString path = configFilePath();

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errOut) *errOut = "Open config for write failed: " + path;
        return false;
    }

    QJsonDocument doc(root);
    const QByteArray data = doc.toJson(QJsonDocument::Indented);

    if (f.write(data) != data.size()) {
        if (errOut) *errOut = "Write config failed: " + path;
        return false;
    }

    if (!f.commit()) {
        if (errOut) *errOut = "Commit config failed: " + path;
        return false;
    }

#ifdef QT_DEBUG
    qDebug() << "[Config] Saved:" << path;
#endif
    return true;
}

bool MainWindow::loadPointConfig(QString* errOut)
{
    const QString path = configFilePath();
    QFile f(path);

    if (!f.exists()) {
#ifdef QT_DEBUG
        qDebug() << "[Config] Not found, skip:" << path;
#endif
        return true; // 第一次运行没有配置很正常
    }

    if (!f.open(QIODevice::ReadOnly)) {
        if (errOut) *errOut = "Open config for read failed: " + path;
        return false;
    }

    QJsonParseError pe;
    QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (errOut) *errOut = "Config JSON parse failed: " + pe.errorString();
        return false;
    }

    QJsonObject root = doc.object();
    QJsonArray arr = root.value("points").toArray();

    QVector<Point> loaded;
    loaded.reserve(16);

    // 去重（大小写不敏感）
    QSet<QString> seen;

    for (const auto& v : arr) {
        if (loaded.size() >= 16) break;

        QString raw = v.toString().trimmed();
        if (raw.isEmpty()) continue;

        Area area; int byteIndex=0, bitIndex=0; QString norm, perr;
        if (!parseAddrLocal(raw, area, byteIndex, bitIndex, norm, perr)) {
#ifdef QT_DEBUG
            qDebug() << "[Config] Skip invalid point:" << raw << "reason:" << perr;
#endif
            continue;
        }

        QString key = norm.toUpper();
        if (seen.contains(key)) continue;
        seen.insert(key);

        Point p; p.area=area; p.byteIndex=byteIndex; p.bitIndex=bitIndex; p.text=norm;
        loaded.push_back(p);
    }

    m_points = loaded;
    refreshTiles();       // 先恢复UI
    syncPointsToWorker(); // 再同步到 worker

#ifdef QT_DEBUG
    qDebug() << "[Config] Loaded count:" << m_points.size() << "from" << path;
#endif
    return true;
}

void MainWindow::initTiles() {
    // pointsContainer 内做 4x4 网格
    m_pointsGrid = new QGridLayout(ui->pointsContainer);
    m_pointsGrid->setContentsMargins(10, 10, 10, 10);
    m_pointsGrid->setHorizontalSpacing(12);
    m_pointsGrid->setVerticalSpacing(12);

    for (int r=0; r<4; ++r) m_pointsGrid->setRowStretch(r, 1);
    for (int c=0; c<4; ++c) m_pointsGrid->setColumnStretch(c, 1);

    m_tiles.resize(16);
    for (int i=0; i<16; ++i) {
        int r = i / 4;
        int c = i % 4;

        QFrame* frame = new QFrame(ui->pointsContainer);
        frame->setFrameShape(QFrame::StyledPanel);
        frame->setMinimumHeight(70);

        QVBoxLayout* v = new QVBoxLayout(frame);
        v->setContentsMargins(8, 6, 8, 6);

        QLabel* addr = new QLabel("-", frame);
        QLabel* val = new QLabel("-", frame);

        QFont f = val->font();
        f.setPointSize(20);
        f.setBold(true);
        val->setFont(f);

        val->setStyleSheet("color: gray;");
        v->addWidget(addr);
        v->addWidget(val);

        frame->setProperty("tile","point");
        frame->setProperty("state","na");

        addr->setProperty("role","addr");
        val->setProperty("role","val");
        val->setProperty("v","na");

        m_pointsGrid->addWidget(frame, r, c);
        m_tiles[i] = {frame, addr, val};
    }

    ui->pointsContainer->setLayout(m_pointsGrid);
}

void MainWindow::refreshTiles() {
    for (int i=0; i<16; ++i) {
        if (i < m_points.size()) {
            m_tiles[i].lblAddr->setText(m_points[i].text);
            setTileValue(i, -1);
            m_tiles[i].frame->show();
        } else {
            m_tiles[i].lblAddr->setText("-");
            m_tiles[i].lblVal->setText("-");
            m_tiles[i].lblVal->setStyleSheet("color: gray;");
            m_tiles[i].frame->show(); // 也可以 hide()，看你喜欢
        }
    }
}

void MainWindow::setTileValue(int idx, int v)
{
    if (idx < 0 || idx >= 16) return;

    auto* frame = m_tiles[idx].frame;
    auto* val   = m_tiles[idx].lblVal;

    if (v == 1) {
        val->setText("1");
        val->setProperty("v","1");
        frame->setProperty("state","on");
    } else if (v == 0) {
        val->setText("0");
        val->setProperty("v","0");
        frame->setProperty("state","off");
    } else {
        val->setText("-");
        val->setProperty("v","na");
        frame->setProperty("state","na");
    }

    // 让QSS立即生效
    frame->style()->unpolish(frame); frame->style()->polish(frame); frame->update();
    val->style()->unpolish(val);     val->style()->polish(val);     val->update();
}


bool MainWindow::parseAddrLocal(const QString& in, Area& area, int& byteIndex, int& bitIndex, QString& normalized, QString& err) {
    QString s = in.trimmed();
    if (s.size() < 4) { err="Address too short."; return false; }
    QChar t = s[0].toUpper();
    if (t!='I' && t!='Q' && t!='M') { err="Only I/Q/M supported."; return false; }
    int dot = s.indexOf('.');
    if (dot < 2) { err="Format like I0.0 / Q1.3 / M10.7"; return false; }

    bool ok1=false, ok2=false;
    int b = s.mid(1, dot-1).toInt(&ok1);
    int bit = s.mid(dot+1).toInt(&ok2);
    if (!ok1 || !ok2) { err="Byte/bit parse failed."; return false; }
    if (b < 0) { err="Byte must be >=0"; return false; }
    if (bit < 0 || bit > 7) { err="Bit must be 0..7"; return false; }

    if (t=='I') area = Area::I;
    else if (t=='Q') area = Area::Q;
    else area = Area::M;

    byteIndex = b;
    bitIndex = bit;
    normalized = QString("%1%2.%3").arg(t).arg(byteIndex).arg(bitIndex);
    return true;
}

void MainWindow::onConnectClicked() {
    QString ip = ui->lineEditIp->text().trimmed();
    if (ip.isEmpty()) { QMessageBox::warning(this, "Hint", "Please input PLC IP."); return; }

    if (ui->btnConnect->text() == "Connect") {
        QMetaObject::invokeMethod(m_worker, "connectToPlc", Qt::QueuedConnection, Q_ARG(QString, ip));
    } else {
        QMetaObject::invokeMethod(m_worker, "disconnectPlc", Qt::QueuedConnection);
        setUiConnected(false);
        ui->lblStatus->setText("Disconnected");
        ui->lblStatus->setProperty("st","idle");
        ui->lblStatus->style()->unpolish(ui->lblStatus);
        ui->lblStatus->style()->polish(ui->lblStatus);

    }
}

void MainWindow::onAutoChanged(int state) {
    bool enabled = (state == Qt::Checked);
    int interval = ui->spinInterval->value();
    QMetaObject::invokeMethod(m_worker, "setAutoRefresh", Qt::QueuedConnection,
                              Q_ARG(bool, enabled), Q_ARG(int, interval));
}

void MainWindow::onIntervalChanged(int v) {
    QMetaObject::invokeMethod(m_worker, "setInterval", Qt::QueuedConnection, Q_ARG(int, v));
    if (ui->chkAuto->isChecked()) {
        QMetaObject::invokeMethod(m_worker, "setAutoRefresh", Qt::QueuedConnection,
                                  Q_ARG(bool, true), Q_ARG(int, v));
    }
}

void MainWindow::onAddClicked() {
    if (m_points.size() >= 16) {
        QMessageBox::warning(this, "Hint", "Monitor points full (max 16). Remove one first.");
        return;
    }

    Area area; int byteIndex=0, bitIndex=0; QString norm, err;
    if (!parseAddrLocal(ui->lineEditAddr->text(), area, byteIndex, bitIndex, norm, err)) {
        QMessageBox::warning(this, "Hint", err);
        return;
    }

    // 去重
    for (const auto& p : m_points) {
        if (p.text.compare(norm, Qt::CaseInsensitive) == 0) {
            QMessageBox::information(this, "Hint", "Already exists.");
            return;
        }
    }

    Point p; p.area=area; p.byteIndex=byteIndex; p.bitIndex=bitIndex; p.text=norm;
    m_points.push_back(p);
    refreshTiles();
    QString saveErr;
    if (!savePointConfig(&saveErr)) {
        QMessageBox::warning(this, "Save config failed", saveErr);
    }

    int areaInt = (area==Area::I) ? 0 : (area==Area::Q) ? 1 : 2;
    QMetaObject::invokeMethod(m_worker, "addPoint", Qt::QueuedConnection,
                              Q_ARG(int, areaInt), Q_ARG(int, byteIndex), Q_ARG(int, bitIndex), Q_ARG(QString, norm));
}

void MainWindow::onRemoveClicked() {
    Area area; int byteIndex=0, bitIndex=0; QString norm, err;
    if (!parseAddrLocal(ui->lineEditAddr->text(), area, byteIndex, bitIndex, norm, err)) {
        QMessageBox::warning(this, "Hint", "Input address to remove, e.g. I0.0");
        return;
    }

    int idx = -1;
    for (int i=0; i<m_points.size(); ++i) {
        if (m_points[i].text.compare(norm, Qt::CaseInsensitive) == 0) { idx = i; break; }
    }
    if (idx < 0) {
        QMessageBox::information(this, "Hint", "Not found in monitor list.");
        return;
    }

    m_points.removeAt(idx);
    refreshTiles();
    QString saveErr;
    if (!savePointConfig(&saveErr)) {
        QMessageBox::warning(this, "Save config failed", saveErr);
    }

    QMetaObject::invokeMethod(m_worker, "removePoint", Qt::QueuedConnection, Q_ARG(int, idx));
}

void MainWindow::onReadOneClicked() {
    QString addr = ui->lineEditRwAddr->text().trimmed();
    if (addr.isEmpty()) { QMessageBox::warning(this, "Hint", "Please input address."); return; }

    ui->lblRwResult->setText("Reading...");
    QMetaObject::invokeMethod(m_worker, "readOne", Qt::QueuedConnection, Q_ARG(QString, addr));
}

void MainWindow::onWriteOneClicked() {
    QString addr = ui->lineEditRwAddr->text().trimmed();
    QString vStr = ui->lineEditWriteValue->text().trimmed();
    if (addr.isEmpty()) { QMessageBox::warning(this, "Hint", "Please input address."); return; }
    if (vStr.isEmpty()) { QMessageBox::warning(this, "Hint", "Please input write value 0/1."); return; }

    bool ok=false;
    int v = vStr.toInt(&ok);
    if (!ok || !(v==0 || v==1)) {
        QMessageBox::warning(this, "Hint", "Write value must be 0 or 1.");
        return;
    }

    ui->lblRwResult->setText("Writing...");
    QMetaObject::invokeMethod(m_worker, "writeOne", Qt::QueuedConnection, Q_ARG(QString, addr), Q_ARG(int, v));
}

void MainWindow::onWorkerConnected(bool ok, const QString& err) {
    if (!ok) {
        ui->lblStatus->setText("Disconnected");
        setUiConnected(false);
        QMessageBox::critical(this, "Connect failed", err);
        return;
    }

    ui->lblStatus->setText("Connected");
    ui->lblStatus->setProperty("st","ok");
    ui->lblStatus->style()->unpolish(ui->lblStatus);
    ui->lblStatus->style()->polish(ui->lblStatus);

    setUiConnected(true);

    // Auto refresh if checked
    if (ui->chkAuto->isChecked()) {
        QMetaObject::invokeMethod(m_worker, "setAutoRefresh", Qt::QueuedConnection,
                                  Q_ARG(bool, true), Q_ARG(int, ui->spinInterval->value()));
    }
}

void MainWindow::onStatusText(const QString& s) {
    ui->lblStatus->setText(s);
}

void MainWindow::onPointsUpdated(const QVector<int>& values) {
    for (int i=0; i<16; ++i) {
        if (i < m_points.size() && i < values.size()) setTileValue(i, values[i]);
        else setTileValue(i, -1);
    }
}

void MainWindow::onOneReadResult(bool ok, int value, const QString& err) {
    if (!ok) {
        ui->lblRwResult->setText("Read failed: " + err);
        return;
    }
    ui->lblRwResult->setText(QString("Read OK: %1").arg(value));
    ui->lineEditWriteValue->setText(QString::number(value)); // 读到的值顺便填入写入框，方便“读后改”
}

void MainWindow::onOneWriteResult(bool ok, const QString& err) {
    if (!ok) {
        ui->lblRwResult->setText("Write failed: " + err);
        return;
    }
    ui->lblRwResult->setText("Write OK");
}
#include <QCloseEvent>

void MainWindow::closeEvent(QCloseEvent* e)
{
    savePointConfig();
    // 关闭时先禁止UI触发新的调用
    ui->chkAuto->blockSignals(true);
    ui->spinInterval->blockSignals(true);

    if (m_worker) {
        QMetaObject::invokeMethod(m_worker, "setAutoRefresh", Qt::QueuedConnection,
                                  Q_ARG(bool, false), Q_ARG(int, ui->spinInterval->value()));
        QMetaObject::invokeMethod(m_worker, "disconnectPlc", Qt::QueuedConnection);
    }

    QMainWindow::closeEvent(e);
}
