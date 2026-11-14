// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Memory.hpp"
#include "TutkSession.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>

namespace bambu_camera {
enum class Error { None, Connect, Timeout, Protocol, TooLarge, Integrity, Credentials, Authentication, Clock };
const char* error_name(Error error);
// Safe diagnostic categories only: never include raw replies, tickets or keys.
enum class SnapshotFailure : uint8_t {
  None, Ticket, FrameLength, HeaderLength, HeaderJson, ResultMissing, RemoteResult,
  ReplyMissing, ImageLimit, TotalChanged, MetadataLength, MetadataJson,
  RangeMissing, RangeInvalid, DuplicateMismatch, Gap, EmptyChunk, FinalLength,
  Digest, Jpeg
};
const char* snapshot_failure_name(SnapshotFailure failure);

// Coarse, credential-free progress markers for the TUTK file request. The
// P4 status endpoint exposes only this stage, never the packet contents.
enum class SnapshotStage : uint8_t {
  NotStarted, WaitingSession, SessionReady, RequestSent, ReplyReceived,
  HeaderParsed, ReplyMatched, ImageChunk, Complete
};
const char* snapshot_stage_name(SnapshotStage stage);
struct SnapshotDiagnostics {
  uint32_t framesReceived=0;
  uint32_t headersParsed=0;
  uint32_t matchedReplies=0;
  uint32_t imageChunks=0;
  uint32_t expectedBytes=0;
  uint32_t receivedBytes=0;
  uint32_t lastOffset=0;
  uint32_t lastSize=0;
  int32_t lastRemoteResult=-1;
  uint32_t transportDatagrams=0;
  uint32_t transportFramedPackets=0;
  uint32_t fecRecovered=0;
  uint32_t fecLost=0;
  uint32_t transportFrameDrops=0;
  uint32_t transportFramesEmitted=0;
  uint32_t pendingFecGroups=0;
  uint32_t pendingFecPackets=0;
  uint32_t pendingFrames=0;
  uint32_t pendingFrameSlices=0;
  uint32_t pendingFrameExpectedSlices=0;
  uint32_t invalidSliceHeaders=0;
  uint32_t avSequenceGaps=0;
  uint32_t avSequenceReordered=0;
  uint32_t appSendFailures=0;
  uint8_t connectionPath=0;
  int32_t socketReceiveBufferBytes=-1;
  bool fallbackAttempted=false;
  bool preferredRelay=false;
  Error preferredError=Error::None;
  SnapshotFailure preferredFailure=SnapshotFailure::None;
  SnapshotStage preferredStage=SnapshotStage::NotStarted;
  uint32_t preferredDurationMs=0;
  uint32_t preferredReceivedBytes=0;
  uint32_t preferredExpectedBytes=0;
  uint8_t preferredConnectionPath=0;
};

// The printer sends a small JSON metadata blob before the JPEG. It is not
// part of the image. Only a complete, contiguous, MD5-checked JPEG is exposed.
class SnapshotDownload {
public:
  static constexpr size_t kMaxImage=512*1024;
  explicit SnapshotDownload(uint32_t sequence=1):sequence_(sequence){}
  bool feed(const uint8_t* frame,size_t length);
  bool done() const {return done_;}
  Error error() const {return error_;}
  SnapshotFailure failure() const {return failure_;}
  SnapshotStage stage() const {return stage_;}
  SnapshotDiagnostics diagnostics() const {return diagnostics_;}
  const BambuVector<uint8_t>& bytes() const {return image_;}
  BambuVector<uint8_t> take(){return std::move(image_);}
private:
  uint32_t sequence_;
  size_t total_=0;
  bool done_=false;
  Error error_=Error::None;
  SnapshotFailure failure_=SnapshotFailure::None;
  SnapshotStage stage_=SnapshotStage::NotStarted;
  SnapshotDiagnostics diagnostics_{};
  BambuVector<uint8_t> image_;
  void advanceStage(SnapshotStage stage) {
    if (static_cast<uint8_t>(stage)>static_cast<uint8_t>(stage_)) stage_=stage;
  }
  bool fail(Error error,SnapshotFailure failure){error_=error;failure_=failure;done_=true;image_.clear();return false;}
};

// The TUTK callback is the sole writer. Once it publishes completion, it no
// longer touches the download; the capture worker can acquire and move it.
class SnapshotReceiver {
  SnapshotDownload download_;
  std::atomic<bool> completed_{false};
public:
  void feed(const uint8_t* frame, size_t length) {
    if (completed_.load(std::memory_order_relaxed) || !frame || !length) return;
    download_.feed(frame, length);
    if (download_.done()) completed_.store(true, std::memory_order_release);
  }
  bool done() const { return completed_.load(std::memory_order_acquire); }
  Error error() const { return download_.error(); }             // only after done()
  SnapshotFailure failure() const { return download_.failure(); } // only after done()
  SnapshotStage stage() const { return download_.stage(); }       // after session.leave()
  SnapshotDiagnostics diagnostics() const { return download_.diagnostics(); } // after session.leave()
  BambuVector<uint8_t> take() { return download_.take(); }      // only after done()
};

std::string snapshot_request(uint32_t sequence=1);

// Blocking development API. Call from a dedicated worker, never the UI/MQTT
// loop. The transport owns the session and closes it on success or failure.
Error capture(const obn::camera::tutk::TutkSessionParams& params,
              BambuVector<uint8_t>& jpeg,unsigned timeout_ms=45000,
              SnapshotFailure* failure=nullptr,
              SnapshotStage* stage=nullptr,
              SnapshotDiagnostics* diagnostics=nullptr);
}
