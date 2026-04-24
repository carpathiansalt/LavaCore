#pragma once
// Abstract input source. Each concrete source produces the two legacy globals
// used by the game loop:
//   joystickTilt    in range [-90, +90]
//   joystickWobble  non-negative; >= attack_threshold triggers attack
//
// The game itself reads those two values via InputManager::apply(), which
// picks the current source and updates the globals in place. This preserves
// the getInput() contract from the original .ino sketch.
#include <stdint.h>

class IInputSource {
  public:
    virtual ~IInputSource() = default;

    // Pull fresh sensor values (or latest network packet) and populate the
    // source-local tilt/wobble. Called once per frame.
    virtual void update() = 0;

    virtual int  tilt()   const = 0;   // -90..+90
    virtual int  wobble() const = 0;   // >= 0

    // Sources that require an external feed (e.g. BLE) should report false
    // when the feed has gone stale. The manager uses this to decide whether
    // to fall back or to zero input.
    virtual bool is_active() const = 0;
};

// -- Globals the game loop reads (kept in the legacy namespace) ------------
extern int joystickTilt;
extern int joystickWobble;
