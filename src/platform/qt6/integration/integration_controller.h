// Puente entre el core de integraciones (callbacks) y la UI Qt (señales).
#pragma once
#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

#include "core/integrations/integration_manager.h"
#include "core/integrations/token_store.h"
#include "qt_platform_services.h"

class IntegrationController : public QObject {
    Q_OBJECT
public:
    explicit IntegrationController(QObject* parent = nullptr);

    void restoreAll();
    // Relee integrations.json y recrea los clientes. Se usa tras guardar
    // credenciales nuevas desde el diálogo de credenciales.
    void reload();
    void connectPlatform(int platform);
    void cancelPlatform(int platform);
    void disconnectPlatform(int platform);
    void refreshChannel(int platform);
    void searchCategories(int platform, int requestId, const QString& query, const QString& cursor);
    void publish(int platform, const QString& title, const QString& categoryId,
                 const QString& categoryName, const QStringList& tags, bool applyTitle,
                 bool applyCategory, bool applyTags);

    sp::integrations::Capabilities capabilities(int platform) const;
    QString configurationProblem(int platform) const;
    QString accountName(int platform) const;
    bool isConnected(int platform) const;
    const sp::integrations::Account& account(int platform) const;

signals:
    void credentialsReloaded();
    void authChanged(int platform, int state, const QString& display, const QString& message);
    void authPrompt(int platform, const QString& userCode, const QString& url);
    void channelReady(int platform, const QString& title, const QString& categoryId,
                      const QString& categoryName, const QStringList& tags, bool tagsKnown);
    void channelFailed(int platform, const QString& message);
    void categoriesReady(int platform, int requestId, const QStringList& ids,
                         const QStringList& names, const QString& cursor);
    void categoriesFailed(int platform, int requestId, const QString& message);
    void publishFinished(int platform, bool ok, const QString& message, const QString& title,
                         const QString& categoryId, const QString& categoryName,
                         const QStringList& tags);

private:
    static sp::integrations::Platform toPlatform(int platform);
    sp::integrations::IBroadcaster& provider(int platform);

    QtPlatformServices services_;
    sp::integrations::FileTokenStore store_;
    std::unique_ptr<sp::integrations::IntegrationManager> manager_;
    // Gestores anteriores tras cambiar credenciales. Se conservan vivos hasta
    // el cierre porque los temporizadores del core (device flow, refresco de
    // tokens) capturan el cliente y podrían dispararse más tarde.
    std::vector<std::unique_ptr<sp::integrations::IntegrationManager>> retired_;
};
