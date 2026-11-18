// ============================================================================
//  Zigbee2MQTT external converter - DOMCOM Dimmer
// ============================================================================
//  Without it Zigbee2MQTT already works through a generated definition
//  (light, brightness, power-on behaviour, effects). The converter adds:
//    - the device name and description;
//    - the settings in the custom cluster 0xFC00 as configuration entities
//      (minimum output, gamma, button ramp time), also in Home Assistant.
//
//  Installation: see docs/ZIGBEE.md. In short, copy this file into the
//  external_converters folder of your Zigbee2MQTT data directory and restart
//  Zigbee2MQTT.
//
//  Written for Zigbee2MQTT 2.x (zigbee-herdsman-converters 26.x).

const {Zcl} = require('zigbee-herdsman');
const m = require('zigbee-herdsman-converters/lib/modernExtend');

// Must match CFG_CLUSTER_ID and CFG_ATTR_* in firmware/main/zb_dimmer.c
const CONFIG_CLUSTER = 'domcomDimmerConfig';

module.exports = {
    zigbeeModel: ['DOMCOM-Dimmer'],
    model: 'DOMCOM-Dimmer',
    vendor: 'DOMCOM',
    description: 'Zigbee dimmer for single-colour LED strips (ESP32-C6)',
    extend: [
        m.deviceAddCustomCluster(CONFIG_CLUSTER, {
            name: CONFIG_CLUSTER,
            ID: 0xfc00,
            attributes: {
                minOutput: {name: 'minOutput', ID: 0x0000, type: Zcl.DataType.UINT16, write: true, max: 2500},
                gamma: {name: 'gamma', ID: 0x0001, type: Zcl.DataType.UINT8, write: true, max: 40},
                holdRampMs: {name: 'holdRampMs', ID: 0x0002, type: Zcl.DataType.UINT16, write: true, max: 20000},
            },
            commands: {},
            commandsResponse: {},
        }),
        m.light({effect: true, powerOnBehavior: true}),
        m.numeric({
            name: 'minimum_output',
            label: 'Minimum output',
            cluster: CONFIG_CLUSTER,
            attribute: 'minOutput',
            description:
                'PWM duty at brightness 1, where the strip should just glow. ' +
                'Raise it if the lowest brightness is dark, lower it if it is too bright.',
            unit: '%',
            scale: 100,
            valueMin: 0,
            valueMax: 25,
            valueStep: 0.01,
            reporting: false,
            entityCategory: 'config',
        }),
        m.numeric({
            name: 'gamma',
            label: 'Gamma',
            cluster: CONFIG_CLUSTER,
            attribute: 'gamma',
            description: 'Brightness curve. 2.8 matches ESPHome; lower values brighten the low end.',
            scale: 10,
            valueMin: 1,
            valueMax: 4,
            valueStep: 0.1,
            reporting: false,
            entityCategory: 'config',
        }),
        m.numeric({
            name: 'button_ramp_time',
            label: 'Button ramp time',
            cluster: CONFIG_CLUSTER,
            attribute: 'holdRampMs',
            description: 'Time to go from minimum to maximum while a button is held.',
            unit: 's',
            scale: 1000,
            valueMin: 1,
            valueMax: 20,
            valueStep: 0.1,
            reporting: false,
            entityCategory: 'config',
        }),
        // Read the settings once on configure, so Home Assistant shows the
        // current values instead of empty fields.
        {
            isModernExtend: true,
            configure: [m.setupConfigureForReading(CONFIG_CLUSTER, ['minOutput', 'gamma', 'holdRampMs'])],
        },
    ],
};
