#include "transport/ble_peripheral.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

#include <FastLED.h>
#include <esp_ota_ops.h>

#include "apps/app_ambient.h"
#include "config/hw_config.h"
#include "config/settings.h"
#include "engine/mode_manager.h"
#include "input/input_manager.h"
#include "input/mpu_input.h"
#include "input/remote_input.h"

namespace {

constexpr uint32_t kAdvTimeoutMs = 0;  // 0 = advertise forever

NimBLEServer*         s_server    = nullptr;
NimBLEService*        s_service   = nullptr;
NimBLECharacteristic* s_info_chr  = nullptr;
NimBLECharacteristic* s_ctrl_chr  = nullptr;
NimBLECharacteristic* s_tel_chr   = nullptr;
NimBLECharacteristic* s_mode_chr  = nullptr;
NimBLECharacteristic* s_cfg_chr   = nullptr;
NimBLECharacteristic* s_amb_chr   = nullptr;
NimBLECharacteristic* s_ota_ctl_chr = nullptr;
NimBLECharacteristic* s_ota_data_chr = nullptr;
NimBLECharacteristic* s_assign_chr = nullptr;

// Map BLE connection handle -> assigned player slot (0..3) or
// TWANG_PLAYER_SPECTATOR. Plain array keyed by slot because the number of
// simultaneous connections is small.
struct Peer {
    bool     in_use      = false;
    uint16_t conn_handle = 0xFFFF;
    uint8_t  player_slot = TWANG_PLAYER_SPECTATOR;
};
Peer s_peers[TWANG_MAX_PLAYERS + 2];   // phones + joystick headroom

void ota_on_ble_disconnect();

uint8_t connected_count() {
    uint8_t n = 0;
    for (auto& p : s_peers) if (p.in_use) ++n;
    return n;
}

Peer* find_peer_by_handle(uint16_t handle) {
    for (auto& p : s_peers) if (p.in_use && p.conn_handle == handle) return &p;
    return nullptr;
}

Peer* find_free_peer() {
    for (auto& p : s_peers) if (!p.in_use) return &p;
    return nullptr;
}

// Returns the next available player slot under the current policy, or
// TWANG_PLAYER_SPECTATOR when the device is full / configured so this
// central cannot play.
uint8_t pick_slot_for_new_peer() {
    const uint8_t mode        = g_settings.input_mode;
    const bool    joystick_ok = mpu_input_probed_present();

    if (mode == INPUT_JOYSTICK_ONLY) return TWANG_PLAYER_SPECTATOR;

    // In AUTO with a joystick present, slot 0 is reserved for the joystick.
    const uint8_t first_phone_slot =
        (mode == INPUT_AUTO && joystick_ok) ? 1 : 0;

    for (uint8_t slot = first_phone_slot; slot < TWANG_MAX_PLAYERS; ++slot) {
        bool taken = false;
        for (auto& p : s_peers) {
            if (p.in_use && p.player_slot == slot) { taken = true; break; }
        }
        if (!taken) return slot;
    }
    return TWANG_PLAYER_SPECTATOR;
}

// -- Server callbacks ------------------------------------------------------
class ServerCallbacks : public NimBLEServerCallbacks {
  public:
    void onConnect(NimBLEServer* /*server*/, ble_gap_conn_desc* desc) override {
        Peer* peer = find_free_peer();
        if (peer == nullptr) {
            Serial.printf("[ble] peer table full, rejecting handle=%u\r\n",
                          desc->conn_handle);
            return;
        }
        peer->in_use      = true;
        peer->conn_handle = desc->conn_handle;
        peer->player_slot = pick_slot_for_new_peer();

        if (peer->player_slot != TWANG_PLAYER_SPECTATOR) {
            input_manager::on_phone_connect(peer->conn_handle, peer->player_slot);
        }
        Serial.printf("[ble] central %u connected -> slot=%u (%d live)\r\n",
                      peer->conn_handle, peer->player_slot, connected_count());

        // Keep advertising so additional centrals can join up to the cap.
        NimBLEDevice::getAdvertising()->start(kAdvTimeoutMs);
    }

