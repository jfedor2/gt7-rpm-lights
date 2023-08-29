import crc32 from './crc.js';

const CONFIG_VERSION = 1;
const CONFIG_SIZE = 64;
const VENDOR_ID = 0xCAFE;
const PRODUCT_ID = 0xBAF4;

let device = null;

document.addEventListener("DOMContentLoaded", function () {
    document.getElementById("open_device").addEventListener("click", open_device);
    document.getElementById("load_from_device").addEventListener("click", load_from_device);
    document.getElementById("save_to_device").addEventListener("click", save_to_device);

    device_buttons_set_disabled_state(true);

    if ("hid" in navigator) {
        navigator.hid.addEventListener('disconnect', hid_on_disconnect);
    } else {
        display_error("Your browser doesn't support WebHID. Try Chrome (desktop version) or a Chrome-based browser.");
    }
});

async function open_device() {
    clear_error();
    let success = false;
    const devices = await navigator.hid.requestDevice({
        filters: [{ vendorId: VENDOR_ID, productId: PRODUCT_ID }]
    }).catch((err) => { display_error(err); });
    const config_interface = devices?.find(d => d.collections.some(c => c.usagePage == 0xff00));
    if (config_interface !== undefined) {
        device = config_interface;
        if (!device.opened) {
            await device.open().catch((err) => { display_error(err + "\nIf you're on Linux, you might need to give yourself permissions to the appropriate /dev/hidraw* device."); });
        }
        success = device.opened;
    }

    device_buttons_set_disabled_state(!success);

    if (!success) {
        device = null;
    }
}

async function load_from_device() {
    if (device == null) {
        return;
    }
    clear_error();

    try {
        const data = await device.receiveFeatureReport(0);
        // console.log(data);
        check_crc(data);
        let pos = 0;

        const config_version = data.getUint8(pos++);
        check_received_version(config_version);

        const flags = data.getUint8(pos++);
        document.getElementById("blink_checkbox").checked = flags & 0x01;

        let ssid = '';
        for (let i = 0; i < 20; i++) {
            const c = data.getUint8(pos+i);
            if (c == 0) {
                break;
            }
            ssid += String.fromCharCode(c);
        }
        document.getElementById("wifi_ssid_input").value = ssid;
        pos += 20;

        pos += 24;
        document.getElementById("wifi_password_input").value = '';

        document.getElementById("ip1_input").value = data.getUint8(pos++);
        document.getElementById("ip2_input").value = data.getUint8(pos++);
        document.getElementById("ip3_input").value = data.getUint8(pos++);
        document.getElementById("ip4_input").value = data.getUint8(pos++);
        document.getElementById("nleds_input").value = data.getUint8(pos++);
        document.getElementById("led_pin_input").value = data.getUint8(pos++);
        document.getElementById("pattern_dropdown").value = data.getUint8(pos++);
        document.getElementById("color_dropdown").value = data.getUint8(pos++);
        document.getElementById("brightness_input").value = data.getUint8(pos++);
        document.getElementById("start_percent_input").value = data.getUint8(pos++);
        document.getElementById("end_percent_input").value = data.getUint8(pos++);
    } catch (e) {
        display_error(e);
    }
}

async function save_to_device() {
    if (device == null) {
        return;
    }
    clear_error();

    try {
        let buffer = new ArrayBuffer(CONFIG_SIZE);
        let dataview = new DataView(buffer);
        dataview.setUint8(0, CONFIG_VERSION);
        let pos = 1;
        const flags = document.getElementById("blink_checkbox").checked ? 1 : 0;
        dataview.setUint8(pos++, flags);

        const ssid = document.getElementById("wifi_ssid_input").value;
        if (ssid.length > 19) {
            throw new Error('Maximum wifi network name length is 19 characters.');
        }
        for (let i = 0; i < 20; i++) {
            const c = ssid.charCodeAt(i);
            dataview.setUint8(pos++, isNaN(c) ? 0 : c);
        }

        const password = document.getElementById("wifi_password_input").value;
        if (password.length > 23) {
            throw new Error('Maximum wifi password length is 23 characters.');
        }
        for (let i = 0; i < 24; i++) {
            const c = password.charCodeAt(i);
            dataview.setUint8(pos++, isNaN(c) ? 0 : c);
        }

        dataview.setUint8(pos++, get_int("ip1_input", "IP address"));
        dataview.setUint8(pos++, get_int("ip2_input", "IP address"));
        dataview.setUint8(pos++, get_int("ip3_input", "IP address"));
        dataview.setUint8(pos++, get_int("ip4_input", "IP address"));
        dataview.setUint8(pos++, get_int("nleds_input", "number of LEDs"));
        dataview.setUint8(pos++, get_int("led_pin_input", "LED pin"));
        dataview.setUint8(pos++, get_int("pattern_dropdown", "pattern"));
        dataview.setUint8(pos++, get_int("color_dropdown", "color scheme"));
        dataview.setUint8(pos++, get_int("brightness_input", "brightness"));
        dataview.setUint8(pos++, get_int("start_percent_input", "start value"));
        dataview.setUint8(pos++, get_int("end_percent_input", "end value"));
        dataview.setUint8(pos++, 0);
        dataview.setUint8(pos++, 0);
        dataview.setUint8(pos++, 0);

        add_crc(dataview);

        // console.log(buffer);

        await device.sendFeatureReport(0, buffer);

    } catch (e) {
        display_error(e);
    }
}

function get_int(element_id, description) {
    const value = parseInt(document.getElementById(element_id).value, 10);
    if (isNaN(value)) {
        throw new Error("Invalid "+description+".");
    }
    return value;
}

function clear_error() {
    document.getElementById("error").classList.add("d-none");
}

function display_error(message) {
    document.getElementById("error").innerText = message;
    document.getElementById("error").classList.remove("d-none");
}

function check_crc(data) {
    if (data.getUint32(CONFIG_SIZE - 4, true) != crc32(data, CONFIG_SIZE - 4)) {
        throw new Error('CRC error.');
    }
}

function add_crc(data) {
    data.setUint32(CONFIG_SIZE - 4, crc32(data, CONFIG_SIZE - 4), true);
}

function check_received_version(config_version) {
    if (config_version != CONFIG_VERSION) {
        throw new Error("Incompatible version.");
    }
}

function hid_on_disconnect(event) {
    if (event.device === device) {
        device = null;
        device_buttons_set_disabled_state(true);
    }
}

function device_buttons_set_disabled_state(state) {
    document.getElementById("load_from_device").disabled = state;
    document.getElementById("save_to_device").disabled = state;
}