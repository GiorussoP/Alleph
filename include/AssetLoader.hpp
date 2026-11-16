#pragma once

#include <cstdlib>
#include <string>

inline std::string getAssetPath(const std::string &relative) {
  const char *appdir = std::getenv("APPDIR");
  if (appdir) {
    return std::string(appdir) + "/usr/bin/assets/" + relative;
  }

  return std::string("./assets/") + relative;
}