    void onDisconnect(NimBLEServer* server, ble_gap_conn_desc* desc) override {
        Peer* peer = find_peer_by_handle(desc->conn_handle);
        if (peer != nullptr) {
            if (peer->player_slot != TWANG_PLAYER_SPECTATOR) {
                input_manager::on_phone_disconnect(peer->conn_handle,
                                                   peer->player_slot);
            }
            Serial.printf("[ble] central %u disconnected (slot %u)\r\n",
                          peer->conn_handle, peer->player_slot);
            *peer = Peer{};
        } else {
            Serial.printf("[ble] unknown disconnect handle=%u\r\n",
                          desc->conn_handle);
        }
        ota_on_ble_disconnect();
        server->startAdvertising();
    }
};
ServerCallbacks s_server_cb;

// -- OTA (BLE stream to esp_ota_ops) ---------------------------------------
// CRC-32 (Ethernet / PNG): poly 0xEDB88320, init 0xFFFFFFFF, xorout 0xFFFFFFFF
static uint32_t ota_crc32_update(uint32_t crc, const uint8_t* p, size_t n) {
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; ++i) {
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1));
        }
    }
    return crc;
}

esp_ota_handle_t        s_ota_handle     = 0;
const esp_partition_t*  s_ota_part       = nullptr;
uint32_t                s_ota_expected   = 0;
uint32_t                s_ota_received   = 0;
uint32_t                s_ota_crc_expect = 0;
uint32_t                s_ota_crc_acc    = 0xFFFFFFFFu;
uint8_t                 s_ota_wire_state = OTA_WIRE_IDLE;
uint8_t                 s_ota_last_err   = OTA_ERR_NONE;

void ota_publish_status() {
    if (s_ota_ctl_chr == nullptr) return;
    OtaStatusPacket st{};
    st.version        = TWANG_OTA_CTRL_VERSION;
    st.state          = s_ota_wire_state;
    st.last_error     = s_ota_last_err;
    st.bytes_received = s_ota_received;
    st.expected_size  = s_ota_expected;
    s_ota_ctl_chr->setValue(reinterpret_cast<const uint8_t*>(&st), sizeof(st));
}

void ota_cleanup_esp_handle() {
    if (s_ota_handle != 0) {
        esp_ota_abort(s_ota_handle);
        s_ota_handle = 0;
    }
    s_ota_part       = nullptr;
    s_ota_expected   = 0;
    s_ota_received   = 0;
    s_ota_crc_expect = 0;
    s_ota_crc_acc    = 0xFFFFFFFFu;
}

// User ABORT opcode or disconnect mid-transfer: return to idle, clear error.
void ota_session_abort() {
    ota_cleanup_esp_handle();
    s_ota_last_err   = OTA_ERR_NONE;
    s_ota_wire_state = OTA_WIRE_IDLE;
    ota_publish_status();
}

void ota_fail(uint8_t err) {
    ota_cleanup_esp_handle();
    s_ota_last_err   = err;
    s_ota_wire_state = OTA_WIRE_ERROR;
    Serial.printf("[ble ota] error %u\r\n", err);
    ota_publish_status();
}

void ota_on_ble_disconnect() {
    if (s_ota_wire_state == OTA_WIRE_RECEIVING) {
        Serial.println("[ble ota] aborted (disconnect)");
        ota_session_abort();
    }
}

bool ota_allow_begin() {
    if (mode_manager::current_mode() == MODE_GAME_TWANG) {
        return false;
    }
    return true;
}

