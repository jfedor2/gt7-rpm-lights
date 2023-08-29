#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/board.h"
#include "tusb.h"

#include "hardware/flash.h"
#include "hardware/gpio.h"

#include "pico/cyw43_arch.h"
#include "pico/stdio.h"

#include "lwip/pbuf.h"
#include "lwip/udp.h"

#include "crc.h"
#include "salsa20.h"
#include "ws2812.h"

#define PERSISTED_CONFIG_SIZE 4096
#define CONFIG_OFFSET_IN_FLASH (PICO_FLASH_SIZE_BYTES - PERSISTED_CONFIG_SIZE)
#define FLASH_CONFIG_IN_MEMORY (((uint8_t*) XIP_BASE) + CONFIG_OFFSET_IN_FLASH)

#define SERVER_PORT 33739
#define OUR_PORT 33740

#define CONFIG_VERSION 1

typedef struct __attribute__((packed)) {
    uint8_t version;
    uint8_t flags;
    char wifi_ssid[20];
    char wifi_password[24];
    uint8_t playstation_ip[4];
    uint8_t nleds;
    uint8_t led_pin;
    uint8_t pattern;
    uint8_t color_scheme;
    uint8_t brightness;
    uint8_t start_percent;
    uint8_t end_percent;
    uint8_t reserved[3];
    uint32_t crc;
} config_t;

typedef struct __attribute__((packed)) {
    uint32_t magic;
    float position[3];
    float velocity[3];
    float rotation[3];
    float relative_orientation_to_north;
    float angular_velocity[3];
    float body_height;
    float engine_rpm;
    uint32_t whatever0;
    float gas_level;
    float gas_capacity;
    float meters_per_second;
    float turbo_boost;
    float oil_pressure;
    float water_temperature;
    float oil_temperature;
    float tire_fl_surface_temperature;
    float tire_fr_surface_temperature;
    float tire_rl_surface_temperature;
    float tire_rr_surface_temperature;
    uint32_t packet_id;
    uint16_t lap_count;
    uint16_t laps_in_race;
    uint32_t best_lap_time;
    uint32_t last_lap_time;
    uint32_t time_of_day_progression;
    uint16_t pre_race_start_position_or_quali_pos;
    uint16_t num_cars_at_pre_race;
    uint16_t min_alert_rpm;
    uint16_t max_alert_rpm;
    uint16_t calculated_max_speed;
    uint16_t flags;
    uint8_t current_and_suggested_gear;
    uint8_t throttle;
    uint8_t brake;
    uint8_t whatever1;
    float road_plane[3];
    float road_plane_distance;
    float wheel_rev_per_second[4];
    float tire_tire_radius[4];
    float tire_sus_height[4];
    uint32_t whatever2[8];
    float clutch_pedal;
    float clutch_engagement;
    float rpm_from_clutch_to_gearbox;
    float transmission_top_speed;
    float gear_ratio[8];
    uint32_t car_code;
} telemetry_t;

struct udp_pcb* pcb;
ip_addr_t server_address;
uint64_t last_heartbeat = 0;

int32_t rpm = 0;
bool blink = false;
bool in_car = false;
bool wifi_connected = false;

config_t config = {
    .version = CONFIG_VERSION,
    .flags = 0x01,
    .wifi_ssid = "",
    .wifi_password = "",
    .playstation_ip = { 0, 0, 0, 0 },
    .nleds = 16,
    .led_pin = 28,
    .pattern = 0,
    .color_scheme = 0,
    .brightness = 25,
    .start_percent = 85,
    .end_percent = 100,
    .reserved = { 0 },
    .crc = 0,
};

uint8_t buf[296];
uint8_t key[] = "Simulator Interface Packet GT7 v";
uint8_t nonce_[8];

void net_recv(void* arg, struct udp_pcb* pcb, struct pbuf* p, const ip_addr_t* addr, u16_t port) {
    if (p->len != 296) {
        pbuf_free(p);
        return;
    }
    memcpy(buf, p->payload, 296);
    pbuf_free(p);

    uint64_t nonce = *((uint32_t*) (buf + 64));
    nonce |= nonce << 32;
    nonce ^= 0xDEADBEAF;
    for (int i = 0; i < 8; i++) {
        nonce_[i] = nonce & 0xFF;
        nonce = nonce >> 8;
    }
    s20_crypt(key, S20_KEYLEN_256, nonce_, 0, buf, 296);
    telemetry_t* t = (telemetry_t*) buf;
    if (t->magic == 0x47375330) {
        rpm = 1024 * (t->engine_rpm - (t->min_alert_rpm * config.start_percent / 100)) / (t->min_alert_rpm * config.end_percent / 100 - (t->min_alert_rpm * config.start_percent / 100));
        if (rpm < 0) {
            rpm = 0;
        }
        if (rpm > 1023) {
            blink = true;
            rpm = 1023;
        } else {
            blink = false;
        }
        // blink = t->flags & (1 << 5);
        in_car = t->flags & 1;
    }
}

void net_init() {
    cyw43_arch_init();
    cyw43_arch_enable_sta_mode();
    if (strlen(config.wifi_ssid) > 0) {
        cyw43_arch_wifi_connect_async(config.wifi_ssid, config.wifi_password, CYW43_AUTH_WPA2_AES_PSK);
    }

    IP4_ADDR(&server_address, config.playstation_ip[0], config.playstation_ip[1], config.playstation_ip[2], config.playstation_ip[3]);
    pcb = udp_new();
    udp_bind(pcb, IP_ANY_TYPE, OUR_PORT);
    udp_recv(pcb, net_recv, NULL);
}

