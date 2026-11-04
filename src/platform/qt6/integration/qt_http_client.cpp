#include "qt_http_client.h"

#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <utility>

QtHttpClient::QtHttpClient(QObject* parent) : QObject(parent), nam_(new QNetworkAccessManager(this)) {}

void QtHttpClient::send(sp::integrations::HttpRequest request, sp::integrations::HttpCallback callback) {
    QNetworkRequest req(QUrl(QString::fromStdString(request.url)));
    for (const auto& [k, v] : request.headers)
        req.setRawHeader(QByteArray::fromStdString(k), QByteArray::fromStdString(v));
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     request.followRedirects ? QNetworkRequest::NoLessSafeRedirectPolicy
                                             : QNetworkRequest::ManualRedirectPolicy);
    if (request.timeoutMs > 0) req.setTransferTimeout(request.timeoutMs);

    const QByteArray method = QByteArray::fromStdString(request.method);
    const QByteArray body = QByteArray::fromStdString(request.body);
    QNetworkReply* reply = nullptr;
    if (method == "GET") {
        reply = nam_->get(req);
    } else if (method == "POST") {
        reply = nam_->post(req, body);
    } else {
        reply = nam_->sendCustomRequest(req, method, body);
    }
    pending_.insert(reply);

    // Límite defensivo: ninguna respuesta legítima de estas APIs lo supera.
    constexpr qint64 kMaxBytes = 8 * 1024 * 1024;

    connect(reply, &QNetworkReply::finished, this, [this, reply, callback = std::move(callback)]() mutable {
        pending_.remove(reply);
        if (cancelled_.remove(reply)) {
            // cancelAll(): el contrato dice que los callbacks pendientes no se invocan.
            reply->deleteLater();
            return;
        }
        sp::integrations::HttpResponse response;
        const QVariant status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
        if (status.isValid()) response.status = status.toInt();

        const auto headers = reply->rawHeaderPairs();
        for (const auto& h : headers)
            response.headers.emplace_back(h.first.toStdString(), h.second.toStdString());

        const QByteArray data = reply->readAll();
        response.body = data.toStdString();

        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() == QNetworkReply::OperationCanceledError) {
                response.error = "petición cancelada";
            } else if (response.status == 0) {
                response.error = reply->errorString().toStdString();
            }
        }
        if (data.size() > kMaxBytes) {
            response.error = "respuesta demasiado grande";
            response.status = 0;
        }
        reply->deleteLater();
        if (callback) callback(std::move(response));
    });

    if (request.timeoutMs > 0) {
        QTimer::singleShot(request.timeoutMs + 1000, reply, [reply] {
            if (reply->isRunning()) reply->abort();
        });
    }
}

void QtHttpClient::cancelAll() {
    const auto replies = pending_;
    pending_.clear();
    for (QNetworkReply* reply : replies) {
        // Marcar antes de abortar: abort() puede emitir finished de inmediato.
        cancelled_.insert(reply);
        reply->abort();
    }
}