void ota_on_begin(const OtaBeginPacket& pkt) {
    if (s_ota_handle != 0 || s_ota_wire_state == OTA_WIRE_RECEIVING) {
        ota_fail(OTA_ERR_INVALID);
        return;
    }
    if (!ota_allow_begin()) {
        s_ota_last_err   = OTA_ERR_BUSY_GAME;
        s_ota_wire_state = OTA_WIRE_ERROR;
        ota_publish_status();
        return;
    }
    if (pkt.image_size == 0) {
        ota_fail(OTA_ERR_INVALID);
        return;
    }

    s_ota_part = esp_ota_get_next_update_partition(nullptr);
    if (s_ota_part == nullptr) {
        ota_fail(OTA_ERR_BEGIN);
        return;
    }
    if (pkt.image_size > s_ota_part->size) {
        ota_fail(OTA_ERR_SIZE);
        return;
    }

    s_ota_expected   = pkt.image_size;
    s_ota_crc_expect = pkt.image_crc32;
    s_ota_received   = 0;
    s_ota_crc_acc    = 0xFFFFFFFFu;
    s_ota_last_err   = OTA_ERR_NONE;

    esp_err_t e = esp_ota_begin(s_ota_part, pkt.image_size, &s_ota_handle);
    if (e != ESP_OK) {
        Serial.printf("[ble ota] esp_ota_begin failed: %x\r\n", (unsigned)e);
        ota_fail(OTA_ERR_BEGIN);
        return;
    }
    s_ota_wire_state = OTA_WIRE_RECEIVING;
    ota_publish_status();
    Serial.printf("[ble ota] begin size=%u crc=%08x\r\n",
                  (unsigned)pkt.image_size, (unsigned)pkt.image_crc32);
}

void ota_on_data(const uint8_t* data, size_t len) {
    if (s_ota_wire_state != OTA_WIRE_RECEIVING || s_ota_handle == 0) {
        return;
    }
    if (len == 0) return;
    if (s_ota_received + len > s_ota_expected) {
        ota_fail(OTA_ERR_SIZE);
        return;
    }
    esp_err_t e = esp_ota_write(s_ota_handle, data, len);
    if (e != ESP_OK) {
        Serial.printf("[ble ota] esp_ota_write failed: %x\r\n", (unsigned)e);
        ota_fail(OTA_ERR_FLASH);
        return;
    }
    s_ota_crc_acc = ota_crc32_update(s_ota_crc_acc, data, len);
    s_ota_received += static_cast<uint32_t>(len);
}

void ota_on_end() {
    if (s_ota_wire_state != OTA_WIRE_RECEIVING || s_ota_handle == 0) {
        ota_fail(OTA_ERR_INVALID);
        return;
    }
    if (s_ota_received != s_ota_expected) {
        ota_fail(OTA_ERR_SIZE);
        return;
    }
    const uint32_t crc_final = ~s_ota_crc_acc;
    if (crc_final != s_ota_crc_expect) {
        Serial.printf("[ble ota] crc mismatch got %08x want %08x\r\n",
                      (unsigned)crc_final, (unsigned)s_ota_crc_expect);
        ota_fail(OTA_ERR_CRC);
        return;
    }
    esp_err_t e = esp_ota_end(s_ota_handle);
    s_ota_handle = 0;
    if (e != ESP_OK) {
        Serial.printf("[ble ota] esp_ota_end failed: %x\r\n", (unsigned)e);
        ota_fail(OTA_ERR_END);
        return;
    }
    e = esp_ota_set_boot_partition(s_ota_part);
    if (e != ESP_OK) {
        Serial.printf("[ble ota] set_boot_partition failed: %x\r\n", (unsigned)e);
        ota_fail(OTA_ERR_END);
        return;
    }
    Serial.println("[ble ota] success, restarting");
    delay(100);
    esp_restart();
}

class OtaControlCallbacks : public NimBLECharacteristicCallbacks {
  public:
    void onRead(NimBLECharacteristic* chr) override {
        OtaStatusPacket st{};
        st.version        = TWANG_OTA_CTRL_VERSION;
        st.state          = s_ota_wire_state;
        st.last_error     = s_ota_last_err;
        st.bytes_received = s_ota_received;
        st.expected_size  = s_ota_expected;
        chr->setValue(reinterpret_cast<const uint8_t*>(&st), sizeof(st));
    }
    void onWrite(NimBLECharacteristic* chr) override {
        std::string data = chr->getValue();
        if (data.size() == sizeof(OtaBeginPacket)) {
            OtaBeginPacket pkt{};
            memcpy(&pkt, data.data(), sizeof(pkt));
            if (pkt.version != TWANG_OTA_CTRL_VERSION ||
                pkt.opcode != OTA_OP_BEGIN) {
                ota_fail(OTA_ERR_INVALID);
                return;
            }
            ota_on_begin(pkt);
            return;
        }
        if (data.size() == sizeof(OtaSimpleCtrlPacket)) {
            OtaSimpleCtrlPacket pkt{};
            memcpy(&pkt, data.data(), sizeof(pkt));
            if (pkt.version != TWANG_OTA_CTRL_VERSION) {
                ota_fail(OTA_ERR_INVALID);
                return;
            }
            if (pkt.opcode == OTA_OP_END) {
                ota_on_end();
                return;
            }
            if (pkt.opcode == OTA_OP_ABORT) {
                s_ota_last_err   = OTA_ERR_NONE;
                s_ota_wire_state = OTA_WIRE_IDLE;
                ota_session_abort();
                return;
            }
            ota_fail(OTA_ERR_INVALID);
            return;
        }
        ota_fail(OTA_ERR_INVALID);
    }
};
OtaControlCallbacks s_ota_control_cb;

