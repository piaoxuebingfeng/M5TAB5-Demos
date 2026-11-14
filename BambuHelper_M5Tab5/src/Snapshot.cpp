// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

// SPDX-License-Identifier: AGPL-3.0-only
#include "Snapshot.hpp"
#if defined(BOARD_IS_WS_P4_7B)
#include "p4_json_allocator.h"
#else
#define p4JsonAllocator() ArduinoJson::detail::DefaultAllocator::instance()
#endif
#include <ArduinoJson.h>
#include <chrono>
#include <cstring>
#include <thread>
#if defined(ESP_PLATFORM) || defined(BAMBU_CAMERA_MBEDTLS)
#include <mbedtls/md.h>
#else
#include <openssl/evp.h>
#endif

namespace bambu_camera {
const char* error_name(Error e){
  switch(e){case Error::None:return "ok";case Error::Connect:return "connect_failed";
    case Error::Timeout:return "timeout";case Error::Protocol:return "invalid_reply";
    case Error::TooLarge:return "image_too_large";case Error::Integrity:return "integrity_failed";
    case Error::Credentials:return "invalid_application_credentials";
    case Error::Authentication:return "cloud_authentication_failed";
    case Error::Clock:return "clock_not_synced";}
  return "unknown";
}
const char* snapshot_failure_name(SnapshotFailure failure) {
  switch (failure) {
    case SnapshotFailure::None: return "none";
    case SnapshotFailure::Ticket: return "ticket";
    case SnapshotFailure::FrameLength: return "frame_length";
    case SnapshotFailure::HeaderLength: return "header_length";
    case SnapshotFailure::HeaderJson: return "header_json";
    case SnapshotFailure::ResultMissing: return "result_missing";
    case SnapshotFailure::RemoteResult: return "remote_result";
    case SnapshotFailure::ReplyMissing: return "reply_missing";
    case SnapshotFailure::ImageLimit: return "image_limit";
    case SnapshotFailure::TotalChanged: return "total_changed";
    case SnapshotFailure::MetadataLength: return "metadata_length";
    case SnapshotFailure::MetadataJson: return "metadata_json";
    case SnapshotFailure::RangeMissing: return "range_missing";
    case SnapshotFailure::RangeInvalid: return "range_invalid";
    case SnapshotFailure::DuplicateMismatch: return "duplicate_mismatch";
    case SnapshotFailure::Gap: return "gap";
    case SnapshotFailure::EmptyChunk: return "empty_chunk";
    case SnapshotFailure::FinalLength: return "final_length";
    case SnapshotFailure::Digest: return "digest";
    case SnapshotFailure::Jpeg: return "jpeg";
  }
  return "unknown";
}
const char* snapshot_stage_name(SnapshotStage stage) {
  switch (stage) {
    case SnapshotStage::NotStarted: return "not_started";
    case SnapshotStage::WaitingSession: return "waiting_session";
    case SnapshotStage::SessionReady: return "session_ready";
    case SnapshotStage::RequestSent: return "request_sent";
    case SnapshotStage::ReplyReceived: return "reply_received";
    case SnapshotStage::HeaderParsed: return "header_parsed";
    case SnapshotStage::ReplyMatched: return "reply_matched";
    case SnapshotStage::ImageChunk: return "image_chunk";
    case SnapshotStage::Complete: return "complete";
  }
  return "unknown";
}

static bool matches_md5(const BambuVector<uint8_t>& bytes,const char* expected){
  if(!expected||std::strlen(expected)!=32)return false;
  uint8_t digest[16];
#if defined(ESP_PLATFORM) || defined(BAMBU_CAMERA_MBEDTLS)
  if(mbedtls_md(mbedtls_md_info_from_type(MBEDTLS_MD_MD5),bytes.data(),bytes.size(),digest)!=0)return false;
#else
  unsigned n=0;
  if(EVP_Digest(bytes.data(),bytes.size(),digest,&n,EVP_md5(),nullptr)!=1||n!=16)return false;
#endif
  unsigned diff=0;
  for(size_t i=0;i<32;++i){
    const char c="0123456789abcdef"[(i&1)?digest[i/2]&15:digest[i/2]>>4];
    diff|=static_cast<unsigned char>(c)^static_cast<unsigned char>(expected[i]);
  }
  return diff==0;
}

bool SnapshotDownload::feed(const uint8_t* frame,size_t length){
  if(done_)return error_==Error::None;
  if(frame&&length)++diagnostics_.framesReceived;
  if(!frame||length==0||length>64*1024)return fail(Error::Protocol,SnapshotFailure::FrameLength);
  advanceStage(SnapshotStage::ReplyReceived);
  size_t split=0;
  while(split+1<length&&(frame[split]!='\n'||frame[split+1]!='\n'))++split;
  const bool binary=split+1<length;
  const size_t json_length=binary?split:length;
  if(json_length>4096)return fail(Error::Protocol,SnapshotFailure::HeaderLength);
  JsonDocument document(p4JsonAllocator());
  if(deserializeJson(document,frame,json_length))return fail(Error::Protocol,SnapshotFailure::HeaderJson);
  ++diagnostics_.headersParsed;
  advanceStage(SnapshotStage::HeaderParsed);
  if(!document["cmdtype"].is<uint32_t>()||!document["sequence"].is<uint32_t>())return true;
  if(document["cmdtype"].as<uint32_t>()!=4||document["sequence"].as<uint32_t>()!=sequence_)return true;
  ++diagnostics_.matchedReplies;
  advanceStage(SnapshotStage::ReplyMatched);
  if(!document["result"].is<int>())return fail(Error::Protocol,SnapshotFailure::ResultMissing);
  const int result=document["result"].as<int>();
  if(result!=0&&result!=1)return fail(Error::Protocol,SnapshotFailure::RemoteResult);
  diagnostics_.lastRemoteResult=result;
  JsonObjectConst reply=document["reply"].as<JsonObjectConst>();
  if(reply.isNull())return fail(Error::Protocol,SnapshotFailure::ReplyMissing);
  if(reply["total"].is<uint64_t>()){
    const auto total=reply["total"].as<uint64_t>();
    if(!total||total>kMaxImage)return fail(Error::TooLarge,SnapshotFailure::ImageLimit);
    if(total_&&total_!=total)return fail(Error::Protocol,SnapshotFailure::TotalChanged);
    total_=static_cast<size_t>(total);
    diagnostics_.expectedBytes=static_cast<uint32_t>(total_);
  }
  const uint8_t* payload=binary?frame+split+2:nullptr;
  size_t available=binary?length-split-2:0;
  if(!reply["mem_dl_param_size"].isNull()){
    if(!reply["mem_dl_param_size"].is<unsigned>())return fail(Error::Protocol,SnapshotFailure::MetadataLength);
    const auto metadata=reply["mem_dl_param_size"].as<unsigned>();
    if(!metadata||metadata>1024||metadata>available)return fail(Error::Protocol,SnapshotFailure::MetadataLength);
    JsonDocument meta(p4JsonAllocator());
    if(deserializeJson(meta,payload,metadata)||!meta["result"].is<int>()||meta["result"].as<int>()!=0)return fail(Error::Protocol,SnapshotFailure::MetadataJson);
    payload+=metadata;available-=metadata;
  }
  if(available){
    if(!total_||!reply["offset"].is<uint64_t>()||!reply["size"].is<uint64_t>())return fail(Error::Protocol,SnapshotFailure::RangeMissing);
    const auto offset=reply["offset"].as<uint64_t>();const auto size=reply["size"].as<uint64_t>();
    if(size!=available||offset>total_||size>total_-offset)return fail(Error::Protocol,SnapshotFailure::RangeInvalid);
    diagnostics_.lastOffset=static_cast<uint32_t>(offset);
    diagnostics_.lastSize=static_cast<uint32_t>(size);
    if(offset<image_.size()){
      if(size>image_.size()-offset||memcmp(image_.data()+offset,payload,size)!=0)return fail(Error::Protocol,SnapshotFailure::DuplicateMismatch);
    }else if(offset==image_.size()){
      image_.insert(image_.end(),payload,payload+available);
      ++diagnostics_.imageChunks;
      diagnostics_.receivedBytes=static_cast<uint32_t>(image_.size());
      advanceStage(SnapshotStage::ImageChunk);
    }else return fail(Error::Protocol,SnapshotFailure::Gap);
  }else if(reply["size"].is<uint64_t>()&&reply["size"].as<uint64_t>()!=0)return fail(Error::Protocol,SnapshotFailure::EmptyChunk);
  if(result==0){
    if(!total_||image_.size()!=total_)return fail(Error::Protocol,SnapshotFailure::FinalLength);
    if(!matches_md5(image_,reply["file_md5"]|static_cast<const char*>(nullptr)))return fail(Error::Integrity,SnapshotFailure::Digest);
    if(image_.size()<4||image_[0]!=0xff||image_[1]!=0xd8||image_[image_.size()-2]!=0xff||image_.back()!=0xd9)return fail(Error::Integrity,SnapshotFailure::Jpeg);
    done_=true;
    advanceStage(SnapshotStage::Complete);
  }
  return true;
}

std::string snapshot_request(uint32_t sequence){
  const auto n=std::to_string(sequence);
  const std::string parameters="{\"command\":\"get_project_file\",\"file_rel_path\":\"\",\"peer_host\":\"studio\",\"sequence_id\":"+n+",\"version\":1}";
  return "{\"mtype\":12289,\"cmdtype\":4,\"sequence\":"+n+",\"req\":{\"path\":\"mem:/26\",\"offset\":0,\"mem_dl_param_size\":"+std::to_string(parameters.size())+"}}\n\n"+parameters;
}

Error capture(const obn::camera::tutk::TutkSessionParams& params,BambuVector<uint8_t>& jpeg,unsigned timeout_ms,SnapshotFailure* failure,SnapshotStage* stage,SnapshotDiagnostics* diagnostics){
  using Clock=std::chrono::steady_clock;
  const auto captureStarted=Clock::now();
  if(failure)*failure=SnapshotFailure::None;
  if(stage)*stage=SnapshotStage::NotStarted;
  if(diagnostics)*diagnostics=SnapshotDiagnostics{};
  if(params.uid.size()!=20||params.authkey.size()>32||params.passwd.empty()||params.passwd.size()>256){
    if(failure)*failure=SnapshotFailure::Ticket;return Error::Protocol;
  }
  jpeg.clear();SnapshotReceiver transfer;
  std::atomic<bool> replyReceived{false};
  const auto deadline=Clock::now()+std::chrono::milliseconds(timeout_ms);
  obn::camera::tutk::TutkSession session;
  session.join(params,[&](const uint8_t* data,int size,int64_t,bool){
    if(size>0){replyReceived.store(true,std::memory_order_release);transfer.feed(data,static_cast<size_t>(size));}
  },obn::camera::tutk::TutkSession::Mode::Ctrl);
  if(stage)*stage=SnapshotStage::WaitingSession;
  while(session.is_joined()&&!session.is_ready()&&Clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(20));
  Error result=Error::Connect;
  if(session.is_ready()){
    if(stage)*stage=SnapshotStage::SessionReady;
    if(session.send_ctrl(snapshot_request())){
      if(stage)*stage=SnapshotStage::RequestSent;
      while(!transfer.done()&&session.is_joined()&&Clock::now()<deadline){
        if(replyReceived.load(std::memory_order_acquire)&&stage&&*stage==SnapshotStage::RequestSent)
          *stage=SnapshotStage::ReplyReceived;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
      // If the session ended, its final callback happened before joined=false.
      // Sample completion after that acquire so its final result is not missed.
      const bool stillJoined=session.is_joined();
      const bool completed=transfer.done();
      result=completed?transfer.error():stillJoined?Error::Timeout:Error::Connect;
      if(completed&&failure)*failure=transfer.failure();
      if(result==Error::None){jpeg=transfer.take();if(stage)*stage=SnapshotStage::Complete;}
      else if(replyReceived.load(std::memory_order_acquire)&&stage)*stage=SnapshotStage::ReplyReceived;
    }
  }else if(Clock::now()>=deadline)result=Error::Timeout;
  session.leave();
  if(stage&&replyReceived.load(std::memory_order_acquire)) *stage=transfer.stage();
  SnapshotDiagnostics finalDiagnostics=transfer.diagnostics();
  const auto transport=session.transport_diagnostics();
  finalDiagnostics.transportDatagrams=transport.datagramsReceived;
  finalDiagnostics.transportFramedPackets=transport.framedPackets;
  finalDiagnostics.fecRecovered=transport.fecRecovered;
  finalDiagnostics.fecLost=transport.fecLost;
  finalDiagnostics.transportFrameDrops=transport.framesDropped;
  finalDiagnostics.transportFramesEmitted=transport.framesEmitted;
  finalDiagnostics.pendingFecGroups=transport.pendingFecGroups;
  finalDiagnostics.pendingFecPackets=transport.pendingFecPackets;
  finalDiagnostics.pendingFrames=transport.pendingFrames;
  finalDiagnostics.pendingFrameSlices=transport.pendingFrameSlices;
  finalDiagnostics.pendingFrameExpectedSlices=transport.pendingFrameExpectedSlices;
  finalDiagnostics.invalidSliceHeaders=transport.invalidSliceHeaders;
  finalDiagnostics.avSequenceGaps=transport.avSequenceGaps;
  finalDiagnostics.avSequenceReordered=transport.avSequenceReordered;
  finalDiagnostics.appSendFailures=transport.appSendFailures;
  finalDiagnostics.connectionPath=transport.connectionPath;
  finalDiagnostics.socketReceiveBufferBytes=transport.socketReceiveBufferBytes;
  if(diagnostics) *diagnostics=finalDiagnostics;

  const bool routeCanChange = params.force_relay || transport.connectionPath != 3;
  if (params.allow_path_fallback && routeCanChange &&
      (result == Error::Timeout || result == Error::Connect)) {
    const SnapshotFailure preferredFailure=failure?*failure:transfer.failure();
    const SnapshotStage preferredStage=stage?*stage:transfer.stage();
    const uint32_t preferredDurationMs=static_cast<uint32_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now()-captureStarted).count());
    auto alternateParams=params;
    alternateParams.force_relay=!params.force_relay;
    alternateParams.allow_path_fallback=false;
    BambuVector<uint8_t> alternateJpeg;
    SnapshotFailure alternateFailure=SnapshotFailure::None;
    SnapshotStage alternateStage=SnapshotStage::NotStarted;
    SnapshotDiagnostics alternateDiagnostics{};
    const Error alternateResult=capture(alternateParams,alternateJpeg,timeout_ms,
                                        &alternateFailure,&alternateStage,
                                        &alternateDiagnostics);
    alternateDiagnostics.fallbackAttempted=true;
    alternateDiagnostics.preferredRelay=params.force_relay;
    alternateDiagnostics.preferredError=result;
    alternateDiagnostics.preferredFailure=preferredFailure;
    alternateDiagnostics.preferredStage=preferredStage;
    alternateDiagnostics.preferredDurationMs=preferredDurationMs;
    alternateDiagnostics.preferredReceivedBytes=finalDiagnostics.receivedBytes;
    alternateDiagnostics.preferredExpectedBytes=finalDiagnostics.expectedBytes;
    alternateDiagnostics.preferredConnectionPath=finalDiagnostics.connectionPath;
    jpeg=std::move(alternateJpeg);
    if(failure) *failure=alternateFailure;
    if(stage) *stage=alternateStage;
    if(diagnostics) *diagnostics=alternateDiagnostics;
    return alternateResult;
  }
  return result;
}
}
