// Zigbee2MQTT external converter. No private/custom dependency API required.
// Cluster 0xFC10 attribute 0: protocol-v1 68-byte octet string; attribute 1: HA ACK.
const CLUSTER = 0xfc10;
export function decode(data) {
    const b = Buffer.from(data);
    if (b.length !== 68 || b[0] !== 1 || b[1] !== 2) throw new Error('Invalid thermostat telemetry');
    const readNode = (offset, sequence) => {
        const flags = b[offset + 8], available = Boolean(flags & 128), faults = b.readUInt32LE(offset + 4);
        const temperature = b.readInt16LE(offset);
        return {
            available,
            temperature: available && !(faults & 1) && temperature !== -32768 ? temperature / 100 : null,
            calibration: b.readInt16LE(offset + 2) / 100,
            faults: available ? faults : 2,
            pump_request: available ? Boolean(flags & 1) : null,
            command_sent: available ? Boolean(flags & 2) : null,
            link: available ? Boolean(flags & 4) : null,
            ha_delivery: available ? Boolean(flags & 8) : null,
            boot: b.readUInt32LE(offset + 9), sequence,
            fault_events: b.readUInt16LE(offset + 13), last_fault: b.readUInt16LE(offset + 15),
        };
    };
    const sequence = b.readUInt32LE(2), local = readNode(6, sequence), peer = readNode(23, b.readUInt32LE(40));
    const flags = b[14], revision = b.readUInt32LE(44);
    return {
        firmware: '0.1.0', node: 'B', sequence, local, peer,
        peer_alarm: !peer.available, sync_ok: !(local.faults & 256), saved: Boolean(flags & 16),
        settings: {
            revision, on: b.readInt16LE(48) / 100, hysteresis: b.readUInt16LE(50) / 100,
            force: b.readInt16LE(52) / 100, min_on_s: b.readUInt16LE(54), manual_s: b.readUInt16LE(56),
            difference: b.readUInt16LE(58) / 100, relay_mode: b[60], pulse_ms: b.readUInt16LE(61), commissioned: Boolean(b[63]),
        },
        temperature_b: local.temperature, temperature_a: peer.temperature,
        faults_b: local.faults, faults_a: peer.faults,
        wifi_path_alarm: !peer.available || !peer.ha_delivery,
        zigbee_path_alarm: !local.ha_delivery,
    };
}
const binary = (name, description) => ({type: 'binary', name, property: name, access: 1, value_on: true, value_off: false, description});
const numeric = (name, description, unit) => ({type: 'numeric', name, property: name, access: 1, description, ...(unit ? {unit} : {})});
export default {
    zigbeeModel: ['ESP-TERMOSTAT-1'], model: 'ESP-TERMOSTAT-1', vendor: 'Jiri DIY',
    description: 'Independent stove pump thermostat, XIAO C6 with wired peer',
    fromZigbee: [{
        cluster: CLUSTER, type: ['attributeReport', 'readResponse'],
        convert: (model, msg) => {
            if (msg.data[0] === undefined) return;
            try { return decode(msg.data[0]); } catch { return; }
        },
    }],
    toZigbee: [{
        key: ['delivery_ack'],
        convertSet: async (entity, key, value) => {
            if (!Number.isSafeInteger(value) || value < 1 || value > 0xffffffff) throw new Error('ACK must be uint32');
            await entity.write(CLUSTER, {1: {value, type: 0x23}}, {disableDefaultResponse: true});
        },
    }],
    exposes: [
        numeric('temperature_b', 'Temperature B measured by C6', '°C'),
        numeric('temperature_a', 'Temperature A forwarded over the wire', '°C'),
        numeric('faults_b', 'Fault bitmask B'), numeric('faults_a', 'Fault bitmask A'),
        binary('peer_alarm', 'Peer or bidirectional cable failed'), binary('sync_ok', 'Settings persisted on both nodes'),
        binary('wifi_path_alarm', 'A did not receive a recent Home Assistant acknowledgement'),
        binary('zigbee_path_alarm', 'B did not receive a recent Home Assistant acknowledgement'),
    ],
    configure: async (device) => { await device.getEndpoint(1).read(CLUSTER, [0]); },
};
