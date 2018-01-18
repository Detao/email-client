#include "example.h"
#include<QDebug>
example::example(QObject *parent):
    QObject(parent),
    command_id(0)
{
    mysend=new sendemail();
    connect(mysend,SIGNAL(e_commandFinished(int,bool)),this,SLOT(S_commandFinished(int,bool)));
}

void example::send()
{
    QString s1("smtp.163.com");
    QString username("***@163.com");
    QString password("***");
    QString tousername("****@sina.com");
    QString subject("test");
    QString attach_text("/root/qt/config.xml");
    QByteArray data("/root/qt/test1.zip");
    command_id=mysend->connectToHost(s1);
    command_id=mysend->login(username,password);
    command_id=mysend->send(tousername,subject,data);
    command_id=mysend->close();
}
void example::S_commandFinished(int tmp,bool en)
{
    Q_UNUSED(tmp)
    if(mysend->currentCommand() == sendemail::ConnectToHost){
        if(en){
            qDebug()<<mysend->errorString();
        }
    }else if (mysend->currentCommand() == sendemail::Login){
        if(en){
            qDebug()<<mysend->errorString();
        }
    }else if(mysend->currentCommand() == sendemail::Send){
        if(en){
            qDebug()<<mysend->errorString();
        }
        else{
             qDebug()<<tr("发送成功");
        }

    }else if (mysend->currentCommand() == sendemail::Close){

    }
}
example::~example()
{
    delete mysend;
}

