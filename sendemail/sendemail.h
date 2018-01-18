#ifndef SENDEMAIL_H
#define SENDEMAIL_H
#include <QtCore/qstring.h>
#include <QtCore/qobject.h>
QT_BEGIN_HEADER
class emailPrivate;
class sendemail:public QObject
{
    Q_OBJECT
public:
    explicit sendemail(QObject *parent = 0);
    virtual ~sendemail();
    enum Command{
        None,
        ConnectToHost,
        Login,
        Send,
        Close
    };
    enum State {
        Unconnected,

        Connected,
        LoggedIn,
        Closing
    };
    enum Error {
        NoError,
        UnknownError,
        HostNotFound,
        ConnectionRefused,
        NotConnected
    };
    int connectToHost(const QString &host,qint16 port=25);
    int login(const QString &username,const QString &password);
    int send(const QString &toname,const QString &subject,const QByteArray &data);
    int send(const QString &toname,const QString &subject,const QByteArray &data,const QString &attachtext_path);
    int close();

    int currentId() const;
    Command currentCommand() const;
    bool hasPendingCommands() const;
    void clearPendingCommands();

    State state() const;

    Error error() const;
    QString errorString() const;
    static std::string Base64Encode(const char* src,const int length=0);
Q_SIGNALS:
    void e_stateChanged(int);
    void e_commandStarted(int);
    void e_commandFinished(int, bool);
    void e_done(bool);

private:

    QString m_smtpserver;
    QString m_username;
    Q_DISABLE_COPY(sendemail)
    QScopedPointer<emailPrivate> d;

    Q_PRIVATE_SLOT(d, void _q_startNextCommand())
    Q_PRIVATE_SLOT(d, void _q_piFinished(const QString&))
    Q_PRIVATE_SLOT(d, void _q_piError(int, const QString&))
    Q_PRIVATE_SLOT(d, void _q_piConnectState(int))
};
QT_END_HEADER
#endif // SENDEMAIL_H
