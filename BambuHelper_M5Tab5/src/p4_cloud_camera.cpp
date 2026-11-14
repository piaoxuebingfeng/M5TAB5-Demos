// Board macro matrix. Arduino IDE has no global -D injection, so every
// translation unit pulls it in first (see board_config.h for the rationale).
#include "board_config.h"

#include "p4_cloud_camera.h"
#if defined(BOARD_IS_WS_P4_7B) && defined(P4_CLOUD_CAMERA_EXPERIMENTAL)
#include <esp_heap_caps.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include <mbedtls/rsa.h>
#include <mbedtls/x509_crt.h>
#include <esp_random.h>
#include <sys/time.h>
#include "cloud_tls.h"
#include "bambu_cloud.h"
#include "cloud_job_name.h"
#include "p4_json_allocator.h"

namespace {
int rng(void*,unsigned char* out,size_t length){esp_fill_random(out,length);return 0;}

void readJobName(HTTPClient& http,const P4CameraAccount& account,P4CameraResult& result){
  if(!account.taskId||!account.taskId[0]||!strcmp(account.taskId,"0"))return;
  if(account.progress)account.progress("job_metadata");
  http.setReuse(false);http.addHeader("Authorization",String("Bearer ")+account.token);
  http.addHeader("User-Agent","bambu_network_agent/02.08.02.60");
  http.addHeader("X-BBL-Client-Name","BambuStudio");http.addHeader("X-BBL-Client-Type","slicer");
  http.addHeader("X-BBL-Client-Version","02.08.02.61");
  const char* keys[]={"Transfer-Encoding"};http.collectHeaders(keys,1);
  result.metadataHttpStatus=http.GET();
  const int length=http.getSize();
  if(result.metadataHttpStatus!=200||length==0||length>128*1024)return;
  BambuVector<char> body;
  if(length>0)body.reserve(length+1);
  const uint32_t began=millis();
  auto* stream=http.getStreamPtr();
  // Do not use HTTPClient's writeToStream loop here: repeated zero-length
  // reads can spin without an overall deadline and starve the idle task.
  auto readSome=[&](uint8_t* out,size_t count)->int{
    while(millis()-began<10000){
      const int available=stream->available();
      if(available>0){const int n=stream->read(out,std::min<size_t>(available,count));vTaskDelay(1);if(n>0)return n;}
      else if(!stream->connected())return -1;
      vTaskDelay(1);
    }
    return -1;
  };
  auto exact=[&](size_t count){
    uint8_t buffer[1024];
    if(count>128*1024-body.size())return false;
    while(count){const int n=readSome(buffer,std::min(count,sizeof(buffer)));if(n<=0)return false;
      body.insert(body.end(),reinterpret_cast<char*>(buffer),reinterpret_cast<char*>(buffer)+n);count-=n;}
    return true;
  };
  if(length>0){if(!exact(length))return;}
  else if(http.header("Transfer-Encoding").equalsIgnoreCase("chunked")){
    for(;;){
      char line[96];size_t n=0;
      do{uint8_t c;if(readSome(&c,1)!=1||n+1>=sizeof(line))return;line[n++]=c;}while(line[n-1]!='\n');
      line[n]=0;char* end=nullptr;const unsigned long count=strtoul(line,&end,16);
      if(end==line||(*end!='\r'&&*end!='\n'&&*end!=';'))return;
      if(!count)break;
      if(!exact(count))return;
      uint8_t c;if(readSome(&c,1)!=1||c!='\r'||readSome(&c,1)!=1||c!='\n')return;
    }
  }else{
    uint8_t buffer[1024];int n;
    while((n=readSome(buffer,sizeof(buffer)))>0){
      if(size_t(n)>128*1024-body.size())return;
      body.insert(body.end(),reinterpret_cast<char*>(buffer),reinterpret_cast<char*>(buffer)+n);
    }
    if(millis()-began>=10000)return;
  }
  const size_t used=body.size();if(!used)return;body.push_back(0);
  JsonDocument filter(p4JsonAllocator());
  for(const char* field:{"id","deviceId","plateIndex","plateName","designTitleTranslated","designTitle"})filter["hits"][0][field]=true;
  JsonDocument document(p4JsonAllocator());
  if(!deserializeJson(document,body.data(),used,DeserializationOption::Filter(filter)))
    selectCloudJobName(document.as<JsonObjectConst>(),account.taskId,account.serial,account.plateIndex,result.jobName,sizeof(result.jobName));
}

bool buildProof(const P4CameraAccount& account,String& certId,String& proof,P4CameraResult& result){
  if(!account.appCertificate||!account.appPrivateKey)return false;
  const size_t certLen=strnlen(account.appCertificate,12001),keyLen=strnlen(account.appPrivateKey,8001);
  if(!certLen||certLen>12000||!keyLen||keyLen>8000)return false;
  mbedtls_x509_crt crt;mbedtls_x509_crt_init(&crt);
  mbedtls_pk_context key;mbedtls_pk_init(&key);
  bool good=false;
  do {
    result.cryptoStep=1;
    if((result.cryptoStatus=mbedtls_x509_crt_parse(&crt,reinterpret_cast<const uint8_t*>(account.appCertificate),certLen+1))!=0)break;
    result.cryptoStep=2;
    if((result.cryptoStatus=mbedtls_pk_parse_key(&key,reinterpret_cast<const uint8_t*>(account.appPrivateKey),keyLen+1,nullptr,0,rng,nullptr))!=0)break;
    result.cryptoStep=3;
    if(!mbedtls_pk_can_do(&key,MBEDTLS_PK_RSA)||mbedtls_pk_get_bitlen(&key)!=2048)break;
    result.cryptoStep=4;
    if((result.cryptoStatus=mbedtls_pk_check_pair(&crt.pk,&key,rng,nullptr))!=0)break;
    result.cryptoStep=5;
    char issuer[256];if(mbedtls_x509_dn_gets(issuer,sizeof(issuer),&crt.issuer)<=0)break;
    certId=issuer;certId+=':';
    size_t start=0;while(start+1<crt.serial.len&&crt.serial.p[start]==0)++start;
    if(crt.serial.len-start>32)break;
    for(size_t i=start;i<crt.serial.len;++i){certId+="0123456789abcdef"[crt.serial.p[i]>>4];certId+="0123456789abcdef"[crt.serial.p[i]&15];}
    timeval now;gettimeofday(&now,nullptr);char stamp[24];
    const int length=snprintf(stamp,sizeof(stamp),"%lld",static_cast<long long>(now.tv_sec)*1000+now.tv_usec/1000);
    uint8_t signature[256];
    if(length<=0||length>=int(sizeof(stamp)))break;
    result.cryptoStep=6;
    if((result.cryptoStatus=mbedtls_rsa_rsassa_pkcs1_v15_sign(mbedtls_pk_rsa(key),rng,nullptr,MBEDTLS_MD_NONE,length,reinterpret_cast<const uint8_t*>(stamp),signature))!=0)break;
    uint8_t encoded[345];size_t used=0;
    result.cryptoStep=7;
    // Mbed TLS requires space for its own terminating NUL as well as the
    // 344 base64 bytes of a 256-byte RSA signature.
    if((result.cryptoStatus=mbedtls_base64_encode(encoded,sizeof(encoded),&used,signature,sizeof(signature)))!=0)break;
    encoded[used]=0;proof=reinterpret_cast<const char*>(encoded);good=true;
  }while(false);
  if(good)result.cryptoStep=0;
  mbedtls_pk_free(&key);mbedtls_x509_crt_free(&crt);return good;
}
}

