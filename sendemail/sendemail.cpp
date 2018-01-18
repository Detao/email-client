#include "sendemail.h"
#include "qtimer.h"
//#define emailPI_DEBUG
//#define emailDTP_DEBUG
#ifndef QT_NO_EMAIL
#include "qtcpsocket.h"
#include <QFile>
#include <QFileInfo>
#include <memory.h>

QT_BEGIN_NAMESPACE
class emailCommand
{
public:
    emailCommand(sendemail::Command cmd, QStringList raw);
    int id;
    sendemail::Command command;
    QStringList rawCmds;
    static QBasicAtomicInt idCounter;
};

QBasicAtomicInt emailCommand::idCounter = Q_BASIC_ATOMIC_INITIALIZER(1);
emailCommand::emailCommand(sendemail::Command cmd, QStringList raw)
    : command(cmd), rawCmds(raw)
{
    id = idCounter.fetchAndAddRelaxed(1);
}
class emailPI : public QObject
{
    Q_OBJECT

public:
    emailPI(QObject *parent = 0);
    void connectToHost(const QString &host, quint16 port);

    bool sendCommands(const QStringList &cmds);
    bool sendCommand(const QString &cmd)
    { return sendCommands(QStringList(cmd)); }

    void clearPendingCommands();
    void abort();

    QString currentCommand() const
    { return currentCmd; }
    bool transferConnectionExtended;

signals:
    void e_connectState(int);
    void e_finished(const QString&);
    void e_error(int, const QString&);

private slots:
    void connected();
    void connectionClosed();
    void delayedCloseFinished();
    void readyRead();
    void error(QAbstractSocket::SocketError);

    //void ConnectState(int);

private:
    void timerEvent( QTimerEvent *event );
    // the states are modelled after the generalized state diagram of RFC 959,
    // page 58
    enum State {
        Begin,
        Idle,
        Waiting,
        Success,
        Failure
    };

    bool processReply();
    bool startNextCmd();
    QTcpSocket commandSocket;
    QString replyText;
    char replyCode[3];
    State state;
    QStringList pendingCommands;
    QString currentCmd;
    int m_emailPITime;
};

emailPI::emailPI(QObject *parent) :
    QObject(parent),
    transferConnectionExtended(true),
    commandSocket(0),
    state(Begin),
    currentCmd(QString()),
    m_emailPITime(0)
{

    commandSocket.setObjectName(QLatin1String("emailPI_socket"));
    connect(&commandSocket, SIGNAL(connected()),
            SLOT(connected()));
    connect(&commandSocket, SIGNAL(disconnected()),
            SLOT(connectionClosed()));
    connect(&commandSocket, SIGNAL(readyRead()),
            SLOT(readyRead()));
    connect(&commandSocket, SIGNAL(error(QAbstractSocket::SocketError)),
            SLOT(error(QAbstractSocket::SocketError)));
}
void emailPI::timerEvent( QTimerEvent *event )
{
    emit e_error(sendemail::NotConnected,sendemail::tr("Time out"));
    killTimer(m_emailPITime);
    m_emailPITime=0;
}
void emailPI::connectToHost(const QString &host, quint16 port)
{
    //emit connectState(sendemail::HostLookup);
    commandSocket.connectToHost(host, port);
}
bool emailPI::sendCommands(const QStringList &cmds)
{
    if (!pendingCommands.isEmpty())
        return false;

    if (commandSocket.state() != QTcpSocket::ConnectedState || state!=Idle) {
        emit e_error(sendemail::NotConnected, sendemail::tr("Not connected"));
        return true; // there are no pending commands
    }

    pendingCommands = cmds;
    startNextCmd();
    return true;
}

void emailPI::clearPendingCommands()
{
    pendingCommands.clear();
    currentCmd.clear();
    state = Idle;
}
void emailPI::connected()
{
    state = Begin;
#if defined(emailPI_DEBUG)
    //    qDebug("emailPI state: %d [connected()]", state);
#endif
    // try to improve performance by setting TCP_NODELAY
    commandSocket.setSocketOption(QAbstractSocket::LowDelayOption, 1);

    emit e_connectState(sendemail::Connected);
}

void emailPI::connectionClosed()
{
    commandSocket.close();
    emit e_connectState(sendemail::Unconnected);
}

void emailPI::delayedCloseFinished()
{
    emit e_connectState(sendemail::Unconnected);
}

