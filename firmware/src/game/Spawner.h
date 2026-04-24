#pragma once
#include <Arduino.h>

class Spawner {
  public:
    void Spawn(int pos, int rate, int sp, int dir, long activate);
    void Kill();
    int Alive();
    int _pos;
    int _rate;
    int _sp;
    int _dir;
    long _lastSpawned;
    long _activate;
  private:
    int _alive;
};

inline void Spawner::Spawn(int pos, int rate, int sp, int dir, long activate){
    _pos = pos;
    _rate = rate;
    _sp = sp;
    _dir = dir;
    _activate = millis() + activate;
    _alive = 1;
}

inline void Spawner::Kill(){
    _alive = 0;
    _lastSpawned = 0;
}

inline int Spawner::Alive(){ return _alive; }
