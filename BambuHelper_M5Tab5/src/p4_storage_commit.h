#pragma once

#include <cstdint>

// These helpers describe the storage owner's commit order, independently of
// Arduino NVS or the renderer. They are also used by the host tests so a
// successful token write cannot later masquerade as a complete settings commit.
template<typename Settings>
inline void p4ClearCloudUserIds(Settings& settings, uint8_t printerCount) {
  for (uint8_t i = 0; i < printerCount; ++i)
    settings.printers[i].config.cloudUserId[0] = 0;
}

class P4StorageCommitter {
public:
  static uint32_t nextRevision(uint32_t& revision) {
    if (++revision == 0) revision = 1;
    return revision;
  }

  template<typename Settings, typename Store, typename Commit>
  bool settings(Settings& value, uint32_t& revision, Store&& store, Commit&& commit) {
    const uint32_t previous = revision;
    value.revision = nextRevision(revision);
    // Allocate the generation before the write. Argument evaluation order
    // must never decide which complete value reaches storage and consumers.
    if (!store(value)) {
      revision = previous;
      return false;
    }
    commit(value);
    return true;
  }

};

// NVS has no transaction spanning independently stored token and settings.
// This policy keeps the candidate private and restores both old values after
// a partial write. Store callbacks include their own read-back verification;
// a failed rollback is reported rather than presented as a committed account.
template<typename Settings, typename StoreSettings, typename StoreToken,
         typename Commit, typename ReadActual>
inline bool p4CommitCloudAccount(const char* candidateToken, const char* previousToken,
                                Settings& previous, Settings& candidate, uint32_t& revision,
                                StoreSettings&& storeSettings, StoreToken&& storeToken,
                                Commit&& publish, ReadActual&& readActual) {
  if (!candidateToken || !candidateToken[0]) return false;
  candidate.revision = P4StorageCommitter::nextRevision(revision);
  const bool candidateSettingsStored = storeSettings(candidate);
  const bool candidateTokenStored = candidateSettingsStored && storeToken(candidateToken);
  if (candidateSettingsStored && candidateTokenStored) {
    publish(candidate);
    return true;
  }
  previous.revision = P4StorageCommitter::nextRevision(revision);
  const bool settingsRestored = storeSettings(previous);
  const bool tokenRestored = storeToken(previousToken);
  if (settingsRestored && tokenRestored) {
    publish(previous);
  } else {
#ifdef ARDUINO
    Serial.println("NVS: P4 cloud account rollback FAILED");
#endif
    Settings actual;
    readActual(actual);
    actual.revision = P4StorageCommitter::nextRevision(revision);
    publish(actual);
  }
  return false;
}