class OtaDataCallbacks : public NimBLECharacteristicCallbacks {
  public:
    void onWrite(NimBLECharacteristic* chr) override {
        std::string data = chr->getValue();
        ota_on_data(reinterpret_cast<const uint8_t*>(data.data()), data.size());
    }
};
OtaDataCallbacks s_ota_data_cb;

// -- Control characteristic (write-without-response) -----------------------
class ControlCallbacks : public NimBLECharacteristicCallbacks {
  public:
    void onWrite(NimBLECharacteristic* chr, ble_gap_conn_desc* desc) override {
        Peer* peer = find_peer_by_handle(desc->conn_handle);
        if (peer == nullptr) return;
        if (peer->player_slot == TWANG_PLAYER_SPECTATOR) {
            // Spectators' control writes are silently dropped.
            return;
        }
        std::string data = chr->getValue();
        if (data.size() != sizeof(ControlPacket)) return;
        ControlPacket pkt{};
        memcpy(&pkt, data.data(), sizeof(pkt));
        if (pkt.version != TWANG_PROTO_VERSION) return;
        input_manager::on_phone_control(peer->player_slot, pkt, millis());
    }
};
ControlCallbacks s_control_cb;

// -- Mode characteristic (read/write) --------------------------------------
class ModeCallbacks : public NimBLECharacteristicCallbacks {
  public:
    void onWrite(NimBLECharacteristic* chr) override {
        std::string data = chr->getValue();
        if (data.size() != sizeof(ModePacket)) return;
        ModePacket req{};
        memcpy(&req, data.data(), sizeof(req));
        if (!mode_manager::request_mode(static_cast<ModeId>(req.mode_id), req.submode_id)) {
            Serial.printf("[ble] unsupported mode request %u\r\n", req.mode_id);
            return;
        }
        chr->setValue(reinterpret_cast<const uint8_t*>(&req), sizeof(req));
    }
};
ModeCallbacks s_mode_cb;

// -- Config characteristic (read/write) ------------------------------------
ConfigPacket build_config_snapshot() {
    ConfigPacket pkt{};
    pkt.version           = 1;
    pkt.input_mode        = g_settings.input_mode;
    pkt.led_count         = g_settings.led_count;
    pkt.led_brightness    = g_settings.led_brightness;
    pkt.joystick_deadzone = g_settings.joystick_deadzone;
    pkt.attack_threshold  = g_settings.attack_threshold;
    pkt.lives_per_level   = g_settings.lives_per_level;
    pkt.audio_volume      = g_settings.audio_volume;
    pkt.flags             = CFG_PERSIST_TO_NVS;
    pkt.reserved          = 0;
    return pkt;
}

template <typename T>
T clamp_u(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }

