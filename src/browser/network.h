#pragma once
#include <filesystem>
namespace turan {
bool ConfigureDns(bool secure,const std::filesystem::path& diagnostic={});
}
