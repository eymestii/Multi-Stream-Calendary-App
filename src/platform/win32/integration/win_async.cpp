#include "win_async.h"

#include <memory>
#include <utility>

namespace sp::win {

bool processMessage(UINT message, WPARAM, LPARAM lparam) {
    if (message == kHttpDone) {
        std::unique_ptr<HttpCompletion> done(reinterpret_cast<HttpCompletion*>(lparam));
        if (done && done->callback) done->callback(std::move(done->response));
        return true;
    }
    if (message == kSchedule) {
        std::unique_ptr<std::function<void()>> callback(
            reinterpret_cast<std::function<void()>*>(lparam));
        if (callback && *callback) (*callback)();
        return true;
    }
    if (message == kLoopback) {
        std::unique_ptr<LoopbackMessage> msg(reinterpret_cast<LoopbackMessage*>(lparam));
        if (msg && msg->callback) msg->callback(std::move(msg->target));
        return true;
    }
    return false;
}

}  // namespace sp::win
