#include "mainwindow.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QSpinBox>
#include <QFile>
#include <QDateTime>
#include <QApplication>
#include <QTimer>
#include <QDebug>
#include <QDir>
MainWindow::MainWindow(QWidget *parent):QMainWindow(parent),worker(new PlcWorker) {
    setWindowTitle(QStringLiteral("PLCmaster · 47 点监控与手动写入")); resize(1400,1000);
    auto root=new QWidget(this); setCentralWidget(root); auto layout=new QVBoxLayout(root);
    auto title=new QLabel(QStringLiteral("转台与导轨 · PLC 实时监控"));
    title->setStyleSheet("font-size:22px;font-weight:600;"); layout->addWidget(title);
    layout->addWidget(new QLabel(QStringLiteral("47 个预置点位 ｜ 写入默认关闭 ｜ 0/1 为原始位值，含义以 PLC 程序为准")));
    auto bar=new QHBoxLayout; layout->addLayout(bar);
    ip=new QLineEdit("192.168.3.2"); ip->setMaximumWidth(160); bar->addWidget(new QLabel("PLC IP")); bar->addWidget(ip);
    button=new QPushButton(QStringLiteral("连接")); bar->addWidget(button);
    auto interval=new QSpinBox; interval->setRange(5,5000); interval->setValue(200); interval->setSuffix(" ms");
    bar->addWidget(new QLabel(QStringLiteral("刷新周期"))); bar->addWidget(interval);
    status=new QLabel(QStringLiteral("未连接")); status->setMinimumWidth(420); status->setWordWrap(true); bar->addWidget(status); bar->addStretch();
    faultIndicator=new QLabel; faultIndicator->setMinimumWidth(220); bar->addWidget(faultIndicator);
    setFaultValue(-1);
    connect(worker,&PlcWorker::faultUpdated,this,[this](int value){setFaultValue(value);});
    auto filters=new QHBoxLayout; layout->addLayout(filters);
    group=new QComboBox; group->addItem(QStringLiteral("全部分组")); filters->addWidget(group);
    search=new QLineEdit; search->setPlaceholderText(QStringLiteral("搜索变量名、地址或中文说明")); filters->addWidget(search);
    table=new QTableWidget(0,6); table->setHorizontalHeaderLabels({QStringLiteral("分组"),QStringLiteral("变量名"),QStringLiteral("地址"),QStringLiteral("状态"),QStringLiteral("中文说明"),QStringLiteral("信号方向")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers); table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setAlternatingRowColors(true); table->verticalHeader()->hide(); table->setSortingEnabled(false);
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(4,QHeaderView::Stretch); layout->addWidget(table);
    updated=new QLabel(QStringLiteral("尚未取得有效数据；短脉冲可能短于刷新周期，无法保证捕获。")); layout->addWidget(updated);
    timingLabel=new QLabel(QStringLiteral("实际刷新间隔：— ｜ 读取耗时：—")); layout->addWidget(timingLabel);
    timingLabel->setToolTip(QStringLiteral("实际刷新间隔：通信线程相邻两轮开始时间之差；读取耗时：本轮 M5.0 与 M20–M28 两次读取总耗时。首轮间隔为空，包含网络等待，不含界面绘制。"));
    connect(worker,&PlcWorker::pollingTiming,this,[this](double intervalMs,double readMs,bool success){
        timingLabel->setText(QStringLiteral("实际刷新间隔：%1 ms ｜ 读取耗时：%2 ms%3")
            .arg(intervalMs<0?QStringLiteral("—"):QString::number(intervalMs,'f',2))
            .arg(readMs,0,'f',2).arg(success?QString():QStringLiteral(" ｜ 本轮读取失败")));
    });
    setupWriteControls(layout);
    worker->moveToThread(&thread); connect(&thread,&QThread::finished,worker,&QObject::deleteLater); thread.start();
    QFile points(":/points.txt"); points.open(QIODevice::ReadOnly);
    const auto lines=QString::fromUtf8(points.readAll()).remove(QChar(0xfeff)).split('\n',QString::SkipEmptyParts);
    for(const auto &line:lines) {
        const auto p=line.trimmed().split('|'); if(p.size()!=4) continue;
        int row=table->rowCount(); table->insertRow(row);
        QString direction=row<20?QStringLiteral("转台程序 → PLC"):QStringLiteral("PLC → 转台程序");
        QStringList cells={p[3],p[0],p[1],QStringLiteral("未知"),p[2],direction};
        for(int c=0;c<6;++c) { auto item=new QTableWidgetItem(cells[c]); item->setToolTip(cells[c]); table->setItem(row,c,item); }
        if(group->findText(p[3])<0) group->addItem(p[3]);
        const auto address=p[1].mid(1).split('.');
        QMetaObject::invokeMethod(worker,"addPoint",Qt::QueuedConnection,Q_ARG(int,2),Q_ARG(int,address[0].toInt()),Q_ARG(int,address[1].toInt()),Q_ARG(QString,p[0]));
    }
    connect(search,&QLineEdit::textChanged,this,[this]{filter();});
    connect(group,qOverload<int>(&QComboBox::currentIndexChanged),this,[this]{filter();});
    connect(interval,qOverload<int>(&QSpinBox::valueChanged),this,[this](int n){monitorInterval=n;QMetaObject::invokeMethod(worker,"setInterval",Qt::QueuedConnection,Q_ARG(int,n));});
    connect(button,&QPushButton::clicked,this,[this,interval]{
        button->setEnabled(false); invalidate();
        if(connected) QMetaObject::invokeMethod(worker,"disconnectPlc",Qt::QueuedConnection);
        else {
            status->setText(QStringLiteral("正在连接…"));
            QMetaObject::invokeMethod(worker,"setAutoRefresh",Qt::QueuedConnection,Q_ARG(bool,true),Q_ARG(int,interval->value()));
            QMetaObject::invokeMethod(worker,"connectToPlc",Qt::QueuedConnection,Q_ARG(QString,ip->text().trimmed()));
        }
    });
    connect(worker,&PlcWorker::connectedResult,this,[this](bool ok,const QString &err){
        connected=ok; button->setEnabled(true); ip->setEnabled(!ok); button->setText(ok?QStringLiteral("断开"):QStringLiteral("连接"));
        status->setText(ok?QStringLiteral("已连接"):QStringLiteral("连接失败：")+err);
        if(!ok) invalidate();
        updateWriteControls();
    });
    connect(worker,&PlcWorker::statusText,this,[this](const QString &s){
        if(s=="Disconnected") { connected=false; button->setEnabled(true); button->setText(QStringLiteral("连接")); ip->setEnabled(true); invalidate(); status->setText(QStringLiteral("已断开 / 连接未建立")); }
        else if(s!="Connected") status->setText(s);
    });
    connect(worker,&PlcWorker::pointsUpdated,this,[this](const QVector<int>& values){
        bool valid=values.size()==table->rowCount();
        for(int r=0;r<table->rowCount();++r) {
            int v=r<values.size()?values[r]:-1; valid=valid&&v>=0;
            auto item=table->item(r,3); item->setText(v<0?QStringLiteral("失效"):QString::number(v));
            item->setForeground(v<0?QColor("#777777"):v?QColor("#1769aa"):QColor("#333333"));
        }
        updated->setText((valid?QStringLiteral("最近有效读取："):QStringLiteral("读取失败，数据失效："))+QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")+QStringLiteral(" ｜ 短脉冲可能漏采"));
        dataValid=valid; if(valid) dataAge.restart(); updateWriteControls();
        if(valid) status->setText(QStringLiteral("已连接 · 47 点读取正常"));
        static int samples=0;
        if(qApp->arguments().contains("--smoke-test") && (!valid || ++samples>=10)) {
            QFile out(QCoreApplication::applicationDirPath()+"/smoke-result.txt"); out.open(QIODevice::WriteOnly);
            out.write(valid?"PASS: 47 points read successfully\n":"FAIL: invalid read\n");
            out.write((timingLabel->text()+"\n").toUtf8());
            out.write(("M5.0="+QString::number(faultValue)+"\n").toUtf8());
            for(int r=0;r<table->rowCount();++r) out.write((table->item(r,1)->text()+" "+table->item(r,2)->text()+"="+table->item(r,3)->text()+"\n").toUtf8()); out.close();
            grab().save(QCoreApplication::applicationDirPath()+"/monitor-preview.png");
            QTimer::singleShot(0,qApp,[valid]{qApp->exit(valid?0:2);});
        }
    });
    if(qApp->arguments().contains("--ui-test")) QTimer::singleShot(100,this,[this]{
        bool ok=table->rowCount()==47;
        worker->faultUpdated(1); ok=ok&&faultIndicator->text().contains(QStringLiteral("1 · 故障"));
        search->setText("no-match"); ok=ok&&faultIndicator->isVisible(); search->clear();
        worker->faultUpdated(0); ok=ok&&faultIndicator->text().contains(QStringLiteral("0 · 无故障"));
        worker->faultUpdated(-1); ok=ok&&faultIndicator->text().contains(QStringLiteral("状态未知"));
        connected=true;dataValid=true;dataAge.restart();pulseRunning=true;
        writeMode->blockSignals(true);writeMode->setChecked(true);writeMode->blockSignals(false);
        table->selectRow(0);updateWriteControls();ok=ok&&writeZero->isEnabled()&&writeOneButton->isEnabled();
        table->selectRow(17);updateWriteControls();ok=ok&&!writeZero->isEnabled()&&!writeOneButton->isEnabled();
        table->selectRow(20);updateWriteControls();ok=ok&&!writeZero->isEnabled();
        feedbackUnlock->blockSignals(true);feedbackUnlock->setChecked(true);feedbackUnlock->blockSignals(false);
        updateWriteControls();ok=ok&&writeZero->isEnabled();
        writeMode->blockSignals(true);writeMode->setChecked(false);writeMode->blockSignals(false);
        feedbackUnlock->blockSignals(true);feedbackUnlock->setChecked(false);feedbackUnlock->blockSignals(false);
        connected=false;pulseRunning=false;
        QVector<int> sample(47,0); sample[45]=1;
        worker->pointsUpdated(sample);
        ok=ok&&table->item(45,3)->text()=="1";
        worker->pointsUpdated(QVector<int>(47,-1));
        for(int r=0;r<47;++r) ok=ok&&table->item(r,3)->text()==QStringLiteral("失效");
        search->setText("M28.1"); int visible=0; for(int r=0;r<47;++r) if(!table->isRowHidden(r)) ++visible;
        ok=ok&&visible==1; search->clear();
        worker->statusText("Disconnected");
        for(int r=0;r<47;++r) ok=ok&&table->item(r,3)->text()==QStringLiteral("未知");
        QFile result(QCoreApplication::applicationDirPath()+"/ui-test-result.txt"); result.open(QIODevice::WriteOnly);
        result.write(ok?"PASS: 47 rows, bit state, invalidation, filter and disconnected state\n":"FAIL\n"); result.close();
        table->selectRow(13); status->setText(QStringLiteral("离线界面检查 · 未连接 PLC"));
        grab().save(QCoreApplication::applicationDirPath()+"/control-preview.png");
        qApp->exit(ok?0:4);
    });
    if(qApp->arguments().contains("--interval-5")) interval->setValue(5);
    if(qApp->arguments().contains("--smoke-test")) { QTimer::singleShot(200,button,&QPushButton::click); QTimer::singleShot(15000,qApp,[]{qApp->exit(3);}); }
}
MainWindow::~MainWindow(){ QMetaObject::invokeMethod(worker,"disconnectPlc",Qt::BlockingQueuedConnection); thread.quit(); thread.wait(); }
void MainWindow::invalidate(){if(timingLabel) timingLabel->setText(QStringLiteral("实际刷新间隔：— ｜ 读取耗时：—"));setFaultValue(-1);dataValid=false; updateWriteControls();for(int r=0;r<table->rowCount();++r){table->item(r,3)->setText(QStringLiteral("未知")); table->item(r,3)->setForeground(QColor("#777777"));} updated->setText(QStringLiteral("当前无有效数据"));}
void MainWindow::filter(){for(int r=0;r<table->rowCount();++r){bool match=false;for(int c=0;c<6;++c) match=match||table->item(r,c)->text().contains(search->text(),Qt::CaseInsensitive);table->setRowHidden(r,!match||(group->currentIndex()>0&&table->item(r,0)->text()!=group->currentText()));}updateWriteControls();}



void MainWindow::setupWriteControls(QVBoxLayout* layout) {
    auto row=new QHBoxLayout; layout->addLayout(row);
    writeMode=new QCheckBox(QStringLiteral("开启写入模式")); feedbackUnlock=new QCheckBox(QStringLiteral("解锁反馈位（可能被 PLC 覆盖）"));
    row->addWidget(writeMode); row->addWidget(feedbackUnlock); row->addStretch();
    selected=new QLabel(QStringLiteral("请选择一个点位")); selected->setWordWrap(true); layout->addWidget(selected);
    auto buttons=new QHBoxLayout; layout->addLayout(buttons);
    writeZero=new QPushButton(QStringLiteral("写 0")); writeOneButton=new QPushButton(QStringLiteral("写 1"));
    writeResult=new QLabel(QStringLiteral("单次写入后立即读回；ERR_RESET 按普通位保持 0/1"));
    buttons->addWidget(writeZero); buttons->addWidget(writeOneButton); buttons->addWidget(writeResult,1);
    auto pulse=new QHBoxLayout; layout->addLayout(pulse);
    pulse->addWidget(new QLabel(QStringLiteral("FeedingDog · M22.1")));
    period=new QSpinBox; period->setRange(5,600000); period->setValue(1000); period->setSuffix(" ms");
    width=new QSpinBox; width->setRange(1,599999); width->setValue(100); width->setSuffix(" ms");
    pulse->addWidget(new QLabel(QStringLiteral("周期（上升沿间隔）"))); pulse->addWidget(period);
    pulse->addWidget(new QLabel(QStringLiteral("高电平时长"))); pulse->addWidget(width);
    pulseStart=new QPushButton(QStringLiteral("启动脉冲")); pulseStop=new QPushButton(QStringLiteral("停止并置 0"));
    pulse->addWidget(pulseStart); pulse->addWidget(pulseStop); pulseLabel=new QLabel(QStringLiteral("未启动")); pulse->addWidget(pulseLabel,1);
    auto hint=new QLabel(QStringLiteral("低电平时长 = 周期 − 高电平时长，各至少 1 ms。电脑与网络存在延迟，不能保证硬实时；断网/程序异常时无法保证置 0。")); hint->setWordWrap(true); layout->addWidget(hint);
    logView=new QPlainTextEdit; logView->setReadOnly(true); logView->setMaximumBlockCount(300); logView->setMaximumHeight(105); layout->addWidget(logView);
    logPath=worker->logFilePath(); logReady=QFile::exists(logPath);
    logView->appendPlainText(logReady?QStringLiteral("日志：")+logPath:QStringLiteral("日志目录不可写，写入功能禁用"));
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(table,&QTableWidget::itemSelectionChanged,this,[this]{updateWriteControls();});
    connect(writeMode,&QCheckBox::toggled,this,[this](bool enabled){
        if(!enabled) {feedbackUnlock->blockSignals(true);feedbackUnlock->setChecked(false);feedbackUnlock->blockSignals(false);}
        QMetaObject::invokeMethod(worker,"setWriteMode",Qt::QueuedConnection,Q_ARG(bool,enabled),Q_ARG(bool,feedbackUnlock->isChecked())); updateWriteControls();
    });
    connect(feedbackUnlock,&QCheckBox::toggled,this,[this](bool enabled){
        QMetaObject::invokeMethod(worker,"setWriteMode",Qt::QueuedConnection,Q_ARG(bool,writeMode->isChecked()),Q_ARG(bool,enabled)); updateWriteControls();
    });
    connect(writeZero,&QPushButton::clicked,this,[this]{submitWrite(0);});
    connect(writeOneButton,&QPushButton::clicked,this,[this]{submitWrite(1);});
    connect(worker,&PlcWorker::oneWriteResult,this,[this](bool ok,const QString& text){writePending=false;writeResult->setText((ok?QStringLiteral("完成："):QStringLiteral("未确认："))+text);updateWriteControls();});
    connect(worker,&PlcWorker::writeModeReset,this,[this]{
        writeMode->blockSignals(true); feedbackUnlock->blockSignals(true); writeMode->setChecked(false);feedbackUnlock->setChecked(false);
        writeMode->blockSignals(false);feedbackUnlock->blockSignals(false); updateWriteControls();
    });
    connect(worker,&PlcWorker::actionLog,this,[this](const QString& text){ logView->appendPlainText(text); });
    connect(pulseStart,&QPushButton::clicked,this,[this]{
        if(!pulseStart->isEnabled()) return; pulseRunning=true; updateWriteControls();
        QMetaObject::invokeMethod(worker,"startPulse",Qt::QueuedConnection,Q_ARG(int,period->value()),Q_ARG(int,width->value()));
    });
    connect(pulseStop,&QPushButton::clicked,this,[this]{QMetaObject::invokeMethod(worker,"stopPulse",Qt::QueuedConnection);});
    connect(worker,&PlcWorker::pulseState,this,[this](bool running,const QString& text){pulseRunning=running;pulseLabel->setText(text);updateWriteControls();});
    connect(period,qOverload<int>(&QSpinBox::valueChanged),this,[this]{updateWriteControls();});
    connect(width,qOverload<int>(&QSpinBox::valueChanged),this,[this]{updateWriteControls();});
    auto stale=new QTimer(this); stale->setInterval(250); connect(stale,&QTimer::timeout,this,[this]{updateWriteControls();});stale->start();
    updateWriteControls();
}
void MainWindow::updateWriteControls() {
    if(faultValue>=0&&faultAge.isValid()&&faultAge.elapsed()>qMax(1000,monitorInterval*3)) setFaultValue(-1);
    if(!writeMode) return;
    bool ready=connected&&dataValid&&dataAge.isValid()&&dataAge.elapsed()<=qMax(1000,monitorInterval*3)&&logReady;
    writeMode->setEnabled(ready||writeMode->isChecked());
    feedbackUnlock->setEnabled(ready&&writeMode->isChecked()&&!writePending);
    int r=table->currentRow(); bool visible=r>=0&&!table->isRowHidden(r);
    if(visible) selected->setText(QStringLiteral("选中：%1 ｜ %2 ｜ %3 ｜ 当前值 %4 ｜ %5")
        .arg(table->item(r,1)->text(),table->item(r,2)->text(),table->item(r,4)->text(),table->item(r,3)->text(),table->item(r,5)->text()));
    else selected->setText(QStringLiteral("请选择一个可见点位"));
    bool feedingDogSelected=visible&&table->item(r,2)->text()=="M22.1";
    bool allowed=ready&&writeMode->isChecked()&&visible&&(r<20||feedbackUnlock->isChecked())&&!(pulseRunning&&feedingDogSelected)&&!writePending;
    const QString writeHint=pulseRunning&&feedingDogSelected?QStringLiteral("FeedingDog 正由脉冲控制，请先停止脉冲"):QString();
    writeZero->setToolTip(writeHint);writeOneButton->setToolTip(writeHint);
    writeZero->setEnabled(allowed); writeOneButton->setEnabled(allowed);
    pulseStart->setEnabled(ready&&writeMode->isChecked()&&!pulseRunning&&!writePending&&period->value()-width->value()>=1);
    pulseStop->setEnabled(pulseRunning); period->setEnabled(!pulseRunning);width->setEnabled(!pulseRunning);
}
void MainWindow::submitWrite(int value) {
    updateWriteControls(); if(!writeZero->isEnabled()) return;
    QString address=table->item(table->currentRow(),2)->text(); writePending=true;updateWriteControls();
    writeResult->setText(QStringLiteral("正在写入 %1 = %2，并读回…").arg(address).arg(value));
    QMetaObject::invokeMethod(worker,"writeOne",Qt::QueuedConnection,Q_ARG(QString,address),Q_ARG(int,value));
}




void MainWindow::setFaultValue(int value) {
    faultValue=value;
    if(value>=0) faultAge.restart(); else faultAge.invalidate();
    QString text=value==1?QStringLiteral("1 · 故障"):value==0?QStringLiteral("0 · 无故障"):QStringLiteral("状态未知");
    QString color=value==1?"#b42318":value==0?"#16794b":"#667085";
    QString bg=value==1?"#fee4e2":value==0?"#dcfae6":"#eaecf0";
    faultIndicator->setText(QStringLiteral("● M5.0 故障标志 ｜ ")+text);
    faultIndicator->setStyleSheet(QString("QLabel { color:%1; background:%2; border-radius:6px; padding:8px; font-weight:600; }").arg(color,bg));
    faultIndicator->setToolTip(QStringLiteral("独立只读监控；1=故障，0=无故障。不会自动改变其他点位或停止喂狗。"));
}
