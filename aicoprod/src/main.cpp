#include <Arduino.h>
#include <M5Unified.h>
#include <ArduinoJson.h>

static constexpr uint8_t SYNC_BYTE = 0xAA;
static constexpr uint8_t MSG_MIC_PCM = 0x01;
static constexpr uint8_t MSG_SPK_PCM = 0x02;
static constexpr uint8_t MSG_JSON_UI = 0x03;
static constexpr uint8_t MSG_HEARTBEAT = 0x04;
static constexpr size_t MAX_PAYLOAD_LEN = 4096;

static constexpr uint32_t SAMPLE_RATE = 24000;
static constexpr size_t MIC_CHUNK_SAMPLES = 480;   // 20 ms @ 24 kHz
static constexpr size_t SPK_RING_SAMPLES = 24000;  // 1.0 s ring buffer in PSRAM
static constexpr size_t SPK_PLAY_CHUNK = 480;      // 20 ms playback blocks

enum RxState {
    WAIT_SYNC,
    WAIT_TYPE,
    WAIT_LEN_HI,
    WAIT_LEN_LO,
    WAIT_PAYLOAD
};

static RxState rx_state = WAIT_SYNC;
static uint8_t rx_type = 0;
static uint16_t rx_len = 0;
static uint16_t rx_idx = 0;
static uint8_t *rx_buf = nullptr;

static int16_t *mic_buf = nullptr;
static int16_t *spk_ring = nullptr;
static volatile size_t spk_head = 0;
static volatile size_t spk_tail = 0;
static int16_t spk_out_chunk[SPK_PLAY_CHUNK];

static bool is_recording = false;
static bool speak_back = true;
static String current_mode = "LOOP";
static String line1 = "BOOT: CORES3 READY";
static String line2 = "101 BPM | D MAJOR";
static String line3 = "WAITING FOR HOST...";

static unsigned long last_hb_ms = 0;
static bool ui_dirty = true;

static inline size_t spk_available() {
    size_t h = spk_head;
    size_t t = spk_tail;
    return (h >= t) ? (h - t) : (SPK_RING_SAMPLES - t + h);
}

static inline void spk_clear() {
    spk_head = 0;
    spk_tail = 0;
}

static void send_frame(uint8_t type, const uint8_t *payload, uint16_t len) {
    if (len > MAX_PAYLOAD_LEN) return;
    uint8_t header[4] = {
        SYNC_BYTE,
        type,
        static_cast<uint8_t>((len >> 8) & 0xFF),
        static_cast<uint8_t>(len & 0xFF)
    };
    Serial.write(header, 4);
    if (len > 0 && payload != nullptr) {
        Serial.write(payload, len);
    }
}

static void send_ui_event(const char *event_name) {
    JsonDocument doc;
    doc["event"] = event_name;
    doc["mode"] = current_mode;
    doc["speak_back"] = speak_back;
    doc["ptt"] = is_recording;
    char out[256];
    size_t n = serializeJson(doc, out, sizeof(out));
    send_frame(MSG_JSON_UI, reinterpret_cast<const uint8_t *>(out), static_cast<uint16_t>(n));
}

static void force_acoustic_cutoff() {
    spk_clear();
    M5.Speaker.stop();
}

static void enter_mic_mode() {
    if (is_recording) return;
    force_acoustic_cutoff();
    M5.Speaker.end();
    auto mic_cfg = M5.Mic.config();
    mic_cfg.sample_rate = SAMPLE_RATE;
    mic_cfg.stereo = false;
    mic_cfg.dma_buf_count = 8;
    mic_cfg.dma_buf_len = 256;
    M5.Mic.config(mic_cfg);
    M5.Mic.begin();
    is_recording = true;
    ui_dirty = true;
    send_ui_event("ptt_start");
}

static void enter_speaker_mode() {
    if (!is_recording) return;
    while (M5.Mic.isRecording()) {
        delay(1);
    }
    M5.Mic.end();
    auto spk_cfg = M5.Speaker.config();
    spk_cfg.sample_rate = SAMPLE_RATE;
    spk_cfg.stereo = false;
    spk_cfg.dma_buf_count = 8;
    spk_cfg.dma_buf_len = 256;
    M5.Speaker.config(spk_cfg);
    M5.Speaker.begin();
    M5.Speaker.setVolume(210);
    is_recording = false;
    ui_dirty = true;
    send_ui_event("ptt_stop");
}

