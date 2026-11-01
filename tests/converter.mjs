import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import definition, {decode} from '../home-assistant/esp-termostat.mjs';
const bytes = readFileSync(process.argv[2]);
// C++ fixture contains node A; protocol transport only runs on B.
bytes[1] = 2;
let decoded = decode(bytes);
assert.equal(decoded.settings.on, 65);
assert.equal(decoded.peer.calibration, 1.23);
assert.equal(decoded.settings.relay_mode, 2);
assert.equal(decoded.node, 'B');
assert.throws(() => decode(bytes.subarray(1)));
const stale = Buffer.from(bytes); stale[31] &= 127;
decoded = decode(stale); assert.equal(decoded.peer.temperature, null); assert(decoded.peer_alarm);
assert.equal(decoded.peer.pump_request, null); assert.equal(decoded.peer.ha_delivery, null);
assert.equal(definition.fromZigbee[0].convert(null, {data: {}}), undefined);
let written;
await definition.toZigbee[0].convertSet({write: async (...args) => {written = args;}}, 'delivery_ack', 123);
assert.deepEqual(written, [0xfc10, {1: {value: 123, type: 0x23}}, {disableDefaultResponse: true}]);
for (const bad of [-1, 0, 1.5, Infinity, '12', 0x100000000]) {
    await assert.rejects(() => definition.toZigbee[0].convertSet({}, 'delivery_ack', bad));
}
console.log('PASS: C++/Zigbee wire compatibility, stale values, malformed packets and ACK validation');
