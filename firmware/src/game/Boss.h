#pragma once
#include <Arduino.h>

class Boss {
  public:
    void Spawn();
    void Hit();
    void Kill();
    bool Alive();
    int _pos;
    int _lives;
    int _ticks;
  private:
    bool _alive;
};

inline void Boss::Spawn(){
    _pos = 800;
    _lives = 3;
    _alive = 1;
}

inline void Boss::Hit(){
    _lives--;
    if(_lives == 0) { Kill(); return; }
    if(_lives == 2){ _pos = 200; }
    else if(_lives == 1){ _pos = 600; }
}

inline bool Boss::Alive(){ return _alive; }
inline void Boss::Kill(){  _alive = 0; }
