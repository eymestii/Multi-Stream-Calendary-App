// Cliente oficial de Twitch (Helix + OAuth Device Code Flow).
#pragma once
#include <functional>
#include <string>
#include <vector>

#include "core/integrations/broadcaster.h"
#include "core/integrations/config.h"
#include "core/integrations/http.h"
#include "core/integrations/platform_services.h"
#include "core/integrations/token_store.h"

namespace sp::integrations {

class TwitchClient final : public IBroadcaster {
public:
    TwitchClient(ProviderConfig config, IPlatformServices& services, ITokenStore& tokens);

    Platform platform() const override { return Platform::Twitch; }
    Capabilities capabilities() const override;
    AuthState authState() const override { return state_; }
    bool configured() const override { return config_.configured(); }
    std::string configurationProblem() const override;
    const Account& account() const override { return account_; }

    void restore(AccountCallback callback) override;
    void connect(ConnectCallback callback) override;
    void disconnect() override;
    void fetchChannel(ChannelCallback callback) override;
    void searchCategories(std::string query, std::string cursor,
                          CategoriesCallback callback) override;
    void publish(const PublishRequest& request, PublishCallback callback) override;
    void cancel() override;

private:
    using ApiCallback = std::function<void(const HttpResponse&, const Result&)>;

    void api(HttpRequest request, ApiCallback callback, bool allowRefresh = true);
    void refresh(VoidCallback callback);
    void fetchMe(VoidCallback callback);
    void pollDevice(ConnectCallback callback);
    void failConnect(const ConnectCallback& callback, const Result& result);

    ProviderConfig config_;
    IPlatformServices& services_;
    ITokenStore& store_;

    TokenSet tokens_;
    Account account_;
    AuthState state_ = AuthState::Disconnected;

    bool refreshing_ = false;
    std::vector<VoidCallback> refreshWaiters_;

    int generation_ = 0;
    std::string deviceCode_;
    std::string verificationUri_;
    std::string deviceScope_;
    int pollInterval_ = 5;
    std::int64_t deviceExpiresAt_ = 0;
};

}  // namespace sp::integrations
