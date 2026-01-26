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

    a.setStyle("Fusion");

    QString qssPath = QCoreApplication::applicationDirPath() + "/light.qss";

#ifdef QT_DEBUG
    qDebug() << "CurrentPath =" << QDir::currentPath();
    qDebug() << "AppDirPath  =" << QCoreApplication::applicationDirPath();
    qDebug() << "QSS path    =" << qssPath;
#endif

    if (QFile::exists(qssPath)) {
        QString qss = loadTextFile(qssPath);
#ifdef QT_DEBUG
        qDebug() << "QSS length  =" << qss.size();
#endif
        if (!qss.isEmpty()) a.setStyleSheet(qss);
    } else {
#ifdef QT_DEBUG
        qDebug() << "QSS not found.";
#endif
    }

    MainWindow w;
    w.show();
    return a.exec();
}
