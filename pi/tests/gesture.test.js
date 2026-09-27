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

console.log("Gesture tests passed");