P4CameraResult p4CaptureCloudSnapshotFromAccount(const P4CameraAccount& account,BambuVector<uint8_t>& jpeg) try {
  using bambu_camera::Error;
  P4CameraResult result;jpeg.clear();
  if(!account.token||!account.token[0]||strnlen(account.token,4097)>4096||!account.serial||!account.serial[0]||strnlen(account.serial,129)>128||
     !account.appPlatform||(strcmp(account.appPlatform,"macos")&&strcmp(account.appPlatform,"linux")&&strcmp(account.appPlatform,"windows"))){result.error=Error::Credentials;return result;}
  if(time(nullptr)<1735689600){result.error=Error::Clock;return result;}
  if(heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT)<64*1024){result.error=Error::TooLarge;return result;}
  String certId,proof;
  if(account.progress)account.progress("signing");
  if(!buildProof(account,certId,proof,result)){result.error=Error::Credentials;return result;}
  obn::camera::tutk::TutkSessionParams ticket;
  {
    // Release the short HTTPS session completely before opening UDP/DTLS.
    // Model-image workers remain disabled. There is no video stream/decoder.
    WiFiClientSecure tls;configureCloudTrust(tls);tls.setTimeout(10);tls.setHandshakeTimeout(10);
    HTTPClient http;http.setTimeout(10000);http.setReuse(false);
    if(account.progress)account.progress("cloud_ticket");
    const String url=String(getBambuApiBase(account.region))+"/v1/iot-service/api/user/ttcode";
    if(!http.begin(tls,url)){result.error=Error::Connect;return result;}
    http.addHeader("Content-Type","application/json");http.addHeader("Authorization",String("Bearer ")+account.token);
    http.addHeader("User-Agent","bambu_network_agent/02.08.02.60");
    http.addHeader("X-BBL-Client-Name","BambuStudio");http.addHeader("X-BBL-Client-Type","slicer");
    http.addHeader("X-BBL-Client-Version","02.08.02.61");http.addHeader("X-BBL-OS-Type",account.appPlatform);
    http.addHeader("X-BBL-Agent-OS-Type",account.appPlatform);http.addHeader("X-BBL-Agent-Version","02.08.02.60");
    if(account.appDeviceId&&strnlen(account.appDeviceId,129)<=128)http.addHeader("X-BBL-Device-ID",account.appDeviceId);
    http.addHeader("x-bbl-app-certification-id",certId);http.addHeader("x-bbl-device-security-sign",proof);
    if(account.executableInfo){
      if(strnlen(account.executableInfo,8193)>8192){http.end();result.error=Error::Credentials;return result;}
      JsonDocument executable(p4JsonAllocator());String compact;
      if(deserializeJson(executable,account.executableInfo)||!executable.is<JsonObject>()){http.end();result.error=Error::Credentials;return result;}
      serializeJson(executable,compact);http.addHeader("X-BBL-Executable-info",compact);
    }
    JsonDocument request(p4JsonAllocator());request["dev_id"]=account.serial;request["dev_version"]=account.firmware?account.firmware:"";
    request["protocols"][0]="tutk";String body;serializeJson(request,body);
    result.httpStatus=http.POST(body);
    const int length=http.getSize();
    if(length<=0||length>16384){http.end();result.error=result.httpStatus==403?Error::Authentication:Error::Protocol;return result;}
    BambuVector<char> response(length+1);
    const size_t read=http.getStreamPtr()->readBytes(response.data(),length);
    if(read!=size_t(length)){result.error=Error::Protocol;return result;}
    response[length]=0;JsonDocument reply(p4JsonAllocator());
    if(deserializeJson(reply,response.data(),length)){result.error=Error::Protocol;return result;}
    result.serviceCode=reply["code"]|0;
    if(result.httpStatus!=200){result.error=Error::Authentication;return result;}
    JsonVariantConst data=reply.as<JsonVariantConst>();if(data["data"].is<JsonObjectConst>())data=data["data"];
    const char* type=data["type"]|"tutk";
    if(strcmp(type,"tutk")!=0){result.error=Error::Protocol;return result;}
    ticket.uid=data["ttcode"]|"";ticket.passwd=data["passwd"]|"";ticket.authkey=data["authkey"]|"";
    ticket.region=data["region"]|(account.region==REGION_CN?"cn":"us");ticket.relay_id=ticket.uid;
    http.end();tls.stop();
  }
  if(account.progress)account.progress("tutk_snapshot");
  result.error=p4CaptureCloudSnapshot(ticket,jpeg,45000,&result.snapshotFailure,
                                      &result.snapshotStage,&result.snapshotDiagnostics);return result;
} catch(...) {
  jpeg.clear();P4CameraResult result;result.error=bambu_camera::Error::TooLarge;return result;
}

