#pragma once

#include "esphome/components/media_player/media_player.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/components/network/util.h"

#ifdef USE_ESP32
#include <mdns.h>
#endif
#ifdef USE_ARDUINO
#include <WiFi.h>
#ifdef USE_ESP8266
#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#endif
#endif

#ifdef USE_ESP_IDF
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#endif

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace esphome {
namespace speaker {
class Speaker;
}  // namespace speaker
}  // namespace esphome

namespace esphome {
namespace airplay_bridge {

class AirPlayBridge : public Component {
 public:
  void set_port_base(uint16_t port_base) { this->port_base_ = port_base; }
  void set_media_url_template(const std::string &media_url_template) { this->media_url_template_ = media_url_template; }
  void set_output_sample_rate(uint32_t rate) { this->output_sample_rate_ = rate; }
  void add_target(media_player::MediaPlayer *player, const std::string &name, esphome::Component *speaker_component);

  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override;

 protected:
  struct TargetSpec {
    media_player::MediaPlayer *player{nullptr};
    esphome::speaker::Speaker *speaker{nullptr};
    std::string name;
    uint16_t port{0};
  };

  struct RtspRequest {
    std::string method;
    std::string uri;
    std::map<std::string, std::string> headers;
    std::string body;
  };

  struct TargetRuntime {
    TargetSpec spec;
#ifdef USE_ARDUINO
    std::unique_ptr<WiFiServer> server;
    WiFiClient client;
#endif
#ifdef USE_ESP_IDF
    int server_fd{-1};
    int client_fd{-1};
#endif
    std::string buffer;
    std::string session_id;
    std::string announce_sdp;
    float last_volume{0.5f};
    bool streaming{false};
#ifdef USE_ESP_IDF
    void *alac_decoder{nullptr};
    std::string alac_config;
    bool alac_initialized{false};
    std::vector<uint8_t> pcm_buffer;
    size_t resample_phase{0};
#endif
  };

  std::vector<TargetSpec> target_specs_{};
  std::vector<TargetRuntime> runtimes_{};
  uint16_t port_base_{7000};
  std::string media_url_template_{};
  uint32_t output_sample_rate_{16000};
  std::string device_id_colon_{};
  std::string device_id_raop_{};
  bool mdns_ready_{false};

  void setup_runtime_();
  bool setup_mdns_();
  void advertise_target_(const TargetRuntime &target);
  void handle_target_(TargetRuntime &target);
  bool extract_next_request_(TargetRuntime &target, RtspRequest &request);
  void handle_request_(TargetRuntime &target, const RtspRequest &request);
  void send_response_(TargetRuntime &target, int status_code, const std::string &cseq,
                      const std::map<std::string, std::string> &headers, const std::string &body = "");
  void send_simple_ok_(TargetRuntime &target, const std::string &cseq,
                       const std::map<std::string, std::string> &headers = {});
  static std::string trim_(const std::string &value);
  static std::string to_lower_(const std::string &value);
  static float db_to_volume_(float db);
  static std::string status_message_(int status_code);
  std::string render_media_url_(const TargetRuntime &target) const;
  void start_stream_(TargetRuntime &target);
  void stop_stream_(TargetRuntime &target);
  void apply_volume_(TargetRuntime &target, float volume);
#ifdef USE_ESP_IDF
  void process_rtp_audio_(TargetRuntime &target, const uint8_t *data, size_t len);
  void process_interleaved_(TargetRuntime &target);
  bool parse_alac_config_from_sdp_(TargetRuntime &target);
  void resample_and_play_(TargetRuntime &target);
#endif
};

}  // namespace airplay_bridge
}  // namespace esphome
