#include "mainwindow.h"
#include <QFile>
#include <QApplication>

#include <QMetaType>
#include <QVector>
#include <QByteArray>

#include <QDebug>//临时打印
#include <QDir>//临时打印

static QString loadTextFile(const QString& path){
    QFile f(path);
    if(!f.open(QFile::ReadOnly)) return "";
    return QString::fromUtf8(f.readAll());
}

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    qRegisterMetaType<QVector<int>>("QVector<int>");
    qRegisterMetaType<QByteArray>("QByteArray");   // 可选，保险

    qDebug() << "CurrentPath =" << QDir::currentPath();
    qDebug() << "AppDirPath  =" << QCoreApplication::applicationDirPath();

    a.setStyle("Fusion");

    // ✅ 用 exe 所在目录拼绝对路径
    QString qssPath = QCoreApplication::applicationDirPath() + "/light.qss";
    qDebug() << "QSS path    =" << qssPath;
    qDebug() << "QSS exists? =" << QFile::exists(qssPath);

    QString qss = loadTextFile(qssPath);
    qDebug() << "QSS length  =" << qss.size();
    a.setStyleSheet(qss);
    MainWindow w;
    w.show();
    return a.exec();
}
