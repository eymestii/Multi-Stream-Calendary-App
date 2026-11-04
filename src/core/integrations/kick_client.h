// Cliente oficial de Kick (OAuth 2.1 + PKCE con redirect loopback).
#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/integrations/broadcaster.h"
#include "core/integrations/config.h"
#include "core/integrations/http.h"
#include "core/integrations/platform_services.h"
#include "core/integrations/token_store.h"

namespace sp::integrations {

class KickClient final : public IBroadcaster {
public:
    KickClient(ProviderConfig config, IPlatformServices& services, ITokenStore& tokens);
    ~KickClient() override;

    Platform platform() const override { return Platform::Kick; }
    Capabilities capabilities() const override;
    AuthState authState() const override { return state_; }
    bool configured() const override;
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

    void beginAuthorize(ConnectCallback callback);
    void handleLoopback(std::string target, ConnectCallback callback, int gen);
    void exchangeCode(std::string code, ConnectCallback callback, int gen);
    void refresh(VoidCallback callback);
    void fetchMe(VoidCallback callback);
    void api(HttpRequest request, ApiCallback callback, bool allowRefresh = true);
    void failConnect(const ConnectCallback& callback, const Result& result);

    ProviderConfig config_;
    IPlatformServices& services_;
    ITokenStore& store_;

    TokenSet tokens_;
    Account account_;
    AuthState state_ = AuthState::Disconnected;

    std::unique_ptr<ILoopbackServer> loopback_;
    std::string loopbackPath_;
    std::string pendingState_;
    std::string codeVerifier_;
    std::string effectiveRedirect_;
    int generation_ = 0;

    bool refreshing_ = false;
    std::vector<VoidCallback> refreshWaiters_;
};

}  // namespace sp::integrations