void emailPI::error(QAbstractSocket::SocketError e)
{
    if (e == QTcpSocket::HostNotFoundError) {
        emit e_connectState(sendemail::Unconnected);
        emit e_error(sendemail::HostNotFound,
                   sendemail::tr("Host %1 not found").arg(commandSocket.peerName()));
    } else if (e == QTcpSocket::ConnectionRefusedError) {
        emit e_connectState(sendemail::Unconnected);
        emit e_error(sendemail::ConnectionRefused,
                   sendemail::tr("Connection refused to host %1").arg(commandSocket.peerName()));
    } else if (e == QTcpSocket::SocketTimeoutError) {
        emit e_connectState(sendemail::Unconnected);
        emit e_error(sendemail::ConnectionRefused,
                   sendemail::tr("Connection timed out to host %1").arg(commandSocket.peerName()));
    }else if(e==QTcpSocket::NetworkError)
    {
        emit e_connectState(sendemail::Unconnected);
        emit e_error(sendemail::NotConnected,
                   commandSocket.peerName());
    }
}

void emailPI::readyRead()
{
    killTimer(m_emailPITime);
    m_emailPITime=0;
    while (commandSocket.canReadLine()) {
        // read line with respect to line continuation
        QString line = QString::fromLatin1(commandSocket.readLine());
        if (replyText.isEmpty()) {
            if (line.length() < 3) {
                // protocol error
                return;
            }
            const int lowerLimit[3] = {1,0,0};
            const int upperLimit[3] = {5,5,9};
            for (int i=0; i<3; i++) {
                replyCode[i] = line[i].digitValue();
                if (replyCode[i]<lowerLimit[i] || replyCode[i]>upperLimit[i]) {
                    // protocol error
                    return;
                }
            }
        }
        QString endOfMultiLine;
        endOfMultiLine[0] = '0' + replyCode[0];
        endOfMultiLine[1] = '0' + replyCode[1];
        endOfMultiLine[2] = '0' + replyCode[2];
        endOfMultiLine[3] = QLatin1Char(' ');
        QString lineCont(endOfMultiLine);
        lineCont[3] = QLatin1Char('-');
        QString lineLeft4 = line.left(4);

        while (lineLeft4 != endOfMultiLine) {
            if (lineLeft4 == lineCont)
                replyText += line.mid(4); // strip 'xyz-'
            else
                replyText += line;
            if (!commandSocket.canReadLine())
                return;
            line = QString::fromLatin1(commandSocket.readLine());
            lineLeft4 = line.left(4);
        }
        replyText += line.mid(4); // strip reply code 'xyz '
        if (replyText.endsWith(QLatin1String("\r\n")))
            replyText.chop(2);

        if (processReply())
            replyText = QLatin1String("");
    }
}

bool emailPI::processReply()
{
#if defined(emailPI_DEBUG)
    //    qDebug("emailPI state: %d [processReply() begin]", state);
    if (replyText.length() < 400)
        qDebug("emailPI recv: %d %s", 100*replyCode[0]+10*replyCode[1]+replyCode[2], replyText.toLatin1().constData());
    else
        qDebug("emailPI recv: %d (text skipped)", 100*replyCode[0]+10*replyCode[1]+replyCode[2]);
#endif

    int replyCodeInt = 100*replyCode[0] + 10*replyCode[1] + replyCode[2];
    switch (state) {
    case Begin:
        if (replyCodeInt == 220) {
            state = Idle;
            emit e_finished(sendemail::tr("Connected to host %1").arg(commandSocket.peerName()));
            break;
        }
        // reply codes not starting with 1 or 2 are not handled.
        return true;
    case Waiting:
        if (replyCodeInt != 250)
            state = Failure;
        else
            state = Success;
        break;
    default:
        // ignore unrequested message
        return true;
    }

    // special actions on certain replies

    if (replyCodeInt == 334) {
        //username ok
        state = Idle;

    } else if (replyCodeInt == 235) {
        // password ok
        state = Idle;
        emit e_connectState(sendemail::LoggedIn);

    } else if (replyCodeInt == 354) {
        // can write data
        while(!pendingCommands.isEmpty())
        {
            startNextCmd();
        }

    }else if(replyCodeInt == 221)
        {
        state= Idle;
    }

    // react on new state
    switch (state) {
    case Begin:
        // should never happen
        break;
    case Success:
        // success handling
        state = Idle;
        // no break!
    case Idle:
        startNextCmd();
        break;
    case Waiting:
        // do nothing
        break;
    case Failure:
        emit e_error(sendemail::UnknownError, replyText);
        break;
    }
#if defined(emailPI_DEBUG)
    //    qDebug("emailPI state: %d [processReply() end]", state);
#endif
    return true;
}

