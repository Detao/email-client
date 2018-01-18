#ifndef EXAMPLE_H
#define EXAMPLE_H
#include <QtCore/qobject.h>
#include "sendemail.h"
class example:public QObject
{
    Q_OBJECT
public:
    explicit example( QObject *parent = 0);
    ~example();
    void send();
public slots:
    void S_commandFinished(int,bool);
private:
    sendemail* mysend;
    int command_id;

};

#endif // EXAMPLE_H