static void render_ui() {
    M5.Display.startWrite();

    // Zone 1: Top Bar (y: 0..44)
    M5.Display.fillRect(0, 0, 320, 45, 0x1082);
    M5.Display.drawFastHLine(0, 44, 320, 0x4208);

    // Mode Badge Left (x: 6..172, y: 5..39)
    uint16_t mode_color = TFT_BLUE;
    if (current_mode == "ARRANGE") mode_color = TFT_PURPLE;
    else if (current_mode == "PERFORMANCE") mode_color = TFT_RED;
    M5.Display.fillRoundRect(6, 5, 166, 34, 6, mode_color);
    M5.Display.setTextColor(TFT_WHITE, mode_color);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextSize(2);
    M5.Display.drawString(current_mode, 89, 22);

    // Talkback / Live Silent Pill Right (x: 180..314, y: 5..39)
    uint16_t pill_color = speak_back ? 0x04A0 : 0xB000;
    const char *pill_label = speak_back ? "TALKBACK" : "LIVE SILENT";
    M5.Display.fillRoundRect(180, 5, 134, 34, 6, pill_color);
    M5.Display.setTextColor(TFT_WHITE, pill_color);
    M5.Display.setTextSize(1.5);
    M5.Display.drawString(pill_label, 247, 22);

    // Zone 2: 3-Line Telemetry Body (y: 45..179)
    M5.Display.fillRect(0, 45, 320, 135, TFT_BLACK);
    M5.Display.setTextDatum(ML_DATUM);
    M5.Display.setTextSize(2);

    M5.Display.setTextColor(TFT_CYAN, TFT_BLACK);
    M5.Display.drawString(line1.substring(0, 24), 12, 72);

    M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
    M5.Display.drawString(line2.substring(0, 24), 12, 112);

    M5.Display.setTextColor(0xAD55, TFT_BLACK);
    M5.Display.drawString(line3.substring(0, 24), 12, 152);

    // Zone 3: Full-Width HOLD TO SPEAK Button (y: 180..240)
    uint16_t ptt_bg = is_recording ? 0xD000 : 0x2124;
    uint16_t ptt_border = is_recording ? TFT_YELLOW : 0x6B4D;
    M5.Display.fillRect(0, 180, 320, 60, ptt_bg);
    M5.Display.drawRect(2, 181, 316, 58, ptt_border);
    M5.Display.setTextDatum(MC_DATUM);
    M5.Display.setTextColor(TFT_WHITE, ptt_bg);
    M5.Display.setTextSize(2);
    M5.Display.drawString(is_recording ? "LISTENING (RELEASE)" : "HOLD TO SPEAK", 160, 210);

    M5.Display.endWrite();
    ui_dirty = false;
}

static void handle_json_frame(const uint8_t *data, uint16_t len) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, data, len);
    if (err) return;

    if (!doc["mode"].isNull()) {
        String new_mode = doc["mode"].as<String>();
        new_mode.toUpperCase();
        if (new_mode != current_mode) {
            current_mode = new_mode;
            if (current_mode == "PERFORMANCE") {
                speak_back = false;
                force_acoustic_cutoff();
            }
            ui_dirty = true;
        }
    }

    if (!doc["speak_back"].isNull()) {
        bool sb = doc["speak_back"].as<bool>();
        if (sb != speak_back) {
            speak_back = sb;
            if (!speak_back) {
                force_acoustic_cutoff();
            }
            ui_dirty = true;
        }
    }

    if (!doc["line1"].isNull()) {
        line1 = doc["line1"].as<String>();
        ui_dirty = true;
    }
    if (!doc["line2"].isNull()) {
        line2 = doc["line2"].as<String>();
        ui_dirty = true;
    }
    if (!doc["line3"].isNull()) {
        line3 = doc["line3"].as<String>();
        ui_dirty = true;
    }
}

static void handle_speaker_pcm(const uint8_t *data, uint16_t len) {
    if (!speak_back || current_mode == "PERFORMANCE" || is_recording) {
        force_acoustic_cutoff();
        return;
    }
    size_t samples = len / sizeof(int16_t);
    const int16_t *pcm = reinterpret_cast<const int16_t *>(data);
    for (size_t i = 0; i < samples; ++i) {
        size_t next_head = (spk_head + 1) % SPK_RING_SAMPLES;
        if (next_head == spk_tail) {
            spk_tail = (spk_tail + 1) % SPK_RING_SAMPLES;
        }
        spk_ring[spk_head] = pcm[i];
        spk_head = next_head;
    }
}