/*
  Starts next pending command. Returns false if there are no pending commands,
  otherwise it returns true.
*/
bool emailPI::startNextCmd()
{
    if (pendingCommands.isEmpty()) {
        currentCmd.clear();
        emit e_finished(replyText);
        return false;
    }
    currentCmd = pendingCommands.first();
    pendingCommands.pop_front();
#if defined(emailPI_DEBUG)
    qDebug("emailPI send: %s", currentCmd.left(currentCmd.length()-2).toLatin1().constData());
#endif
    state = Waiting;
    if(currentCmd.startsWith(QLatin1String("_STARTWRITEDATA")))
    {

        QFile m_File(currentCmd.mid(15));
        if(!m_File.open(QIODevice::ReadOnly))
        {
            emit e_error(sendemail::Send,tr("open file failed %1").QString::arg(currentCmd.mid(15)));
            killTimer(m_emailPITime);
            m_emailPITime=0;
            return true;
        }
        else{
            int blockSize=45;
            int nread=0;
            char buf[1024];
            do{
                memset(buf,0,1024);
                nread=m_File.read(buf,blockSize);
                if(nread<=0)
                {
                    break;
                }
                std::string cur;
                size_t found;
                cur=sendemail::Base64Encode(buf,nread);
                if((found=cur.find_first_of('\0'))!=std::string::npos)
                {
                    cur[found]='\r';
                    cur[found+1]='\n';
                    cur=cur.substr(0,found+2);
                }
                else
                {
                    cur.append("\r\n");
                }

                commandSocket.write(cur.c_str(), cur.length()/*.toLatin1()*/);

            }while(nread > 0);
        }
        m_File.close();
        return true;
    }
    if(m_emailPITime==0)
        m_emailPITime=startTimer(5000);
    commandSocket.write(currentCmd.toStdString().c_str(), currentCmd.length()/*.toLatin1()*/);

    return true;
}
class emailPrivate
{
    Q_DECLARE_PUBLIC(sendemail)
public:

    inline emailPrivate(sendemail *owner) : close_waitForStateChange(false), state(sendemail::Unconnected),
        error(sendemail::NoError),q_ptr(owner)
    { }

    ~emailPrivate() { while (!pending.isEmpty()) delete pending.takeFirst(); }

    // private slots
    void _q_startNextCommand();
    void _q_piFinished(const QString&);
    void _q_piError(int, const QString&);
    void _q_piConnectState(int);

    int addCommand(emailCommand *cmd);

    emailPI pi;
    QList<emailCommand *> pending;
    bool close_waitForStateChange;
    sendemail::State state;
    sendemail::Error error;
    QString errorString;
    sendemail *q_ptr;
};
int emailPrivate::addCommand(emailCommand *cmd)
{
    pending.append(cmd);

    if (pending.count() == 1) {
        // don't emit the commandStarted() signal before the ID is returned
        QTimer::singleShot(0, q_func(), SLOT(_q_startNextCommand()));
    }
    return cmd->id;
}
void emailPrivate::_q_startNextCommand()
{
    Q_Q(sendemail);
    if (pending.isEmpty())
        return;
    emailCommand *c = pending.first();

    error = sendemail::NoError;
    errorString = QT_TRANSLATE_NOOP(sendemail, QLatin1String("Unknown error"));

    emit q->e_commandStarted(c->id);

    if (c->command == sendemail::ConnectToHost) {
        pi.connectToHost(c->rawCmds[0], c->rawCmds[1].toUInt());
    }
    else {
        if (c->command ==sendemail::Send) {

        } else if (c->command ==sendemail::Close) {
            state = sendemail::Closing;
            emit q->e_stateChanged(state);
        }
        pi.sendCommands(c->rawCmds);
    }


}
void emailPrivate::_q_piFinished(const QString&)
{
    if (pending.isEmpty())
        return;
    emailCommand *c = pending.first();

    if (c->command == sendemail::Close) {
        if (state != sendemail::Unconnected) {
            close_waitForStateChange = true;
            return;
        }
    }
    emit q_func()->e_commandFinished(c->id, false);
    pending.removeFirst();

    delete c;

    if (pending.isEmpty()) {
        emit q_func()->e_done(false);
    } else {
        _q_startNextCommand();
    }
}
void emailPrivate::_q_piError(int errorCode, const QString &text)
{
    Q_Q(sendemail);

    if (pending.isEmpty()) {
        qWarning("emailPrivate::_q_piError was called without pending command!");
        return;
    }
    emailCommand *c = pending.first();
    error = sendemail::Error(errorCode);
    switch (q->currentCommand()) {
    case sendemail::ConnectToHost:
        errorString = QString::fromLatin1(QT_TRANSLATE_NOOP("sendemail", "Connecting to host failed:\n%1"))
                .arg(text);
        break;
    case sendemail::Login:
        errorString = QString::fromLatin1(QT_TRANSLATE_NOOP("sendemail", "Login failed:\n%1"))
                .arg(text);
        break;
    case sendemail::Send:
        errorString = QString::fromLatin1(QT_TRANSLATE_NOOP("sendemail", "send data failed:\n%1"))
                .arg(text);
        break;
    default:
        errorString = text;
        break;
    }

    pi.clearPendingCommands();
    q->clearPendingCommands();
    emit q->e_commandFinished(c->id, true);

    pending.removeFirst();
    delete c;
    if (pending.isEmpty())
        emit q->e_done(true);
    else
        _q_startNextCommand();
}

