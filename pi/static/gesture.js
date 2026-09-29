(function (root, factory) {
  const GestureRecognizer = factory();
  if (typeof module === "object" && module.exports) module.exports = GestureRecognizer;
  else root.GestureRecognizer = GestureRecognizer;
})(typeof globalThis !== "undefined" ? globalThis : this, function () {
  "use strict";

  class GestureRecognizer {
    constructor(options = {}) {
      this.tapDistance = options.tapDistance ?? 22;
      this.tapDurationMs = options.tapDurationMs ?? 450;
      this.longPressMs = options.longPressMs ?? 800;
      this.swipeDistance = options.swipeDistance ?? 75;
      this.swipeDurationMs = options.swipeDurationMs ?? 1400;
      this.axisRatio = options.axisRatio ?? 1.25;
      this.reset();
    }

    reset() {
      this.active = null;
      this.blocked = false;
      this.pointers = new Set();
    }

    down(id, x, y, timeMs) {
      this.pointers.add(id);
      if (this.pointers.size !== 1) {
        this.active = null;
        this.blocked = true;
        return;
      }
      if (!this.blocked) this.active = { id, x, y, timeMs, lastX: x, lastY: y };
    }

    move(id, x, y) {
      if (this.active && this.active.id === id) {
        this.active.lastX = x;
        this.active.lastY = y;
      }
    }

    // Polled by a timer while a finger is held. Returns true once, when the single
    // active pointer has stayed put long enough; its release then yields no gesture.
    longPress(timeMs) {
      const active = this.active;
      if (!active || active.consumed || this.blocked) return false;
      if (timeMs - active.timeMs < this.longPressMs) return false;
      if (Math.hypot(active.lastX - active.x, active.lastY - active.y) > this.tapDistance) return false;
      active.consumed = true;
      return true;
    }

    up(id, x, y, timeMs) {
      this.pointers.delete(id);
      if (this.blocked) {
        if (this.pointers.size === 0) this.blocked = false;
        return null;
      }
      const start = this.active;
      this.active = null;
      if (!start || start.id !== id || start.consumed) return null;

      const dx = x - start.x;
      const dy = y - start.y;
      const distance = Math.hypot(dx, dy);
      const duration = timeMs - start.timeMs;
      if (duration < 0) return null;
      if (distance <= this.tapDistance && duration <= this.tapDurationMs) return "tap";
      if (distance < this.swipeDistance || duration > this.swipeDurationMs) return null;

      const absX = Math.abs(dx);
      const absY = Math.abs(dy);
      if (absX >= absY * this.axisRatio) return dx > 0 ? "swipe-right" : "swipe-left";
      if (absY >= absX * this.axisRatio) return dy > 0 ? "swipe-down" : "swipe-up";
      return null;
    }

    cancel(id) {
      this.pointers.delete(id);
      this.active = null;
      if (this.pointers.size === 0) this.blocked = false;
      else this.blocked = true;
    }
  }

  return GestureRecognizer;
});
