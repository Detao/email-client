#include <QApplication>
#include "sendemail.h"
#include <QDebug>
#include  "example.h"
#include <QFile>
int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
//    QFile file("/root/qt/test.txt");
//    if(file.open(QIODevice::WriteOnly))
//    {
//        int count=1000000;
//        while(count)
//        {
//            file.write("count");
//            count--;
//        }
//    }
        example test;
        test.send();
        qDebug()<<"123";

    return a.exec();
}
