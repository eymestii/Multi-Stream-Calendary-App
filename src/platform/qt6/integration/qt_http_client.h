// Implementación Qt6 del transporte HTTP del core (QNetworkAccessManager).
#pragma once
#include <QHash>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSet>

#include "core/integrations/http.h"

class QNetworkReply;

class QtHttpClient : public QObject, public sp::integrations::IHttpClient {
    Q_OBJECT
public:
    explicit QtHttpClient(QObject* parent = nullptr);
    void send(sp::integrations::HttpRequest request,
              sp::integrations::HttpCallback callback) override;
    void cancelAll() override;

private:
    QNetworkAccessManager* nam_;
    QSet<QNetworkReply*> pending_;
    // Respuestas abortadas por cancelAll(): su callback no debe invocarse.
    QSet<QNetworkReply*> cancelled_;
};
