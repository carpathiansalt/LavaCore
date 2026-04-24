#pragma once
#include <Arduino.h>

#define FRICTION 1

class Particle {
  public:
    void Spawn(int pos);
    void Tick(int apply_gravity);
    void Kill();
    bool Alive();
    int _pos;
    int _power;
  private:
    int _life;
    int _alive;
    int _sp;
};

inline void Particle::Spawn(int pos){
    _pos = pos;
    _sp = random(-200, 200);
    _power = 255;
    _alive = 1;
    _life = 220 - abs(_sp);
}

inline void Particle::Tick(int apply_gravity){
    if(_alive){
        _life++;
        if(_sp > 0) _sp -= _life/10;
        else        _sp += _life/10;
        if(apply_gravity && _pos > 500) _sp -= 10;
        _power = 100 - _life;
        if(_power <= 0){
            Kill();
        } else {
            _pos += _sp/7.0;
            if(_pos > 1000){
                _pos = 1000;
                _sp = -(_sp/2);
            } else if(_pos < 0){
                _pos = 0;
                _sp = -(_sp/2);
            }
        }
    }
}

inline bool Particle::Alive(){ return _alive; }
inline void Particle::Kill(){  _alive = 0; }
