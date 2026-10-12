#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace turan {
enum SitePermission : uint32_t { MicrophoneAccess=1,CameraAccess=2,LocationAccess=4,NotificationAccess=8,ClipboardAccess=16,ScreenAccess=32 };
struct PermissionRule {std::string origin,top;uint32_t kind=0;bool allow=false;};
bool PermissionOriginValid(const std::string& origin);
bool PermissionRuleValid(const PermissionRule& rule);
std::optional<bool> PermissionDecision(const std::vector<PermissionRule>& rules,const std::string& origin,const std::string& top,uint32_t kinds);
bool RememberPermission(std::vector<PermissionRule>& rules,const std::string& origin,const std::string& top,uint32_t kinds,bool allow);
}
