#pragma once
#include "ComposableLoadouts.h"
#include "Database.h"
#include "FashionModel.h"
#include <functional>

namespace AlterEgo::NativeLibrary {

// Transient, resolved domain graph for presentation, import and edit planning.
// This is not a disk format. Only committed SQL records are authoritative.
struct Snapshot {
    Composable::Document loadouts;
    std::vector<Fashion::Look> fashion;
    std::int64_t revision = 0;
};

struct Match {
    std::string id, name;
    std::int64_t users = 0;
};

// Exact normalized content is independent of display name, component ID, and
// semantic Compare rules. Object keys are ordered by the JSON adapter; array
// position, explicit empty/unknown values, exact IDs and extensions are retained.
std::string ExactFingerprint(const Composable::Json& data);
bool ExactContent(const Composable::Json& a, const Composable::Json& b);

class Repository {
public:
    using Fingerprint = std::function<std::string(const Composable::Json&)>;
    explicit Repository(const std::string& utf8Path, Database::Connection::OpenMode mode,
                        Fingerprint fingerprint = ExactFingerprint, bool verify = true);
    Repository(const Repository&) = delete;
    Repository& operator=(const Repository&) = delete;

    Snapshot Read();
    // Apply only changed rows and changed dependent records in one transaction.
    // A stale edit plan must be refreshed rather than silently overwrite data.
    std::int64_t Commit(const Snapshot& before, const Snapshot& after);
    std::vector<Match> ExactMatches(const std::string& kind,
                                   const Composable::Json& data);
    std::vector<std::string> Users(const std::string& kind, const std::string& id);
private:
    Database::Connection db_;
    Fingerprint fingerprint_;
    Snapshot ReadUnlocked();
};

} // namespace AlterEgo::NativeLibrary