void apply_config(const ConfigPacket& in) {
    const bool persist = (in.flags & CFG_PERSIST_TO_NVS) != 0;

    uint8_t  im   = in.input_mode <= INPUT_MULTIPLAYER ? in.input_mode : INPUT_AUTO;
    uint16_t lc   = clamp_u<uint16_t>(in.led_count, MIN_LEDS, MAX_LEDS);
    uint8_t  lb   = clamp_u<uint8_t >(in.led_brightness, MIN_BRIGHTNESS, MAX_BRIGHTNESS);
    uint8_t  dz   = clamp_u<uint8_t >(in.joystick_deadzone,
                                      MIN_JOYSTICK_DEADZONE, MAX_JOYSTICK_DEADZONE);
    uint16_t at   = clamp_u<uint16_t>(in.attack_threshold,
                                      MIN_ATTACK_THRESHOLD, MAX_ATTACK_THRESHOLD);
    uint8_t  lv   = clamp_u<uint8_t >(in.lives_per_level,
                                      MIN_LIVES_PER_LEVEL, MAX_LIVES_PER_LEVEL);
    uint8_t  vol  = clamp_u<uint8_t >(in.audio_volume, MIN_VOLUME, MAX_VOLUME);

    g_settings.input_mode        = im;
    g_settings.led_count         = lc;
    g_settings.led_brightness    = lb;
    g_settings.joystick_deadzone = dz;
    g_settings.attack_threshold  = at;
    g_settings.lives_per_level   = lv;
    g_settings.audio_volume      = vol;

    FastLED.setBrightness(lb);

    if (persist) twang_settings::save();

    Serial.printf("[ble] config write: mode=%u leds=%u bright=%u dz=%u atk=%u "
                  "lives=%u vol=%u persist=%d\r\n",
                  im, lc, lb, dz, at, lv, vol, persist);
}

class ConfigCallbacks : public NimBLECharacteristicCallbacks {
  public:
    void onRead(NimBLECharacteristic* chr) override {
        const ConfigPacket snap = build_config_snapshot();
        chr->setValue(reinterpret_cast<const uint8_t*>(&snap), sizeof(snap));
    }
    void onWrite(NimBLECharacteristic* chr) override {
        std::string data = chr->getValue();
        if (data.size() != sizeof(ConfigPacket)) return;
        ConfigPacket pkt{};
        memcpy(&pkt, data.data(), sizeof(pkt));
        if (pkt.version != 1) return;
        apply_config(pkt);
        const ConfigPacket echo = build_config_snapshot();
        chr->setValue(reinterpret_cast<const uint8_t*>(&echo), sizeof(echo));
    }
};
ConfigCallbacks s_config_cb;

// -- Ambient characteristic (read/write) -----------------------------------
class AmbientCallbacks : public NimBLECharacteristicCallbacks {
  public:
    void onRead(NimBLECharacteristic* chr) override {
        const AmbientPacket snap = app_ambient::snapshot();
        chr->setValue(reinterpret_cast<const uint8_t*>(&snap), sizeof(snap));
    }
    void onWrite(NimBLECharacteristic* chr) override {
        std::string data = chr->getValue();
        if (data.size() != sizeof(AmbientPacket)) return;
        AmbientPacket pkt{};
        memcpy(&pkt, data.data(), sizeof(pkt));
        if (pkt.version != 1) return;
        app_ambient::apply(pkt);
        const AmbientPacket echo = app_ambient::snapshot();
        chr->setValue(reinterpret_cast<const uint8_t*>(&echo), sizeof(echo));
    }
};
AmbientCallbacks s_ambient_cb;

// -- Assignment characteristic (per-connection read) -----------------------
class AssignmentCallbacks : public NimBLECharacteristicCallbacks {
  public:
    // IMPORTANT: NimBLE serialises read responses to the attribute value set
    // in this callback, so returning a different value per `desc` is safe.
    void onRead(NimBLECharacteristic* chr, ble_gap_conn_desc* desc) override {
        uint8_t payload[2] = {TWANG_PLAYER_SPECTATOR, 0};
        Peer* peer = find_peer_by_handle(desc->conn_handle);
        if (peer != nullptr) {
            payload[0] = peer->player_slot;
        }
        payload[1] = input_manager::active_player_count();
        chr->setValue(payload, sizeof(payload));
    }
};
AssignmentCallbacks s_assignment_cb;

String make_device_name() {
    uint64_t mac = ESP.getEfuseMac();
    uint16_t suffix = static_cast<uint16_t>(mac & 0xFFFF);
    char buf[12];
    snprintf(buf, sizeof(buf), "TWANG-%04X", suffix);
    return String(buf);
}