// Independent best-effort metadata stage. The caller has already published
// the snapshot, closed TUTK, and must not wait for this on the UI/MQTT loop.
void p4FetchCloudJobName(const P4CameraAccount& account,P4CameraResult& result) try {
  if(!account.taskId||!account.taskId[0]||!strcmp(account.taskId,"0"))return;
  WiFiClientSecure tls;configureCloudTrust(tls);tls.setTimeout(1);tls.setHandshakeTimeout(5);
  HTTPClient http;http.setTimeout(3000);http.setConnectTimeout(3000);http.setReuse(false);
  const String url=String(getBambuApiBase(account.region))+"/v1/user-service/my/tasks?limit=10";
  if(http.begin(tls,url))readJobName(http,account,result);
  http.end();tls.stop();
}catch(...){}

// Retained by the experimental environment's --undefined flag so the P4
// build verifies the complete crypto/transport link, even before UI hookup.
extern "C" P4CameraResult p4_cloud_camera_link_check(const P4CameraAccount& account,BambuVector<uint8_t>& jpeg){
  return p4CaptureCloudSnapshotFromAccount(account,jpeg);
}
// Call only from a dedicated worker. The main UI/MQTT loop never waits on a
// handshake or a snapshot. Image buffers and FEC data live in PSRAM.
bambu_camera::Error p4CaptureCloudSnapshot(const obn::camera::tutk::TutkSessionParams& ticket,
                                         BambuVector<uint8_t>& jpeg,unsigned timeout_ms,
                                         bambu_camera::SnapshotFailure* failure,
                                         bambu_camera::SnapshotStage* stage,
                                         bambu_camera::SnapshotDiagnostics* diagnostics){
  if(heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM)<1024*1024)
    return bambu_camera::Error::TooLarge;
  auto preferred=ticket;
  preferred.force_relay=true;
  preferred.allow_path_fallback=true;
  try{return bambu_camera::capture(preferred,jpeg,timeout_ms,failure,stage,diagnostics);}
  catch(...){jpeg.clear();return bambu_camera::Error::TooLarge;}
}
#endif
