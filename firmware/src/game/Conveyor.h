#pragma once
#include <Arduino.h>
#include "config/settings.h"

class Conveyor {
  public:
    void Spawn(int startPoint, int endPoint, int speed);
    void Kill();
    int _startPoint;
    int _endPoint;
    int _speed;
    bool _alive;
};

inline void Conveyor::Spawn(int startPoint, int endPoint, int speed){
    _startPoint = startPoint;
    _endPoint = endPoint;
    _speed = constrain(speed, -MAX_PLAYER_SPEED + 1, MAX_PLAYER_SPEED - 1);
    _alive = true;
}

inline void Conveyor::Kill(){ _alive = false; }
