// Servicios de plataforma Qt6: navegador, servidor loopback de OAuth y temporizadores.
#pragma once
#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QVector>
#include <functional>
#include <memory>

#include "core/integrations/platform_services.h"
#include "qt_http_client.h"

class QTcpSocket;

class QtBrowserLauncher : public sp::integrations::IBrowserLauncher {
public:
    void openUrl(const std::string& url) override;
};

class QtLoopbackServer : public QObject, public sp::integrations::ILoopbackServer {
    Q_OBJECT
public:
    explicit QtLoopbackServer(QObject* parent = nullptr);
    ~QtLoopbackServer() override;

    bool start(unsigned short port, std::function<void(std::string)> onCallback,
               std::function<void(std::string)> onError) override;
    void stop() override;
    unsigned short port() const override { return port_; }

private:
    void onNewConnection();
    void readSocket(QTcpSocket* socket);

    QTcpServer server_;
    std::function<void(std::string)> onCallback_;
    std::function<void(std::string)> onError_;
    QHash<QTcpSocket*, QByteArray> buffers_;
    QVector<QTcpSocket*> sockets_;
    unsigned short port_ = 0;
};

class QtPlatformServices : public QObject, public sp::integrations::IPlatformServices {
    Q_OBJECT
public:
    explicit QtPlatformServices(QObject* parent = nullptr);

    sp::integrations::IHttpClient& http() override { return http_; }
    sp::integrations::IBrowserLauncher& browser() override { return browser_; }
    std::unique_ptr<sp::integrations::ILoopbackServer> makeLoopback() override;
    void scheduleAfter(int milliseconds, std::function<void()> callback) override;

private:
    QtHttpClient http_;
    QtBrowserLauncher browser_;
};
