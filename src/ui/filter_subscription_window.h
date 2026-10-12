#pragma once
#include "privacy/filter_subscription.h"
#include <windows.h>
#include <functional>
#include <memory>

namespace turan {
// Native-only modeless panel. The callback schedules publication on the owner's
// worker; no renderer bridge or web-controlled subscription URLs are exposed.
HWND OpenFilterSubscriptionWindow(HWND owner,std::shared_ptr<FilterSubscriptions> subscriptions,
                                  bool turkish,std::function<void()> policy_changed,
                                  std::function<std::string()> publication_status={});
}