static void process_serial_rx() {
    while (Serial.available() > 0) {
        uint8_t b = static_cast<uint8_t>(Serial.read());
        switch (rx_state) {
            case WAIT_SYNC:
                if (b == SYNC_BYTE) {
                    rx_state = WAIT_TYPE;
                }
                break;
            case WAIT_TYPE:
                rx_type = b;
                rx_state = WAIT_LEN_HI;
                break;
            case WAIT_LEN_HI:
                rx_len = static_cast<uint16_t>(b) << 8;
                rx_state = WAIT_LEN_LO;
                break;
            case WAIT_LEN_LO:
                rx_len |= static_cast<uint16_t>(b);
                if (rx_len > MAX_PAYLOAD_LEN) {
                    rx_state = WAIT_SYNC;
                } else if (rx_len == 0) {
                    rx_state = WAIT_SYNC;
                } else {
                    rx_idx = 0;
                    rx_state = WAIT_PAYLOAD;
                }
                break;
            case WAIT_PAYLOAD:
                rx_buf[rx_idx++] = b;
                if (rx_idx >= rx_len) {
                    if (rx_type == MSG_SPK_PCM) {
                        handle_speaker_pcm(rx_buf, rx_len);
                    } else if (rx_type == MSG_JSON_UI) {
                        handle_json_frame(rx_buf, rx_len);
                    }
                    rx_state = WAIT_SYNC;
                }
                break;
        }
    }
}

static void service_audio_io() {
    if (is_recording) {
        if (M5.Mic.isEnabled()) {
            if (M5.Mic.record(mic_buf, MIC_CHUNK_SAMPLES, SAMPLE_RATE)) {
                send_frame(MSG_MIC_PCM,
                           reinterpret_cast<const uint8_t *>(mic_buf),
                           MIC_CHUNK_SAMPLES * sizeof(int16_t));
            }
        }
    } else {
        if (!speak_back || current_mode == "PERFORMANCE") {
            if (M5.Speaker.isPlaying()) {
                force_acoustic_cutoff();
            }
            return;
        }
        size_t avail = spk_available();
        if (avail >= SPK_PLAY_CHUNK && M5.Speaker.isPlaying() < 2) {
            for (size_t i = 0; i < SPK_PLAY_CHUNK; ++i) {
                spk_out_chunk[i] = spk_ring[spk_tail];
                spk_tail = (spk_tail + 1) % SPK_RING_SAMPLES;
            }
            M5.Speaker.playRaw(spk_out_chunk, SPK_PLAY_CHUNK, SAMPLE_RATE, false, 1, 0);
        }
    }
}

static void handle_touch() {
    auto t = M5.Touch.getDetail();

    // Top-right [TALKBACK / LIVE SILENT] pill tap (x >= 180, y < 45)
    if (t.wasPressed() && t.x >= 180 && t.y < 45) {
        speak_back = !speak_back;
        if (!speak_back) {
            force_acoustic_cutoff();
        }
        ui_dirty = true;
        send_ui_event("toggle_talkback");
        return;
    }

    // Bottom zone HOLD TO SPEAK (y >= 180)
    bool touch_in_ptt = t.isPressed() && (t.y >= 180);
    if (touch_in_ptt && !is_recording) {
        enter_mic_mode();
    } else if (!t.isPressed() && is_recording) {
        enter_speaker_mode();
    }
}

void setup() {
    auto cfg = M5.config();
    cfg.internal_mic = true;
    cfg.internal_spk = true;
    M5.begin(cfg);

    Serial.setRxBufferSize(16384);
    Serial.setTxBufferSize(16384);
    Serial.begin(921600);

    rx_buf = static_cast<uint8_t *>(ps_malloc(MAX_PAYLOAD_LEN));
    mic_buf = static_cast<int16_t *>(ps_malloc(MIC_CHUNK_SAMPLES * sizeof(int16_t)));
    spk_ring = static_cast<int16_t *>(ps_malloc(SPK_RING_SAMPLES * sizeof(int16_t)));

    M5.Mic.end();
    auto spk_cfg = M5.Speaker.config();
    spk_cfg.sample_rate = SAMPLE_RATE;
    spk_cfg.stereo = false;
    spk_cfg.dma_buf_count = 8;
    spk_cfg.dma_buf_len = 256;
    M5.Speaker.config(spk_cfg);
    M5.Speaker.begin();
    M5.Speaker.setVolume(210);

    M5.Display.setRotation(1);
    M5.Display.fillScreen(TFT_BLACK);
    render_ui();
}

void loop() {
    M5.update();
    process_serial_rx();
    handle_touch();
    service_audio_io();

    unsigned long now = millis();
    if (now - last_hb_ms >= 1000) {
        last_hb_ms = now;
        uint32_t uptime_s = now / 1000;
        uint8_t hb[4] = {
            static_cast<uint8_t>((uptime_s >> 24) & 0xFF),
            static_cast<uint8_t>((uptime_s >> 16) & 0xFF),
            static_cast<uint8_t>((uptime_s >> 8) & 0xFF),
            static_cast<uint8_t>(uptime_s & 0xFF)
        };
        send_frame(MSG_HEARTBEAT, hb, 4);
    }

    if (ui_dirty) {
        render_ui();
    }
}
