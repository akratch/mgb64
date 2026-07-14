#ifndef MGB64_ENV_OWNERSHIP_H
#define MGB64_ENV_OWNERSHIP_H

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace EnvOwnership {

struct EnvOp {
    std::string key;
    bool        set;    // true: setenv(key,value); false: unsetenv(key)
    std::string value;  // meaningful only when set == true
};

std::vector<EnvOp> hatchOps(bool shootOutLights, bool autoAim);

std::map<std::string, std::string> parseAdvanced(const std::string &text);

struct Reconciliation {
    std::vector<EnvOp> ops;     // apply in order
    std::string        record;  // new ownership record to persist
};

Reconciliation reconcile(
    const std::map<std::string, std::string> &desired,
    const std::string &priorRecord,
    const std::function<std::optional<std::string>(const std::string &)> &lookupEnv);

std::string encodeRecord(
    const std::map<std::string, std::pair<bool, std::string>> &owned);
std::map<std::string, std::pair<bool, std::string>> decodeRecord(
    const std::string &rec);

}  // namespace EnvOwnership

#endif  // MGB64_ENV_OWNERSHIP_H
