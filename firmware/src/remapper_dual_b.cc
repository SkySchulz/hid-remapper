#include <bsp/board_api.h>
#include <tusb.h>

#include "hardware/watchdog.h"
#include "pico/stdio.h"
#include "pico/time.h"

#include "activity_led.h"
#include "dual.h"
#include "interval_override.h"
#include "out_report.h"
#include "serial.h"

uint8_t buffer[SERIAL_MAX_PAYLOAD_SIZE + sizeof(device_connected_t)];
bool initialized = false;

bool serial_callback(const uint8_t* data, uint16_t len) {
    switch ((DualCommand) data[0]) {
        case DualCommand::B_INIT:
            interval_override = ((b_init_t*) data)->interval_override;
            initialized = true;
            break;
        case DualCommand::RESTART:
            watchdog_reboot(0, 0, 0);
            break;
        case DualCommand::SEND_OUT_REPORT: {
            send_out_report_t* msg = (send_out_report_t*) data;
            do_queue_out_report(msg->report, len - sizeof(send_out_report_t), msg->report_id, msg->dev_addr, msg->interface, OutType::OUTPUT);
            break;
        }
        case DualCommand::SET_FEATURE_REPORT: {
            set_feature_report_t* msg = (set_feature_report_t*) data;
            do_queue_out_report(msg->report, len - sizeof(set_feature_report_t), msg->report_id, msg->dev_addr, msg->interface, OutType::SET_FEATURE);
            break;
        }
        case DualCommand::GET_FEATURE_REPORT: {
            get_feature_report_t* msg = (get_feature_report_t*) data;
            do_queue_get_report(msg->report_id, msg->dev_addr, msg->interface, msg->len);
            break;
        }
        default:
            break;
    }

    return false;
}

void request_b_init() {
    request_b_init_t msg;
    serial_write((uint8_t*) &msg, sizeof(msg));
}

int main() {
    serial_init();
    board_init();
    stdio_init_all();

    while (!initialized) {
        request_b_init();
        serial_read(serial_callback);
    }

    tusb_init();

    while (true) {
        tuh_task();
        serial_read(serial_callback);
        do_send_out_report();
        activity_led_off_maybe();
    }

    return 0;
}

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* report, uint16_t len) {
    activity_led_on();

    report_received_t* msg = (report_received_t*) buffer;
    msg->command = DualCommand::REPORT_RECEIVED;
    msg->dev_addr = dev_addr;
    msg->interface = instance;
    memcpy(msg->report, report, len);
    serial_write((uint8_t*) msg, len + sizeof(report_received_t));
    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const* desc_report, uint16_t desc_len) {
    printf("tuh_hid_mount_cb\n");
    stdio_flush();
    device_connected_t* msg = (device_connected_t*) buffer;
    msg->command = DualCommand::DEVICE_CONNECTED;
    uint16_t vid;
    uint16_t pid;
    tuh_vid_pid_get(dev_addr, &vid, &pid);
    msg->vid = vid;
    msg->pid = pid;
    msg->dev_addr = dev_addr;
    msg->interface = instance;
    memcpy(msg->report_descriptor, desc_report, desc_len);
    serial_write((uint8_t*) msg, desc_len + sizeof(device_connected_t));
    tuh_hid_receive_report(dev_addr, instance);
}

void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance) {
    printf("tuh_hid_umount_cb %d %d\n", dev_addr, instance);
    stdio_flush();
    device_disconnected_t msg;
    msg.dev_addr = dev_addr;
    msg.interface = instance;
    serial_write((uint8_t*) &msg, sizeof(msg));
}

void tuh_sof_cb() {
    start_of_frame_t msg;
    serial_write((uint8_t*) &msg, sizeof(msg));
}

void get_report_cb(uint8_t dev_addr, uint8_t interface, uint8_t report_id, uint8_t report_type, uint8_t* report, uint16_t len) {
    get_feature_response_t* msg = (get_feature_response_t*) buffer;
    msg->command = DualCommand::GET_FEATURE_RESPONSE;
    msg->dev_addr = dev_addr;
    msg->interface = interface;
    msg->report_id = report_id;
    memcpy(msg->report, report, len);
    serial_write((uint8_t*) msg, len + sizeof(get_feature_response_t));
}

void set_report_complete_cb(uint8_t dev_addr, uint8_t interface, uint8_t report_id) {
    set_feature_complete_t* msg = (set_feature_complete_t*) buffer;
    msg->command = DualCommand::SET_FEATURE_COMPLETE;
    msg->dev_addr = dev_addr;
    msg->interface = interface;
    msg->report_id = report_id;
    serial_write((uint8_t*) msg, sizeof(set_feature_complete_t));
}
