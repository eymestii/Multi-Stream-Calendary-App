#include "qt_platform_services.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QHostAddress>
#include <QTcpSocket>
#include <QTimer>
#include <QUrl>

#include <utility>

void QtBrowserLauncher::openUrl(const std::string& url) {
    QDesktopServices::openUrl(QUrl(QString::fromStdString(url)));
}

QtLoopbackServer::QtLoopbackServer(QObject* parent) : QObject(parent) {
    connect(&server_, &QTcpServer::newConnection, this, &QtLoopbackServer::onNewConnection);
}

QtLoopbackServer::~QtLoopbackServer() { stop(); }

bool QtLoopbackServer::start(unsigned short port, std::function<void(std::string)> onCallback,
                             std::function<void(std::string)> onError) {
    stop();
    onCallback_ = std::move(onCallback);
    onError_ = std::move(onError);
    if (!server_.listen(QHostAddress::LocalHost, port)) {
        // El fallo inmediato se comunica devolviendo false; no se invoca onError
        // para no disparar dos callbacks de error en el cliente OAuth.
        return false;
    }
    port_ = server_.serverPort();
    return true;
}

void QtLoopbackServer::stop() {
    if (server_.isListening()) server_.close();
    for (QTcpSocket* socket : sockets_) {
        if (!socket) continue;
        // Evita que la señal disconnected vuelva a programar deleteLater().
        socket->disconnect(this);
        socket->abort();
        socket->deleteLater();
    }
    sockets_.clear();
    buffers_.clear();
    port_ = 0;
}

void QtLoopbackServer::onNewConnection() {
    while (QTcpSocket* socket = server_.nextPendingConnection()) {
        sockets_.push_back(socket);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readSocket(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] {
            buffers_.remove(socket);
            sockets_.removeAll(socket);
            socket->deleteLater();
        });
        if (socket->bytesAvailable() > 0) readSocket(socket);
    }
}

void QtLoopbackServer::readSocket(QTcpSocket* socket) {
    QByteArray& buffer = buffers_[socket];
    buffer += socket->readAll();
    if (buffer.size() > 64 * 1024) {
        socket->disconnectFromHost();
        return;
    }
    const int end = buffer.indexOf("\r\n");
    if (end < 0) return;

    const QList<QByteArray> parts = buffer.left(end).split(' ');
    if (parts.size() < 2) {
        socket->disconnectFromHost();
        return;
    }
    const std::string target = parts[1].toStdString();

    const QByteArray html =
        "<!doctype html><html><head><meta charset=\"utf-8\"><title>Stream Planner</title>"
        "<style>body{font-family:sans-serif;background:#1e1e1e;color:#eee;text-align:center;"
        "padding:60px}code{color:#8ec07c}</style></head><body>"
        "<h2>Autorización recibida</h2><p>Ya puedes cerrar esta pestaña y volver a "
        "<b>Stream Planner</b>.</p></body></html>";
    const QByteArray response = "HTTP/1.1 200 OK\r\nContent-Type: text/html; charset=utf-8\r\n"
                                "Content-Length: " + QByteArray::number(html.size()) +
                                "\r\nConnection: close\r\n\r\n" + html;
    socket->write(response);
    socket->flush();
    socket->disconnectFromHost();

    if (onCallback_) {
        auto cb = onCallback_;
        QTimer::singleShot(0, this, [cb, target] { cb(target); });
    }
}

QtPlatformServices::QtPlatformServices(QObject* parent) : QObject(parent) {}

std::unique_ptr<sp::integrations::ILoopbackServer> QtPlatformServices::makeLoopback() {
    return std::make_unique<QtLoopbackServer>();
}

void QtPlatformServices::scheduleAfter(int milliseconds, std::function<void()> callback) {
    QTimer::singleShot(milliseconds, QCoreApplication::instance(), [callback = std::move(callback)] {
        if (callback) callback();
    });
}
