#pragma once
// Experimental direct-cloud capture is opt-in while the actual account and
// board are being validated. Default production firmware does not open it.
#if defined(BOARD_IS_WS_P4_7B) && defined(P4_CLOUD_CAMERA_EXPERIMENTAL)
#include "Snapshot.hpp"
#include "bambu_state.h"
struct P4CameraAccount {
  const char* token=nullptr;
  const char* serial=nullptr;
  const char* firmware=nullptr;
  CloudRegion region=REGION_CN;
  // Imported once during setup; never embedded in source/release binaries.
  const char* appCertificate=nullptr;
  const char* appPrivateKey=nullptr;
  const char* appPlatform="macos";
  const char* executableInfo=nullptr;
  const char* appDeviceId=nullptr;
  const char* taskId=nullptr; // query the friendly design name once per job
  uint16_t plateIndex=0;
  void (*progress)(const char*)=nullptr; // fixed diagnostic stage names only
};
struct P4CameraResult {
  bambu_camera::Error error=bambu_camera::Error::None;
  int httpStatus=0;
  int serviceCode=0;
  int cryptoStep=0;
  int cryptoStatus=0;
  int metadataHttpStatus=0;
  bambu_camera::SnapshotFailure snapshotFailure=bambu_camera::SnapshotFailure::None;
  bambu_camera::SnapshotStage snapshotStage=bambu_camera::SnapshotStage::NotStarted;
  bambu_camera::SnapshotDiagnostics snapshotDiagnostics{};
  char jobName[128]={};
};
P4CameraResult p4CaptureCloudSnapshotFromAccount(const P4CameraAccount& account,
                                               BambuVector<uint8_t>& jpeg);
void p4FetchCloudJobName(const P4CameraAccount& account,P4CameraResult& result);
bambu_camera::Error p4CaptureCloudSnapshot(const obn::camera::tutk::TutkSessionParams& ticket,
                                         BambuVector<uint8_t>& jpeg,unsigned timeout_ms=45000,
                                         bambu_camera::SnapshotFailure* failure=nullptr,
                                         bambu_camera::SnapshotStage* stage=nullptr,
                                         bambu_camera::SnapshotDiagnostics* diagnostics=nullptr);
#endif