void emailPrivate::_q_piConnectState(int connectState)
{
    state = sendemail::State(connectState);
    emit q_func()->e_stateChanged(state);
    if (close_waitForStateChange) {
        close_waitForStateChange = false;
        _q_piFinished(QLatin1String(QT_TRANSLATE_NOOP("sendemail", "Connection closed")));
    }
}
sendemail::sendemail(QObject *parent)
    :QObject(parent),d(new emailPrivate(this))
{
    d->errorString=tr("Unknown error");
    connect(&d->pi, SIGNAL(e_connectState(int)),
            SLOT(_q_piConnectState(int)));
    connect(&d->pi, SIGNAL(e_finished(QString)),
            SLOT(_q_piFinished(QString)));
    connect(&d->pi, SIGNAL(e_error(int,QString)),
            SLOT(_q_piError(int,QString)));
}
int sendemail::connectToHost(const QString &smtpserver, qint16 port)
{
    QStringList cmds;
    m_smtpserver=smtpserver;
    cmds << smtpserver;
    cmds << QString::number((uint)port);
    int id =  d->addCommand(new emailCommand(ConnectToHost,cmds));
    return id;

}
int sendemail::login(const QString &username,const QString &password)
{
    QStringList cmds;
    m_username=username;
    cmds << QLatin1String("HELO ") + m_smtpserver + QLatin1String("\r\n");
    cmds << QLatin1String("auth login\r\n");
    cmds << QString::fromStdString(Base64Encode(username.toStdString().c_str(),username.length())) + QLatin1String("\r\n");
    cmds << QString::fromStdString(Base64Encode(password.toStdString().c_str(),password.length())) + QLatin1String("\r\n");
    int id =  d->addCommand(new emailCommand(Login,cmds));
    return id;
}
int sendemail::send(const QString &toname,const QString &subject,const QByteArray &data)
{
    QStringList cmds;
    cmds << QLatin1String("mail from:<") + m_username + QLatin1String(">\r\n");
    cmds << QLatin1String("rcpt to:<") + toname + QLatin1String(">\r\n");
    cmds << QLatin1String("data\r\n");
    cmds << QLatin1String("From: ") + m_username + QLatin1String("\r\n");
    cmds << QLatin1String("to:") + toname + QLatin1String("\r\n");
    cmds << QLatin1String("Subject: ")+subject+ QLatin1String("\r\n\r\n");
    cmds << data + QLatin1String("\r\n.\r\n");
    int id = d->addCommand(new emailCommand(Send,cmds));
    return id;
}
int sendemail::send(const QString &toname,const QString &subject,
                    const QByteArray &data,const QString &attachtext_path)
{
    QFileInfo file(attachtext_path);
    QStringList cmds;
    cmds << QLatin1String("mail from:<") + m_username + QLatin1String(">\r\n");
    cmds << QLatin1String("rcpt to:<") + toname + QLatin1String(">\r\n");
    cmds << QLatin1String("data\r\n");
    cmds << QLatin1String("From: ") + m_username + QLatin1String("\r\n");
    cmds << QLatin1String("To: ") + toname + QLatin1String("\r\n");
    cmds << QLatin1String("Subject: ")+subject+ QLatin1String("\r\n");
    cmds << QLatin1String("Mime-Version: 1.0\r\n");
    cmds << QLatin1String("Content-Type: multipart/mixed;\r\n");
    cmds << QLatin1String(" boundary=\"__=_Part_Boundary_001_011991.029871\"\r\n\r\n");
    cmds << QLatin1String("--__=_Part_Boundary_001_011991.029871\r\n");
    cmds << QLatin1String("Content-Description: enclosed photo\r\n");
    cmds << QLatin1String("Content-Type: text/plain;\r\n");
    cmds << QLatin1String(" charset=\"gb2312\"\r\n"); //至少空一格
    cmds << QLatin1String("Content-Transfer-Encoding: base64\r\n\r\n");
    cmds << QString::fromStdString(Base64Encode(data.toStdString().c_str(),data.length())) + QLatin1String("\r\n\r\n"); //邮件正文!
    cmds << QLatin1String("--__=_Part_Boundary_001_011991.029871\r\n");
    cmds << QLatin1String("Content-Type: application/octet-stream;\r\n");
    cmds << QLatin1String(" name=") +file.fileName() + QLatin1String("\r\n"); //至少空一格
    cmds << QLatin1String("Content-Transfer-Encoding: base64\r\n\r\n");
    cmds << QLatin1String("_STARTWRITEDATA") + attachtext_path;
    cmds << QLatin1String("\r\n\r\n");
    //cmds << QLatin1String("--__=_Part_Boundary_001_011991.029871--\r\n");
    cmds << QLatin1String("\r\n.\r\n");  //结束符
    int id = d->addCommand(new emailCommand(Send,cmds));
    return id;
}
int sendemail::close()
{
    return  d->addCommand(new emailCommand(Close,QStringList(QLatin1String("quit\r\n"))));
}
int sendemail::currentId() const
{
    if (d->pending.isEmpty())
        return 0;
    return d->pending.first()->id;
}
sendemail::Command sendemail::currentCommand() const
{
    if (d->pending.isEmpty())
        return None;
    return d->pending.first()->command;
}
bool sendemail::hasPendingCommands() const
{
    return d->pending.count() > 1;
}
void sendemail::clearPendingCommands()
{
    // delete all entires except the first one
    while (d->pending.count() > 1)
        delete d->pending.takeLast();
}
sendemail::State sendemail::state() const
{
    return d->state;
}
sendemail::Error sendemail::error() const
{
    return d->error;
}
QString sendemail::errorString() const
{
    return d->errorString;
}
sendemail::~sendemail()
{
    close();
}
std::string sendemail::Base64Encode(const char* src,const int length) {
    int i, j, srcLen;
    srcLen=length;
    std::string dst(srcLen / 3 * 4 + 4, 0);
    for(i = 0, j= 0; i <=srcLen - 3; i+=3, j+=4) {
        dst[j] = (src[i] & 0xFC) >> 2;
        dst[j+1] = ((src[i] & 0x03) << 4) + ((src[i+1] & 0xF0) >> 4);
        dst[j+2] = ((src[i+1] & 0x0F) << 2) + ((src[i+2] & 0xC0) >> 6);
        dst[j+3] = src[i+2] & 0x3F;
    }
    if( srcLen % 3 == 1 ) {
        dst[j] = (src[i] & 0xFC) >> 2;
        dst[j+1] = ((src[i] & 0x03) << 4);
        dst[j+2] = 64;
        dst[j+3] = 64;
        j += 4;
    }
    else if( srcLen % 3 == 2 ) {
        dst[j] = (src[i] & 0xFC) >> 2;
        dst[j+1] = ((src[i] & 0x03) << 4) + ((src[i+1] & 0xF0) >> 4);
        dst[j+2] = ((src[i+1] & 0x0F) << 2);
        dst[j+3] = 64;
        j+=4;
    }

    static unsigned char *base64 = (unsigned char*)("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/=");
    for(i = 0; i < j; ++i) {    //map 6 bit value to base64 ASCII character
        dst[i] = base64[(int)dst[i]];
    }
    return dst;
}
QT_END_NAMESPACE
#include "sendemail.moc"
#include "moc_sendemail.cpp"
#endif // QT_NO_sendemail