void write_info_characteristic() {
    InfoPacket info{};
    info.proto_version = TWANG_PROTO_VERSION;
    info.fw_major      = TWANG_FW_MAJOR;
    info.fw_minor      = TWANG_FW_MINOR;
    info.fw_patch      = TWANG_FW_PATCH;
    info.board_id      = static_cast<uint32_t>(ESP.getEfuseMac() & 0xFFFFFFFFULL);
    info.capabilities  = CAP_HAS_GAME | CAP_HAS_MPU_FALLBACK |
                         CAP_HAS_AMBIENT_MODES | CAP_HAS_NPLAYER |
                         CAP_HAS_OTA;
    info.data_pin      = TWANG_DATA_PIN;
    s_info_chr->setValue(reinterpret_cast<const uint8_t*>(&info), sizeof(info));
}

void write_initial_mode_value() {
    ModePacket mp{static_cast<uint8_t>(mode_manager::current_mode()), 0};
    s_mode_chr->setValue(reinterpret_cast<const uint8_t*>(&mp), sizeof(mp));
}

} // namespace

namespace ble_peripheral {

void begin() {
    const String name = make_device_name();
    NimBLEDevice::init(std::string(name.c_str()));
    NimBLEDevice::setPower(ESP_PWR_LVL_P7);
    NimBLEDevice::setMTU(512);

    s_server = NimBLEDevice::createServer();
    s_server->setCallbacks(&s_server_cb);

    s_service = s_server->createService(TWANG_SVC_UUID);

    s_info_chr   = s_service->createCharacteristic(
        TWANG_CHR_INFO_UUID, NIMBLE_PROPERTY::READ);
    s_ctrl_chr   = s_service->createCharacteristic(
        TWANG_CHR_CONTROL_UUID, NIMBLE_PROPERTY::WRITE_NR);
    s_tel_chr    = s_service->createCharacteristic(
        TWANG_CHR_TELEMETRY_UUID, NIMBLE_PROPERTY::NOTIFY);
    s_mode_chr   = s_service->createCharacteristic(
        TWANG_CHR_MODE_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    s_cfg_chr    = s_service->createCharacteristic(
        TWANG_CHR_CONFIG_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    s_amb_chr    = s_service->createCharacteristic(
        TWANG_CHR_AMBIENT_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    s_ota_ctl_chr = s_service->createCharacteristic(
        TWANG_CHR_OTA_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE);
    s_ota_data_chr = s_service->createCharacteristic(
        TWANG_CHR_OTA_DATA_UUID, NIMBLE_PROPERTY::WRITE_NR);
    s_assign_chr = s_service->createCharacteristic(
        TWANG_CHR_ASSIGNMENT_UUID, NIMBLE_PROPERTY::READ);

    s_ctrl_chr   ->setCallbacks(&s_control_cb);
    s_mode_chr   ->setCallbacks(&s_mode_cb);
    s_cfg_chr    ->setCallbacks(&s_config_cb);
    s_amb_chr    ->setCallbacks(&s_ambient_cb);
    s_ota_ctl_chr->setCallbacks(&s_ota_control_cb);
    s_ota_data_chr->setCallbacks(&s_ota_data_cb);
    s_assign_chr ->setCallbacks(&s_assignment_cb);

    write_info_characteristic();
    write_initial_mode_value();

    {
        const ConfigPacket snap = build_config_snapshot();
        s_cfg_chr->setValue(reinterpret_cast<const uint8_t*>(&snap), sizeof(snap));
    }
    {
        const AmbientPacket snap = app_ambient::snapshot();
        s_amb_chr->setValue(reinterpret_cast<const uint8_t*>(&snap), sizeof(snap));
    }
    {
        uint8_t seed[2] = {TWANG_PLAYER_SPECTATOR, 0};
        s_assign_chr->setValue(seed, sizeof(seed));
    }
    ota_publish_status();

    s_service->start();

    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(TWANG_SVC_UUID);
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMinPreferred(0x12);
    adv->start(kAdvTimeoutMs);

    Serial.printf("[ble] advertising as %s (N-player capable)\r\n", name.c_str());
}

bool is_connected() { return connected_count() > 0; }

void send_telemetry(const TelemetryPacket& tel) {
    if (s_tel_chr == nullptr) return;
    if (connected_count() == 0) return;
    s_tel_chr->setValue(reinterpret_cast<const uint8_t*>(&tel), sizeof(tel));
    s_tel_chr->notify();   // broadcasts to every subscribed central
}

} // namespace ble_peripheral
