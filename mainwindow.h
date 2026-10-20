#pragma once
#include <QMainWindow>
#include <QThread>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QTableWidget>
#include <QComboBox>
#include <QCheckBox>
#include <QSpinBox>
#include <QPlainTextEdit>
#include <QVBoxLayout>
#include <QElapsedTimer>
#include "plcworker.h"
class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent=nullptr);
    ~MainWindow();
private:
    void setupWriteControls(QVBoxLayout* layout);
    void updateWriteControls();
    void submitWrite(int value);
    void invalidate();
    void setFaultValue(int value);
    QLabel *faultIndicator=nullptr,*timingLabel=nullptr;
    QElapsedTimer faultAge;
    int faultValue=-1;
    QCheckBox *writeMode=nullptr,*feedbackUnlock=nullptr;
    QPushButton *writeZero=nullptr,*writeOneButton=nullptr,*pulseStart=nullptr,*pulseStop=nullptr;
    QSpinBox *period=nullptr,*width=nullptr;
    QLabel *selected=nullptr,*writeResult=nullptr,*pulseLabel=nullptr;
    QPlainTextEdit *logView=nullptr;
    QElapsedTimer dataAge;
    bool dataValid=false,pulseRunning=false,writePending=false,logReady=false;
    QString logPath;
    int monitorInterval=200;
    void filter();
    QThread thread;
    PlcWorker *worker;
    QLineEdit *ip, *search;
    QPushButton *button;
    QLabel *status, *updated;
    QTableWidget *table;
    QComboBox *group;
    bool connected=false;
};
