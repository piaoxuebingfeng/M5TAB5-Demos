#pragma once

// NVS has no transaction spanning settings/account and the camera namespace.
// On failure always attempt every restoration before publishing actual state.
// A failed reset can never masquerade as a complete successful rollback.
struct P4FactoryResetResult { bool reset = false, restored = false; };

template<typename Reset, typename RestoreSettings, typename RestoreAccount,
         typename RestoreCamera, typename PublishActual>
P4FactoryResetResult p4FactoryResetStorage(Reset&& reset,
    RestoreSettings&& restoreSettings, RestoreAccount&& restoreAccount,
    RestoreCamera&& restoreCamera, PublishActual&& publishActual) {
  if (reset()) {
    publishActual();
    return {true, false};
  }
  const bool settings = restoreSettings();
  const bool account = restoreAccount();
  const bool camera = restoreCamera();
  publishActual();
  return {false, settings && account && camera};
}