void send_heartbeat() {
    if (wifi_connected) {
        struct pbuf* p = pbuf_alloc(PBUF_TRANSPORT, 1, PBUF_RAM);
        uint8_t* req = (uint8_t*) p->payload;
        req[0] = 'A';
        udp_sendto(pcb, p, &server_address, SERVER_PORT);
        pbuf_free(p);
    }
}

void net_task() {
    wifi_connected = CYW43_LINK_UP == cyw43_tcpip_link_status(&cyw43_state, CYW43_ITF_STA);
    uint64_t now = time_us_64();
    if (now - last_heartbeat > 10000000) {
        send_heartbeat();
        last_heartbeat = now;
    }
}

void put_pixel_scaled(uint32_t pixel_grb) {
    uint32_t g = (pixel_grb >> 16) & 0xFF;
    uint32_t r = (pixel_grb >> 8) & 0xFF;
    uint32_t b = pixel_grb & 0xFF;
    g = g * config.brightness / 100;
    r = r * config.brightness / 100;
    b = b * config.brightness / 100;
    put_pixel(g << 16 | r << 8 | b);
}

void led_task() {
    if (config.nleds == 0) {
        return;
    }
    if (!wifi_connected) {
        uint32_t color = 0x0000FF;
        for (int i = 0; i < config.nleds; i++) {
            put_pixel_scaled(color);
        }
    } else if (!in_car) {
        uint32_t color = 0x000000;
        for (int i = 0; i < config.nleds; i++) {
            put_pixel_scaled(color);
        }
    } else if ((config.flags & 0x01) && blink) {
        uint32_t color = time_us_64() % 100000 > 50000 ? 0xFFFFFF : 0x000000;
        for (int i = 0; i < config.nleds; i++) {
            put_pixel_scaled(color);
        }
    } else {
        uint8_t nleds = (config.pattern == 1) ? (config.nleds / 2) : config.nleds;
        for (uint8_t i = 0; i < config.nleds; i++) {
            uint8_t j;
            if (config.pattern == 1) {
                j = i < nleds ? i : config.nleds - i - 1;
            } else {
                j = i;
            }
            uint32_t color = 0;
            uint32_t lit_mask = rpm > (j * 1024 / nleds) ? 0xFFFFFF : 0;
            switch (config.color_scheme) {
                case 0:
                    color = 0x00FF00;
                    break;
                case 1:
                    color = (j < nleds / 3) ? 0xFF0000 : ((j < nleds * 2 / 3) ? 0xFFFF00 : 0x00FF00);
                    break;
                case 2:
                    color = (rpm < 1024 / 3) ? 0xFF0000 : ((rpm < 1024 * 2 / 3) ? 0xFFFF00 : 0x00FF00);
                    break;
            }
            put_pixel_scaled(color & lit_mask);
        }
    }
}

void leds_init() {
    neopixel_init(config.led_pin);
}

bool config_ok(config_t* c) {
    if (crc32((uint8_t*) c, sizeof(config_t) - 4) != c->crc) {
        return false;
    }
    if (c->version != CONFIG_VERSION) {
        return false;
    }
    return true;
}

void config_init() {
    config.crc = crc32((uint8_t*) &config, sizeof(config_t) - 4);
    if (!config_ok((config_t*) FLASH_CONFIG_IN_MEMORY)) {
        return;
    }
    memcpy(&config, FLASH_CONFIG_IN_MEMORY, sizeof(config_t));
}

uint16_t tud_hid_get_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t* buffer, uint16_t reqlen) {
    if (reqlen != sizeof(config_t)) {
        return 0;
    }

    memcpy(buffer, &config, reqlen);
    config_t* c = (config_t*) buffer;
    memset(c->wifi_password, 0, sizeof(c->wifi_password));
    c->crc = crc32((uint8_t*) c, sizeof(config_t) - 4);

    return reqlen;
}

void persist_config() {
    static uint8_t buffer[PERSISTED_CONFIG_SIZE];

    memset(buffer, 0, sizeof(buffer));
    memcpy(buffer, &config, sizeof(config_t));
    uint32_t ints = save_and_disable_interrupts();
    flash_range_erase(CONFIG_OFFSET_IN_FLASH, PERSISTED_CONFIG_SIZE);
    flash_range_program(CONFIG_OFFSET_IN_FLASH, buffer, PERSISTED_CONFIG_SIZE);
    restore_interrupts(ints);
}

void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const* buffer, uint16_t bufsize) {
    if (bufsize != sizeof(config_t)) {
        return;
    }
    if (!config_ok((config_t*) buffer)) {
        return;
    }
    memcpy(&config, buffer, bufsize);
    config.wifi_ssid[sizeof(config.wifi_ssid) - 1] = 0;
    config.wifi_password[sizeof(config.wifi_password) - 1] = 0;
    if ((strlen(config.wifi_password) == 0) &&
        config_ok((config_t*) FLASH_CONFIG_IN_MEMORY)) {
        memcpy(config.wifi_password, ((config_t*) FLASH_CONFIG_IN_MEMORY)->wifi_password, sizeof(config.wifi_password));
    }
    config.crc = crc32((uint8_t*) &config, sizeof(config_t) - 4);
    persist_config();
}

int main(void) {
    board_init();
    // stdio_init_all();
    config_init();
    leds_init();
    net_init();
    tusb_init();

    while (true) {
        tud_task();
        cyw43_arch_poll();
        net_task();
        led_task();
        sleep_us(75);
    }

    return 0;
}
