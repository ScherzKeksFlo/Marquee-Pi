const assert = require("node:assert/strict");
const GestureRecognizer = require("../static/gesture.js");

function stroke(dx, dy, duration = 300) {
  const gesture = new GestureRecognizer();
  gesture.down(1, 100, 100, 0);
  gesture.move(1, 100 + dx, 100 + dy);
  return gesture.up(1, 100 + dx, 100 + dy, duration);
}

assert.equal(stroke(0, 0), "tap");
assert.equal(stroke(140, 10), "swipe-right");
assert.equal(stroke(-140, 10), "swipe-left");
assert.equal(stroke(10, 140), "swipe-down");
assert.equal(stroke(10, -140), "swipe-up");
assert.equal(stroke(55, 50), null);
assert.equal(stroke(30, 0), null);
assert.equal(stroke(0, 0, 700), null);
assert.equal(stroke(140, 0, 2000), null);

const multi = new GestureRecognizer();
multi.down(1, 100, 100, 0);
multi.down(2, 120, 100, 10);
assert.equal(multi.up(1, 250, 100, 200), null);
assert.equal(multi.up(2, 120, 100, 210), null);
multi.down(3, 100, 100, 300);
assert.equal(multi.up(3, 100, 100, 350), "tap");

const held = new GestureRecognizer();
held.down(1, 100, 100, 0);
assert.equal(held.longPress(500), false);
assert.equal(held.longPress(800), true);
assert.equal(held.longPress(900), false, "long press fires only once");
assert.equal(held.up(1, 100, 100, 1000), null, "release after long press is not a tap");

const drifted = new GestureRecognizer();
drifted.down(1, 100, 100, 0);
drifted.move(1, 160, 100);
assert.equal(drifted.longPress(900), false, "moving finger is not a long press");
assert.equal(drifted.up(1, 160, 100, 950), null);

const twoFingers = new GestureRecognizer();
twoFingers.down(1, 100, 100, 0);
twoFingers.down(2, 120, 100, 100);
assert.equal(twoFingers.longPress(900), false, "multi-touch never opens the menu");
twoFingers.up(1, 100, 100, 950);
twoFingers.up(2, 120, 100, 960);
assert.equal(new GestureRecognizer().longPress(900), false, "no active pointer");

console.log("Gesture tests passed");
