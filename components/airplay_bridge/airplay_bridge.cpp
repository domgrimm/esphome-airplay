#include "airplay_bridge.h"

#include "esphome/core/application.h"
#include "esphome/core/log.h"
#include "esphome/core/util.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <sstream>

#ifdef USE_ESP32
#include <esp_mac.h>
#endif

namespace esphome {
namespace airplay_bridge {

static const char *const TAG = "airplay_bridge";

void AirPlayBridge::add_target(media_player::MediaPlayer *player, const std::string &name) {
  TargetSpec spec;
  spec.player = player;
  spec.name = name;
  this->target_specs_.push_back(spec);
}

float AirPlayBridge::get_setup_priority() const { return setup_priority::AFTER_WIFI; }

void AirPlayBridge::setup() {
#if !defined(USE_ARDUINO) && !defined(USE_ESP_IDF)
  ESP_LOGE(TAG, "This component currently requires Arduino or esp-idf framework.");
  return;
#else
  this->setup_runtime_();
#endif
}

void AirPlayBridge::loop() {
  for (auto &target : this->runtimes_) {
    this->handle_target_(target);
  }
}

void AirPlayBridge::dump_config() {
  ESP_LOGCONFIG(TAG, "AirPlay Bridge:");
  ESP_LOGCONFIG(TAG, "  Port base: %u", this->port_base_);
  ESP_LOGCONFIG(TAG, "  Media URL template: %s", this->media_url_template_.empty() ? "(none)" : this->media_url_template_.c_str());
  ESP_LOGCONFIG(TAG, "  Targets: %u", static_cast<unsigned>(this->target_specs_.size()));
  for (const auto &target : this->target_specs_) {
    ESP_LOGCONFIG(TAG, "    - %s", target.name.c_str());
  }
}

void AirPlayBridge::setup_runtime_() {
  if (this->target_specs_.empty()) {
    ESP_LOGW(TAG, "No media player targets configured.");
    return;
  }

  std::string fallback_device_name = App.get_name();
  for (size_t idx = 0; idx < this->target_specs_.size(); idx++) {
    auto &spec = this->target_specs_[idx];
    if (spec.name.empty()) {
      std::string player_name = spec.player->get_name();
      spec.name = player_name.empty() ? fallback_device_name + " " + std::to_string(idx + 1) : player_name;
    }
    spec.port = static_cast<uint16_t>(this->port_base_ + idx);

    TargetRuntime runtime;
    runtime.spec = spec;
#ifdef USE_ARDUINO
    runtime.server = std::make_unique<WiFiServer>(spec.port);
    runtime.server->begin();
    runtime.server->setNoDelay(true);
#endif
#ifdef USE_ESP_IDF
    runtime.server_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (runtime.server_fd < 0) {
      ESP_LOGE(TAG, "socket() failed for target '%s' on port %u", spec.name.c_str(), spec.port);
      continue;
    }
    int reuse = 1;
    setsockopt(runtime.server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    sockaddr_in bind_addr{};
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    bind_addr.sin_port = htons(spec.port);
    if (bind(runtime.server_fd, reinterpret_cast<sockaddr *>(&bind_addr), sizeof(bind_addr)) < 0) {
      ESP_LOGE(TAG, "bind() failed for target '%s' on port %u", spec.name.c_str(), spec.port);
      close(runtime.server_fd);
      runtime.server_fd = -1;
      continue;
    }
    if (listen(runtime.server_fd, 1) < 0) {
      ESP_LOGE(TAG, "listen() failed for target '%s' on port %u", spec.name.c_str(), spec.port);
      close(runtime.server_fd);
      runtime.server_fd = -1;
      continue;
    }
    int flags = fcntl(runtime.server_fd, F_GETFL, 0);
    if (flags >= 0) {
      fcntl(runtime.server_fd, F_SETFL, flags | O_NONBLOCK);
    }
#endif
    this->runtimes_.push_back(std::move(runtime));
  }

  this->mdns_ready_ = this->setup_mdns_();
  if (!this->mdns_ready_) {
    ESP_LOGW(TAG, "mDNS service setup failed, discovery may not work.");
  } else {
    for (const auto &target : this->runtimes_) {
      this->advertise_target_(target);
    }
  }
}

bool AirPlayBridge::setup_mdns_() {
#ifdef USE_ESP32
  if (mdns_init() != ESP_OK) {
    ESP_LOGE(TAG, "mdns_init failed");
    return false;
  }
  std::string host = App.get_name();
  std::replace(host.begin(), host.end(), ' ', '-');
  mdns_hostname_set(host.c_str());
  mdns_instance_name_set(host.c_str());

  uint8_t mac[6];
  if (esp_read_mac(mac, ESP_MAC_WIFI_STA) != ESP_OK) {
    ESP_LOGW(TAG, "Failed to read STA MAC; using fallback identifier.");
    this->device_id_colon_ = "00:00:00:00:00:00";
    this->device_id_raop_ = "000000000000";
  } else {
    char colon_buf[18];
    char raop_buf[13];
    snprintf(colon_buf, sizeof(colon_buf), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    snprintf(raop_buf, sizeof(raop_buf), "%02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    this->device_id_colon_ = colon_buf;
    this->device_id_raop_ = raop_buf;
  }
  return true;
#endif
#ifdef USE_ESP8266
  std::string host = App.get_name();
  std::replace(host.begin(), host.end(), ' ', '-');
  if (!MDNS.begin(host.c_str())) {
    ESP_LOGE(TAG, "MDNS.begin failed");
    return false;
  }
  this->device_id_colon_ = "00:00:00:00:00:00";
  this->device_id_raop_ = "000000000000";
  return true;
#endif
  return false;
}

void AirPlayBridge::advertise_target_(const TargetRuntime &target) {
#ifdef USE_ESP32
  std::string raop_instance = this->device_id_raop_ + "@" + target.spec.name;
  mdns_txt_item_t raop_txt[] = {
      {(char *) "txtvers", (char *) "1"},
      {(char *) "ch", (char *) "2"},
      {(char *) "cn", (char *) "0,1"},
      {(char *) "et", (char *) "0,1"},
      {(char *) "md", (char *) "0,1,2"},
      {(char *) "pw", (char *) "false"},
      {(char *) "sr", (char *) "44100"},
      {(char *) "ss", (char *) "16"},
      {(char *) "tp", (char *) "UDP"},
      {(char *) "vn", (char *) "3"},
      {(char *) "vs", (char *) "220.68"},
      {(char *) "am", (char *) "ESPHome"},
      {(char *) "sf", (char *) "0x4"},
  };
  mdns_service_add(raop_instance.c_str(), "_raop", "_tcp", target.spec.port, raop_txt, sizeof(raop_txt) / sizeof(raop_txt[0]));

  mdns_txt_item_t airplay_txt[] = {
      {(char *) "deviceid", (char *) this->device_id_colon_.c_str()},
      {(char *) "features", (char *) "0x4A7FFFF7,0x1E"},
      {(char *) "flags", (char *) "0x4"},
      {(char *) "model", (char *) "ESPHomeAirPlay1,1"},
      {(char *) "pk", (char *) "0000000000000000000000000000000000000000"},
      {(char *) "pi", (char *) "00000000-0000-0000-0000-000000000000"},
      {(char *) "srcvers", (char *) "220.68"},
      {(char *) "vv", (char *) "2"},
  };
  mdns_service_add(target.spec.name.c_str(), "_airplay", "_tcp", target.spec.port, airplay_txt,
                   sizeof(airplay_txt) / sizeof(airplay_txt[0]));
#endif
#ifdef USE_ESP8266
  // ESP8266 Arduino mDNS does not expose service instances; only one raop entry is practical.
  MDNS.addService("raop", "tcp", target.spec.port);
  MDNS.addServiceTxt("raop", "tcp", "sr", "44100");
  MDNS.addServiceTxt("raop", "tcp", "ss", "16");
#endif
}

void AirPlayBridge::handle_target_(TargetRuntime &target) {
#ifdef USE_ARDUINO
  if (!target.client.connected()) {
    if (target.client) {
      target.client.stop();
    }
    target.buffer.clear();
    target.streaming = false;

    WiFiClient candidate = target.server->available();
    if (candidate) {
      target.client = candidate;
      target.client.setNoDelay(true);
      ESP_LOGI(TAG, "Client connected to target '%s' on port %u", target.spec.name.c_str(), target.spec.port);
    }
  }

  if (!target.client || !target.client.connected()) {
    return;
  }

  while (target.client.available()) {
    char c = static_cast<char>(target.client.read());
    target.buffer.push_back(c);
  }

  RtspRequest request;
  while (this->extract_next_request_(target, request)) {
    this->handle_request_(target, request);
  }
#endif

#ifdef USE_ESP_IDF
  if (target.server_fd < 0) {
    return;
  }

  if (target.client_fd < 0) {
    sockaddr_in client_addr{};
    socklen_t addr_len = sizeof(client_addr);
    int accepted = accept(target.server_fd, reinterpret_cast<sockaddr *>(&client_addr), &addr_len);
    if (accepted >= 0) {
      int flags = fcntl(accepted, F_GETFL, 0);
      if (flags >= 0) {
        fcntl(accepted, F_SETFL, flags | O_NONBLOCK);
      }
      target.client_fd = accepted;
      target.buffer.clear();
      target.streaming = false;
      ESP_LOGI(TAG, "Client connected to target '%s' on port %u", target.spec.name.c_str(), target.spec.port);
    }
  }

  if (target.client_fd < 0) {
    return;
  }

  char rx[1024];
  while (true) {
    const ssize_t read_len = recv(target.client_fd, rx, sizeof(rx), 0);
    if (read_len > 0) {
      target.buffer.append(rx, static_cast<size_t>(read_len));
      continue;
    }
    if (read_len == 0) {
      close(target.client_fd);
      target.client_fd = -1;
      target.buffer.clear();
      target.streaming = false;
      return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      break;
    }
    ESP_LOGW(TAG, "Socket read failed for target '%s' (errno=%d)", target.spec.name.c_str(), errno);
    close(target.client_fd);
    target.client_fd = -1;
    target.buffer.clear();
    target.streaming = false;
    return;
  }

  RtspRequest request;
  while (this->extract_next_request_(target, request)) {
    this->handle_request_(target, request);
  }
#endif
}

bool AirPlayBridge::extract_next_request_(TargetRuntime &target, RtspRequest &request) {
  // Interleaved RTP over TCP packets start with '$' and have a 2-byte length.
  while (target.buffer.size() >= 4 && target.buffer[0] == '$') {
    const uint16_t payload_len = (static_cast<uint8_t>(target.buffer[2]) << 8) | static_cast<uint8_t>(target.buffer[3]);
    const size_t frame_len = static_cast<size_t>(4 + payload_len);
    if (target.buffer.size() < frame_len) {
      return false;
    }
    target.buffer.erase(0, frame_len);
  }

  const size_t header_end = target.buffer.find("\r\n\r\n");
  if (header_end == std::string::npos) {
    return false;
  }

  std::string header_blob = target.buffer.substr(0, header_end);
  std::istringstream stream(header_blob);
  std::string line;

  if (!std::getline(stream, line)) {
    target.buffer.erase(0, header_end + 4);
    return false;
  }
  line = trim_(line);
  {
    std::istringstream first_line(line);
    first_line >> request.method >> request.uri;
  }
  if (request.method.empty()) {
    target.buffer.erase(0, header_end + 4);
    return false;
  }

  request.headers.clear();
  while (std::getline(stream, line)) {
    line = trim_(line);
    if (line.empty()) {
      continue;
    }
    const size_t colon = line.find(':');
    if (colon == std::string::npos) {
      continue;
    }
    std::string key = to_lower_(trim_(line.substr(0, colon)));
    std::string value = trim_(line.substr(colon + 1));
    request.headers[key] = value;
  }

  size_t content_len = 0;
  auto content_it = request.headers.find("content-length");
  if (content_it != request.headers.end()) {
    content_len = static_cast<size_t>(atoi(content_it->second.c_str()));
  }

  const size_t total_len = header_end + 4 + content_len;
  if (target.buffer.size() < total_len) {
    return false;
  }

  request.body = target.buffer.substr(header_end + 4, content_len);
  target.buffer.erase(0, total_len);
  return true;
}

void AirPlayBridge::handle_request_(TargetRuntime &target, const RtspRequest &request) {
  const auto cseq_it = request.headers.find("cseq");
  const std::string cseq = cseq_it != request.headers.end() ? cseq_it->second : "1";

  std::map<std::string, std::string> headers{
      {"Server", "ESPHome AirPlay Bridge"},
      {"Audio-Jack-Status", "connected; type=analog"},
  };

  if (request.method == "OPTIONS") {
    headers["Public"] = "ANNOUNCE, SETUP, RECORD, PAUSE, FLUSH, TEARDOWN, SET_PARAMETER, GET_PARAMETER";
    this->send_simple_ok_(target, cseq, headers);
    return;
  }

  if (request.method == "ANNOUNCE") {
    target.announce_sdp = request.body;
    this->send_simple_ok_(target, cseq, headers);
    return;
  }

  if (request.method == "SETUP") {
    if (target.session_id.empty()) {
      target.session_id = str_snprintf("%08X", random_uint32());
    }
    headers["Session"] = target.session_id;
    headers["Transport"] = "RTP/AVP/TCP;unicast;interleaved=0-1;mode=record";
    this->send_simple_ok_(target, cseq, headers);
    return;
  }

  if (request.method == "RECORD") {
    headers["Session"] = target.session_id;
    headers["RTP-Info"] = "seq=0;rtptime=0";
    this->start_stream_(target);
    this->send_simple_ok_(target, cseq, headers);
    return;
  }

  if (request.method == "FLUSH") {
    headers["Session"] = target.session_id;
    this->stop_stream_(target);
    this->send_simple_ok_(target, cseq, headers);
    return;
  }

  if (request.method == "SET_PARAMETER") {
    headers["Session"] = target.session_id;
    auto ct_it = request.headers.find("content-type");
    const std::string content_type = ct_it != request.headers.end() ? to_lower_(ct_it->second) : "";
    if (content_type.find("text/parameters") != std::string::npos) {
      std::istringstream body_stream(request.body);
      std::string parameter;
      while (std::getline(body_stream, parameter)) {
        parameter = trim_(parameter);
        if (parameter.rfind("volume:", 0) == 0) {
          const float airplay_db = static_cast<float>(atof(parameter.substr(7).c_str()));
          const float volume = db_to_volume_(airplay_db);
          this->apply_volume_(target, volume);
        }
      }
    }
    this->send_simple_ok_(target, cseq, headers);
    return;
  }

  if (request.method == "GET_PARAMETER") {
    headers["Content-Type"] = "text/parameters";
    headers["Session"] = target.session_id;
    const float db = target.last_volume <= 0.0001f ? -144.0f : 20.0f * log10f(target.last_volume);
    const std::string body = str_snprintf("volume: %.2f\r\n", db);
    this->send_response_(target, 200, cseq, headers, body);
    return;
  }

  if (request.method == "TEARDOWN") {
    headers["Session"] = target.session_id;
    this->stop_stream_(target);
    this->send_simple_ok_(target, cseq, headers);
#ifdef USE_ARDUINO
    target.client.stop();
#endif
#ifdef USE_ESP_IDF
    if (target.client_fd >= 0) {
      close(target.client_fd);
      target.client_fd = -1;
    }
#endif
    return;
  }

  this->send_response_(target, 501, cseq, headers);
}

void AirPlayBridge::send_response_(TargetRuntime &target, int status_code, const std::string &cseq,
                                   const std::map<std::string, std::string> &headers, const std::string &body) {
  std::string response = "RTSP/1.0 " + std::to_string(status_code) + " " + status_message_(status_code) + "\r\n";
  response += "CSeq: " + cseq + "\r\n";
  for (const auto &kv : headers) {
    response += kv.first + ": " + kv.second + "\r\n";
  }
  if (!body.empty()) {
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
  }
  response += "\r\n";
  response += body;

#ifdef USE_ARDUINO
  target.client.print(response.c_str());
#endif
#ifdef USE_ESP_IDF
  if (target.client_fd >= 0) {
    (void) send(target.client_fd, response.data(), response.size(), 0);
  }
#endif
}

void AirPlayBridge::send_simple_ok_(TargetRuntime &target, const std::string &cseq,
                                    const std::map<std::string, std::string> &headers) {
  this->send_response_(target, 200, cseq, headers);
}

void AirPlayBridge::start_stream_(TargetRuntime &target) {
  target.streaming = true;

  auto call = target.spec.player->make_call();
  const std::string media_url = this->render_media_url_(target);
  if (!media_url.empty()) {
    call.set_media_url(media_url);
  }
  call.set_command(media_player::MEDIA_PLAYER_COMMAND_PLAY);
  call.perform();
}

void AirPlayBridge::stop_stream_(TargetRuntime &target) {
  if (!target.streaming) {
    return;
  }
  target.streaming = false;
  auto call = target.spec.player->make_call();
  call.set_command(media_player::MEDIA_PLAYER_COMMAND_STOP);
  call.perform();
}

void AirPlayBridge::apply_volume_(TargetRuntime &target, float volume) {
  target.last_volume = clamp(volume, 0.0f, 1.0f);
  auto call = target.spec.player->make_call();
  call.set_volume(target.last_volume);
  call.perform();
}

std::string AirPlayBridge::render_media_url_(const TargetRuntime &target) const {
  if (this->media_url_template_.empty()) {
    return "";
  }

  std::string out = this->media_url_template_;
  std::string ip;
  auto ip_addresses = network::get_ip_addresses();
  for (const auto &candidate : ip_addresses) {
    if (candidate.is_set()) {
      ip = candidate.str();
      break;
    }
  }
  if (ip.empty()) {
    ip = network::get_use_address();
  }

  auto replace_all = [](std::string &source, const std::string &from, const std::string &to) {
    if (from.empty()) {
      return;
    }
    size_t start_pos = 0;
    while ((start_pos = source.find(from, start_pos)) != std::string::npos) {
      source.replace(start_pos, from.length(), to);
      start_pos += to.length();
    }
  };

  replace_all(out, "{ip}", ip);
  replace_all(out, "{port}", std::to_string(target.spec.port));
  replace_all(out, "{target}", target.spec.name);
  replace_all(out, "{session}", target.session_id);
  return out;
}

std::string AirPlayBridge::trim_(const std::string &value) {
  const char *whitespace = " \r\n\t";
  const size_t start = value.find_first_not_of(whitespace);
  if (start == std::string::npos) {
    return "";
  }
  const size_t end = value.find_last_not_of(whitespace);
  return value.substr(start, end - start + 1);
}

std::string AirPlayBridge::to_lower_(const std::string &value) {
  std::string out = value;
  std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return out;
}

float AirPlayBridge::db_to_volume_(float db) {
  if (db <= -100.0f) {
    return 0.0f;
  }
  return clamp(powf(10.0f, db / 20.0f), 0.0f, 1.0f);
}

std::string AirPlayBridge::status_message_(int status_code) {
  switch (status_code) {
    case 200:
      return "OK";
    case 400:
      return "Bad Request";
    case 454:
      return "Session Not Found";
    case 500:
      return "Internal Server Error";
    case 501:
      return "Not Implemented";
    default:
      return "OK";
  }
}
}  // namespace airplay_bridge
}  // namespace esphome
