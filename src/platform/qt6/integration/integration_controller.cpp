#include "integration_controller.h"

#include <utility>

#include "core/paths.h"

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

QStringList qsl(const std::vector<std::string>& values) {
    QStringList out;
    for (const auto& v : values) out << qs(v);
    return out;
}

sp::integrations::Platform toPlatformImpl(int platform) {
    return platform == 1 ? sp::integrations::Platform::Kick : sp::integrations::Platform::Twitch;
}

}  // namespace

IntegrationController::IntegrationController(QObject* parent)
    : QObject(parent),
      services_(),
      store_(sp::defaultTokenPath()),
      manager_(std::make_unique<sp::integrations::IntegrationManager>(
          sp::integrations::loadIntegrationConfig(sp::defaultConfigPath()), services_, store_)) {}

void IntegrationController::reload() {
    // Las peticiones del gestor anterior ya no interesan.
    services_.http().cancelAll();
    if (manager_) {
        // Invalida temporizadores y callbacks pendientes (device flow, refresh).
        manager_->twitch().cancel();
        manager_->kick().cancel();
        // El gestor anterior se conserva: sus temporizadores programados capturan
        // punteros a sus clientes y deben seguir siendo válidos al dispararse.
        retired_.push_back(std::move(manager_));
    }
    manager_ = std::make_unique<sp::integrations::IntegrationManager>(
        sp::integrations::loadIntegrationConfig(sp::defaultConfigPath()), services_, store_);
    emit credentialsReloaded();
    restoreAll();
}

sp::integrations::Platform IntegrationController::toPlatform(int platform) {
    return toPlatformImpl(platform);
}

sp::integrations::IBroadcaster& IntegrationController::provider(int platform) {
    return manager_->provider(toPlatform(platform));
}

sp::integrations::Capabilities IntegrationController::capabilities(int platform) const {
    return manager_->provider(toPlatform(platform)).capabilities();
}

QString IntegrationController::configurationProblem(int platform) const {
    return qs(manager_->provider(toPlatform(platform)).configurationProblem());
}

QString IntegrationController::accountName(int platform) const {
    const auto& a = manager_->provider(toPlatform(platform)).account();
    return qs(a.displayName.empty() ? a.login : a.displayName);
}

bool IntegrationController::isConnected(int platform) const {
    return manager_->provider(toPlatform(platform)).authState() == sp::integrations::AuthState::Connected;
}

const sp::integrations::Account& IntegrationController::account(int platform) const {
    return manager_->provider(toPlatform(platform)).account();
}

void IntegrationController::restoreAll() {
    for (int platform = 0; platform <= 1; ++platform) {
        provider(platform).restore([this, platform](const sp::integrations::Result& result,
                                                    const sp::integrations::Account& account) {
            const auto state = provider(platform).authState();
            QString display = qs(account.displayName.empty() ? account.login : account.displayName);
            QString message = qs(result.message);
            emit authChanged(platform, static_cast<int>(state), display, message);
            if (result.ok()) {
                provider(platform).fetchChannel(
                    [this, platform](const sp::integrations::Result& r,
                                     const sp::integrations::ChannelSnapshot& snap) {
                        if (!r.ok()) {
                            emit channelFailed(platform, qs(r.message));
                            return;
                        }
                        emit channelReady(platform, qs(snap.title), qs(snap.categoryId),
                                          qs(snap.categoryName), qsl(snap.tags), snap.tagsKnown);
                    });
            }
        });
    }
}

void IntegrationController::connectPlatform(int platform) {
    emit authChanged(platform, static_cast<int>(sp::integrations::AuthState::Connecting), {}, {});
    provider(platform).connect([this, platform](const sp::integrations::ConnectResult& result) {
        if (result.pending) {
            emit authChanged(platform, static_cast<int>(sp::integrations::AuthState::Connecting), {}, {});
            emit authPrompt(platform, qs(result.userCode), qs(result.verificationUri));
            return;
        }
        if (!result.result.ok()) {
            emit authChanged(platform, static_cast<int>(sp::integrations::AuthState::Error), {},
                             qs(result.result.message));
            return;
        }
        const QString display = qs(result.account.displayName.empty() ? result.account.login
                                                                     : result.account.displayName);
        emit authChanged(platform, static_cast<int>(sp::integrations::AuthState::Connected), display, {});
        provider(platform).fetchChannel([this, platform](const sp::integrations::Result& r,
                                                        const sp::integrations::ChannelSnapshot& snap) {
            if (!r.ok()) {
                emit channelFailed(platform, qs(r.message));
                return;
            }
            emit channelReady(platform, qs(snap.title), qs(snap.categoryId), qs(snap.categoryName),
                              qsl(snap.tags), snap.tagsKnown);
        });
    });
}

void IntegrationController::cancelPlatform(int platform) {
    provider(platform).cancel();
    emit authChanged(platform, static_cast<int>(sp::integrations::AuthState::Disconnected), {}, {});
}

void IntegrationController::disconnectPlatform(int platform) {
    provider(platform).disconnect();
    emit authChanged(platform, static_cast<int>(sp::integrations::AuthState::Disconnected), {}, {});
}

void IntegrationController::refreshChannel(int platform) {
    provider(platform).fetchChannel([this, platform](const sp::integrations::Result& r,
                                                    const sp::integrations::ChannelSnapshot& snap) {
        if (!r.ok()) {
            emit channelFailed(platform, qs(r.message));
            return;
        }
        emit channelReady(platform, qs(snap.title), qs(snap.categoryId), qs(snap.categoryName),
                          qsl(snap.tags), snap.tagsKnown);
    });
}

void IntegrationController::searchCategories(int platform, int requestId, const QString& query,
                                             const QString& cursor) {
    provider(platform).searchCategories(query.toStdString(), cursor.toStdString(),
                                        [this, platform, requestId](
                                            const sp::integrations::Result& r,
                                            const sp::integrations::CategoryPage& page) {
                                            if (!r.ok()) {
                                                emit categoriesFailed(platform, requestId, qs(r.message));
                                                return;
                                            }
                                            QStringList ids, names;
                                            for (const auto& c : page.items) {
                                                ids << qs(c.id);
                                                names << qs(c.name);
                                            }
                                            emit categoriesReady(platform, requestId, ids, names,
                                                                 qs(page.cursor));
                                        });
}

void IntegrationController::publish(int platform, const QString& title, const QString& categoryId,
                                    const QString& categoryName, const QStringList& tags,
                                    bool applyTitle, bool applyCategory, bool applyTags) {
    sp::integrations::PublishRequest req;
    req.title = title.toStdString();
    req.categoryId = categoryId.toStdString();
    req.categoryName = categoryName.toStdString();
    for (const auto& t : tags) req.tags.push_back(t.toStdString());
    req.applyTitle = applyTitle;
    req.applyCategory = applyCategory;
    req.applyTags = applyTags;
    provider(platform).publish(req, [this, platform, title, categoryId, categoryName, tags](
                                        const sp::integrations::Result& r) {
        emit publishFinished(platform, r.ok(), qs(r.message), title, categoryId, categoryName, tags);
    });
}
