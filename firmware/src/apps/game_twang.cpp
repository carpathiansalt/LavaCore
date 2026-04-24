// TWANG gameplay, ported from TWANG32.ino and refactored for up to
// TWANG_MAX_PLAYERS concurrent cursors on the same strip. Each player has
// their own position, lives, score and attack state; enemies, lava,
// conveyors and the boss are shared world state that every cursor can
// interact with.
//
// Level-complete rule:  first ALIVE player to reach the exit advances all
//                       surviving cursors to the next level.
// Game-over rule:       when every player has exhausted their lives.
// Respawn rule:         on death, a player enters the DEAD visual state; the
//                       other cursors keep playing. When the dying animation
//                       finishes the player either respawns at pos 0 (with
//                       one fewer life) or is marked game_over if lives hit 0.
//
// (c) B. Dring 3/2018 (original)
// License: Creative Commons 4.0 Attribution - Share Alike

#include "apps/game_twang.h"

#include <Arduino.h>
#include <FastLED.h>
#include <string.h>

#include "config/hw_config.h"
#include "config/settings.h"
#include "hal/led_output.h"
#include "hal/sound.h"
#include "input/input_source.h"
#include "input/input_manager.h"
#include "input/remote_input.h"
#include "game/Boss.h"
#include "game/Conveyor.h"
#include "game/Enemy.h"
#include "game/Lava.h"
#include "game/Particle.h"
#include "game/Spawner.h"
#include "game/iSin.h"

// -- Tunables (identical to the original sketch) ---------------------------
#define TIMEOUT                  20000
#define DEFAULT_ATTACK_WIDTH     70
#define ATTACK_DURATION          500
#define BOSS_WIDTH               40

#define STARTUP_WIPEUP_DUR       200
#define STARTUP_SPARKLE_DUR      1300
#define STARTUP_FADE_DUR         1500

#define GAMEOVER_SPREAD_DURATION 1000
#define GAMEOVER_FADE_DURATION   1500

#define WIN_FILL_DURATION        500
#define WIN_CLEAR_DURATION       1000
#define WIN_OFF_DURATION         1200

#define ENEMY_COUNT              10
#define PARTICLE_COUNT           40
#define SPAWN_COUNT              2
#define LAVA_COUNT               4
#define CONVEYOR_COUNT           2

#define COOLING                  75
#define SPARKING                 40

namespace {

using input_manager::kMaxPlayers;

// -- Pools and actors (shared across players) -----------------------------
Enemy     enemyPool[ENEMY_COUNT];
Particle  particlePool[PARTICLE_COUNT];
Spawner   spawnPool[SPAWN_COUNT];
Lava      lavaPool[LAVA_COUNT];
Conveyor  conveyorPool[CONVEYOR_COUNT];
Boss      boss;
iSin      isin;

// -- Per-player state ------------------------------------------------------
struct Player {
    bool     active             = false;  // slot filled this frame
    bool     pending_join       = false;  // source seen mid-level; join next level
    bool     alive              = false;  // cursor on the strip this round
    int      position           = 0;      // virtual world [0..VIRTUAL_LED_COUNT]
    int      positionModifier   = 0;
    bool     attacking          = false;
    long     attackMillis       = 0;
    int      attack_width       = DEFAULT_ATTACK_WIDTH;
    long     killTime           = 0;
    int      lives              = LIVES_PER_LEVEL;
    int      score              = 0;
    bool     reached_exit       = false;
    bool     game_over          = false;
    // colour used to draw this player's cursor + attack aura.
    CRGB     cursor_color       = CRGB::Green;
};

Player players[kMaxPlayers];

// -- Shared game state ----------------------------------------------------
enum Stages { STARTUP, PLAY, WIN, DEAD, SCREENSAVER, BOSS_KILLED, GAMEOVER };
Stages stage = STARTUP;

long previousMillis = 0;
int  levelNumber    = 0;
long stageStartTime = 0;
bool lastLevel      = false;
uint8_t levelPlayerCount = 1;
int levelDifficultyPercent = 100;

// Player palette: keep these visually distinct at small strip sizes.
const CRGB kPlayerColors[kMaxPlayers] = {
    CRGB(0, 255, 0),      // P0 green (classic TWANG)
    CRGB(0, 180, 255),    // P1 cyan
    CRGB(255, 150, 0),    // P2 orange
    CRGB(255, 0, 180),    // P3 magenta
};

// Forward decls -------------------------------------------------------------
void loadLevel();
void spawnBoss();
void moveBoss();
void spawnEnemy(int pos, int dir, int speed, int wobble, int playerSideRef);
void spawnSpawner(uint8_t idx, int pos, int rate, int sp, int dir, long activate);
void spawnLava(int left, int right, int ontime, int offtime, int offset,
               int state, float grow, float flow);
void spawnConveyor(int startPoint, int endPoint, int dir);
void cleanupLevel();
void levelComplete();
void nextLevel();
void gameOver();
void die(uint8_t idx);
void tickStartup(long mm);
void tickEnemies();
void tickBoss();
void drawPlayer(uint8_t idx);
void drawExit();
void tickSpawners();
void tickLava();
bool tickParticles();
void tickConveyors();
void tickBossKilled(long mm);
void tickDie(long mm);
void tickGameover(long mm);
void tickWin(long mm);
void drawLives();
void drawAttack(uint8_t idx);
int  getLED(int pos);
bool inLava(int pos);
void updateLives();
void save_game_stats(bool bossKill);
void screenSaverTick();
void Fire2012();
void LED_march();
void random_LED_flashes();
void sinelon();
void juggle();
void SFXFreqSweepWarble(int duration, int elapsedTime, int freqStart, int freqEnd, int warble);
void SFXFreqSweepNoise (int duration, int elapsedTime, int freqStart, int freqEnd, uint8_t noiseFactor);
void SFXtilt(int amount);
void SFXattacking();
void SFXdead();
void SFXgameover();
void SFXkill();
void SFXwin();
void SFXbosskilled();
void SFXcomplete();
long map_constrain(long x, long in_min, long in_max, long out_min, long out_max);

// -- Player-state helpers -------------------------------------------------
int leader_position() {
    int best = 0;
    for (auto& p : players) if (p.active && p.alive && p.position > best) best = p.position;
    return best;
}

int any_attack_tilt() {
    // Pick the strongest tilt of any alive player for tilt SFX / screensaver
    // wake-up. This is purely audio / state; each player's motion is driven
    // by their own input.
    int best = 0;
    for (uint8_t i = 0; i < kMaxPlayers; ++i) {
        if (!players[i].active || !players[i].alive) continue;
        if (abs(playerTilt[i]) > abs(best)) best = playerTilt[i];
    }
    return best;
}

bool any_alive() {
    for (auto& p : players) if (p.active && p.alive) return true;
    return false;
}

bool all_game_over() {
    for (auto& p : players) if (p.active && !p.game_over) return false;
    return true;
}

bool any_player_joined() {
    for (auto& p : players) if (p.active) return true;
    return false;
}

uint8_t active_player_count_for_level() {
    uint8_t n = 0;
    for (auto& p : players) {
        if (p.active && !p.game_over) ++n;
    }
    return n == 0 ? 1 : n;
}

int difficulty_percent_for_players(uint8_t count) {
    if (count <= 1) return 100;
    return 100 + ((static_cast<int>(count) - 1) *
                  TWANG_MULTIPLAYER_SCALE_STEP_PERCENT);
}

int scale_enemy_speed(int speed) {
    if (speed == 0 || levelDifficultyPercent == 100) return speed;
    const int scaled = (abs(speed) * levelDifficultyPercent + 50) / 100;
    return speed < 0 ? -scaled : scaled;
}

int scale_spawner_rate(int rate) {
    if (rate <= 0 || levelDifficultyPercent == 100) return rate;
    const int scaled = (rate * 100 + (levelDifficultyPercent / 2)) /
                       levelDifficultyPercent;
    return _max(TWANG_MIN_SPAWNER_RATE_MS, scaled);
}

void activate_slot(uint8_t i) {
    players[i].active = true;
    players[i].pending_join = false;
    players[i].alive  = true;
    players[i].lives  = g_settings.lives_per_level;
    players[i].score  = 0;
    players[i].position = 0;
    players[i].game_over = false;
    players[i].cursor_color = kPlayerColors[i];
}

// Refresh players[].active from input_manager once per frame. A slot becomes
// active immediately outside PLAY. During PLAY, late joiners are queued for
// the next level boundary so they never spawn into an active hazard layout.
void refresh_active_slots() {
    for (uint8_t i = 0; i < kMaxPlayers; ++i) {
        const bool has_source =
            input_manager::slot_source(i) != input_manager::SlotSource::None;
        if (!has_source) {
            if (!players[i].active) players[i].pending_join = false;
            continue;
        }
        if (players[i].active) continue;
        if (stage == PLAY) {
            players[i].pending_join = true;
        } else {
            activate_slot(i);
        }
    }
}

} // namespace

// ============== IApp implementation =======================================

void GameTwang::init() {
    sound_init(DAC_AUDIO_PIN);
    stage          = STARTUP;
    stageStartTime = millis();
    for (uint8_t i = 0; i < kMaxPlayers; ++i) {
        players[i] = Player{};
        players[i].cursor_color = kPlayerColors[i];
    }
}

void GameTwang::tick(uint32_t /*now_ms*/) {
    long mm = millis();

    refresh_active_slots();

#if TWANG_TRACE_GAME_INPUT
    static uint32_t s_lastGameTraceMs = 0;
    if (mm - s_lastGameTraceMs >= 1000) {
        s_lastGameTraceMs = mm;
        const int pk = any_attack_tilt();
        Serial.printf(
            "[twang.game] stage=%d any_alive=%d all_go=%d inp_act=%u tilt_pk=%d "
            "ri_act=%u%u%u%u slot_src=%u%u%u%u\r\n",
            static_cast<int>(stage),
            any_alive() ? 1 : 0,
            all_game_over() ? 1 : 0,
            static_cast<unsigned>(input_manager::active_player_count()),
            pk,
            g_remote_inputs[0].is_active() ? 1u : 0u,
            g_remote_inputs[1].is_active() ? 1u : 0u,
            g_remote_inputs[2].is_active() ? 1u : 0u,
            g_remote_inputs[3].is_active() ? 1u : 0u,
            static_cast<unsigned>(input_manager::slot_source(0)),
            static_cast<unsigned>(input_manager::slot_source(1)),
            static_cast<unsigned>(input_manager::slot_source(2)),
            static_cast<unsigned>(input_manager::slot_source(3)));
    }
#endif

    // Audio follows the leader cursor so single-player behavior is preserved.
    if (stage == PLAY) {
        bool any_attacking = false;
        for (auto& p : players) if (p.active && p.alive && p.attacking) { any_attacking = true; break; }
        if (any_attacking) SFXattacking();
        else               SFXtilt(any_attack_tilt());
    } else if (stage == DEAD) {
        SFXdead();
    }

    if (mm - previousMillis < MIN_REDRAW_INTERVAL) return;
    previousMillis = mm;

    const int tilt_peak = any_attack_tilt();
    if (abs(tilt_peak) > g_settings.joystick_deadzone) {
        g_last_input_time_ms = mm;
        if (stage == SCREENSAVER) {
            levelNumber    = -1;
            stageStartTime = mm;
            stage          = WIN;
        }
    } else if (g_last_input_time_ms + TIMEOUT < mm) {
        stage = SCREENSAVER;
    }

    if (stage == SCREENSAVER) {
        screenSaverTick();
    } else if (stage == STARTUP) {
        if (stageStartTime + STARTUP_FADE_DUR > mm) {
            tickStartup(mm);
        } else if (!any_player_joined()) {
            SFXcomplete();
            FastLED.clear();
        } else {
            SFXcomplete();
            levelNumber = 0;
            loadLevel();
        }
    } else if (stage == PLAY) {
        // Per-player tick: movement, attack, exit check.
        for (uint8_t i = 0; i < kMaxPlayers; ++i) {
            Player& p = players[i];
            if (!p.active || !p.alive) continue;

            if (p.attacking && p.attackMillis + ATTACK_DURATION < mm) p.attacking = false;

            if (!p.attacking && playerWobble[i] >= g_settings.attack_threshold) {
                p.attackMillis = mm;
                p.attacking    = true;
            }

            p.position += p.positionModifier;
            if (!p.attacking) {
                int moveAmount = (playerTilt[i] / 6.0f);
                if (DIRECTION) moveAmount = -moveAmount;
                moveAmount = constrain(moveAmount, -MAX_PLAYER_SPEED, MAX_PLAYER_SPEED);
                p.position -= moveAmount;
                if (p.position < 0) p.position = 0;

                if (boss.Alive() && p.position >= VIRTUAL_LED_COUNT) p.position = 999;

                if (p.position >= VIRTUAL_LED_COUNT && !boss.Alive()) {
                    p.reached_exit = true;
                }
            }

            if (inLava(p.position)) die(i);
        }

        if (!any_alive()) {
#if TWANG_TRACE_GAME_INPUT
            Serial.printf(
                "[twang.game] PLAY: !any_alive -> %s (inp_act=%u ri=%u%u%u%u)\r\n",
                all_game_over() ? "GAMEOVER" : "DEAD",
                static_cast<unsigned>(input_manager::active_player_count()),
                g_remote_inputs[0].is_active() ? 1u : 0u,
                g_remote_inputs[1].is_active() ? 1u : 0u,
                g_remote_inputs[2].is_active() ? 1u : 0u,
                g_remote_inputs[3].is_active() ? 1u : 0u);
#endif
            // All cursors died simultaneously this frame - treat like legacy.
            // die() already transitioned individual players; if EVERYONE is now
            // game_over, go to GAMEOVER, else fall through into DEAD animation.
            if (all_game_over()) {
                stage = GAMEOVER;
                stageStartTime = mm;
            } else {
                stage = DEAD;
                stageStartTime = mm;
            }
            return;
        }

        // Level complete when any alive player touched the exit.
        bool someone_done = false;
        for (auto& p : players) if (p.active && p.alive && p.reached_exit) { someone_done = true; break; }
        if (someone_done) { levelComplete(); return; }

        FastLED.clear();
        tickConveyors();
        tickSpawners();
        tickBoss();
        tickLava();
        tickEnemies();
        for (uint8_t i = 0; i < kMaxPlayers; ++i) drawPlayer(i);
        for (uint8_t i = 0; i < kMaxPlayers; ++i) drawAttack(i);
        drawExit();
    } else if (stage == DEAD) {
        FastLED.clear();
        tickDie(mm);
        if (!tickParticles()) {
            // Respawn the fallen players that still have lives; advance to
            // next level for everyone once the animation finishes.
            for (auto& p : players) {
                if (p.active && !p.alive && !p.game_over) {
                    p.alive    = true;
                    p.position = 0;
                }
            }
            loadLevel();
        }
    } else if (stage == WIN) {
        tickWin(mm);
    } else if (stage == BOSS_KILLED) {
        tickBossKilled(mm);
    } else if (stage == GAMEOVER) {
        if (stageStartTime + GAMEOVER_FADE_DURATION > mm) {
            tickGameover(mm);
        } else {
            FastLED.clear();
            save_game_stats(false);
            stage = STARTUP;
            stageStartTime = millis();
            // Reset all players for the next run.
            for (uint8_t i = 0; i < kMaxPlayers; ++i) {
                players[i] = Player{};
                players[i].cursor_color = kPlayerColors[i];
            }
        }
    }

    led_output::show();
}

void GameTwang::fill_telemetry(TelemetryPacket& out) const {
    out.mode_id = MODE_GAME_TWANG;
    out.level   = static_cast<uint16_t>(levelNumber < 0 ? 0 : levelNumber);

    uint8_t active = 0;
    for (uint8_t i = 0; i < kMaxPlayers; ++i) {
        auto& slot = out.players[i];
        const Player& p = players[i];
        if (!p.active) { slot.lives = 0; slot.flags = 0; slot.score_x10 = 0; continue; }
        ++active;
        slot.lives = static_cast<uint8_t>(p.lives < 0 ? 0 : p.lives);
        uint8_t f = 0;
        if (p.alive)     f |= TEL_P_ALIVE;
        if (p.attacking) f |= TEL_P_ATTACKING;
        if (input_manager::slot_source(i) == input_manager::SlotSource::Joystick) {
            f |= TEL_P_JOYSTICK;
        }
        slot.flags = f;
        uint32_t sx10 = static_cast<uint32_t>(p.score < 0 ? 0 : p.score) / 10u;
        if (sx10 > 0xFFFFu) sx10 = 0xFFFFu;
        slot.score_x10 = static_cast<uint16_t>(sx10);
    }

    out.player_count = active;
    uint8_t f = 0;
    if (stage == PLAY)     f |= TEL_IN_GAME;
    if (stage == GAMEOVER) f |= TEL_GAME_OVER;
    if (boss.Alive())      f |= TEL_BOSS_ALIVE;
    if (active > 1)        f |= TEL_MULTIPLAYER_ACTIVE;
    out.global_flags = f;
}

// ============== Internal helpers (direct port) ============================

namespace {

void loadLevel() {
    FastLED.setBrightness(g_settings.led_brightness);
    updateLives();
    cleanupLevel();
    lastLevel = false;

    for (uint8_t i = 0; i < kMaxPlayers; ++i) {
        if (!players[i].pending_join) continue;
        if (input_manager::slot_source(i) != input_manager::SlotSource::None) {
            activate_slot(i);
        } else {
            players[i].pending_join = false;
        }
    }
    levelPlayerCount = active_player_count_for_level();
    levelDifficultyPercent = difficulty_percent_for_players(levelPlayerCount);

    for (uint8_t i = 0; i < kMaxPlayers; ++i) {
        Player& p = players[i];
        p.attack_width = DEFAULT_ATTACK_WIDTH;
        p.position     = 0;
        p.reached_exit = false;
        p.attacking    = false;
    }

    // All references to "playerPosition" / "lives" in the original level
    // definitions map to player 0 for layout parity. Other players start at
    // position 0 and share the world.
    int p0_start = 0;

    int playerSideRef = p0_start;  // used for enemy playerSide init
    switch (levelNumber) {
        case 0:
            p0_start = 200;
            playerSideRef = p0_start;
            spawnEnemy(1, 0, 0, 0, playerSideRef);
            break;
        case 1: spawnEnemy(900, 0, 1, 0, playerSideRef); break;
        case 2: spawnSpawner(0, 1000, 3000, 2, 0, 0); break;
        case 3:
            spawnLava(400, 490, 2000, 2000, 0, Lava::OFF, 0, 0);
            spawnEnemy(350, 0, 1, 0, playerSideRef);
            spawnSpawner(0, 1000, 5500, 3, 0, 0);
            break;
        case 4:
            spawnLava(400, 490, 2000, 2000, 0, Lava::OFF, 0, -0.5f);
            spawnEnemy(350, 0, 1, 0, playerSideRef);
            spawnSpawner(0, 1000, 5500, 3, 0, 0);
            break;
        case 5:
            spawnLava(400, 450, 2000, 2000, 0, Lava::OFF, 0.25f, 0);
            spawnEnemy(350, 0, 1, 0, playerSideRef);
            spawnSpawner(0, 1000, 5500, 3, 0, 0);
            break;
        case 6:
            spawnEnemy(700, 1, 7, 275, playerSideRef);
            spawnEnemy(500, 1, 5, 250, playerSideRef);
            break;
        case 7:
            spawnEnemy(700, 1, 7, 275, playerSideRef);
            spawnEnemy(500, 1, 5, 250, playerSideRef);
            spawnEnemy(600, 1, 7, 200, playerSideRef);
            spawnEnemy(800, 1, 5, 350, playerSideRef);
            spawnEnemy(400, 1, 7, 150, playerSideRef);
            spawnEnemy(450, 1, 5, 400, playerSideRef);
            break;
        case 8:
            p0_start = 200;
            playerSideRef = p0_start;
            spawnLava(10, 180, 2000, 2000, 0, Lava::OFF, 0, 0.5f);
            spawnEnemy(350, 0, 1, 0, playerSideRef);
            spawnSpawner(0, 1000, 5500, 3, 0, 0);
            break;
        case 9:
            spawnConveyor(100, 600, -6);
            spawnEnemy(800, 0, 0, 0, playerSideRef);
            break;
        case 10:
            spawnConveyor(50, 1000, 6);
            spawnEnemy(300, 0, 0, 0, playerSideRef); spawnEnemy(400, 0, 0, 0, playerSideRef);
            spawnEnemy(500, 0, 0, 0, playerSideRef); spawnEnemy(600, 0, 0, 0, playerSideRef);
            spawnEnemy(700, 0, 0, 0, playerSideRef); spawnEnemy(800, 0, 0, 0, playerSideRef);
            spawnEnemy(900, 0, 0, 0, playerSideRef);
            break;
        case 11:
            spawnLava(400, 450, 2000, 2000, 0, Lava::OFF, 0.2f, -0.5f);
            spawnEnemy(350, 0, 1, 0, playerSideRef);
            spawnSpawner(0, 1000, 5500, 3, 0, 0);
            break;
        case 12: spawnSpawner(0, 900, 1300, 2, 0, 0); break;
        case 13:
            for (auto& p : players) p.attack_width = 32;
            spawnSpawner(0, 900, 1800, 2, 0, 0);
            break;
        case 14:
            spawnSpawner(0, 550, 1500, 2, 0, 0);
            spawnSpawner(1, 550, 1500, 2, 1, 0);
            break;
        case 15:
            spawnSpawner(0, 500, 1200, 2, 0, 0);
            spawnSpawner(1, 500, 1200, 2, 1, 0);
            spawnLava(900, 950, 2200, 800, 2000, Lava::OFF, 0, 0);
            break;
        case 16:
            spawnLava(195, 300, 2000, 2000, 0, Lava::OFF, 0, 0);
            spawnLava(400, 500, 2000, 2000, 0, Lava::OFF, 0, 0);
            spawnLava(600, 700, 2000, 2000, 0, Lava::OFF, 0, 0);
            spawnSpawner(0, 1000, 3800, 4, 0, 0);
            break;
        case 17:
            spawnEnemy(700, 1, 7, 275, playerSideRef);
            spawnEnemy(500, 1, 5, 250, playerSideRef);
            spawnSpawner(0, 1000, 5500, 4, 0, 3000);
            spawnSpawner(1, 0,    5500, 5, 1, 10000);
            spawnConveyor(100, 900, -4);
            break;
        case 18:
            spawnEnemy(800, 1, 7, 275, playerSideRef);
            spawnEnemy(700, 1, 7, 275, playerSideRef);
            spawnEnemy(500, 1, 5, 250, playerSideRef);
            spawnSpawner(0, 1000, 3000, 4, 0, 3000);
            spawnSpawner(1, 0,    5500, 5, 1, 10000);
            spawnConveyor(100, 900, -6);
            break;
        case 19: spawnBoss(); break;
    }

    // Place starting positions.
    for (uint8_t i = 0; i < kMaxPlayers; ++i) {
        if (!players[i].active) continue;
        players[i].position = p0_start;
        players[i].reached_exit = false;
    }

    stageStartTime = millis();
    stage = PLAY;
}

void spawnBoss() {
    lastLevel = true;
    boss.Spawn();
    moveBoss();
}

void moveBoss() {
    int spawnSpeed = 1800;
    if (boss._lives == 2) spawnSpeed = 1600;
    if (boss._lives == 1) spawnSpeed = 1000;
    spawnSpawner(0, boss._pos, spawnSpeed, 3, 0, 0);
    spawnSpawner(1, boss._pos, spawnSpeed, 3, 1, 0);
}

void spawnEnemy(int pos, int dir, int speed, int wobble, int playerSideRef) {
    for (int e = 0; e < ENEMY_COUNT; e++) {
        if (!enemyPool[e].Alive()) {
            enemyPool[e].Spawn(pos, dir, scale_enemy_speed(speed), wobble);
            enemyPool[e].playerSide = pos > playerSideRef ? 1 : -1;
            return;
        }
    }
}

void spawnSpawner(uint8_t idx, int pos, int rate, int sp, int dir, long activate) {
    if (idx >= SPAWN_COUNT) return;
    spawnPool[idx].Spawn(pos, scale_spawner_rate(rate), sp, dir, activate);
}

void spawnLava(int left, int right, int ontime, int offtime, int offset,
               int state, float grow, float flow) {
    for (int i = 0; i < LAVA_COUNT; i++) {
        if (!lavaPool[i].Alive()) {
            lavaPool[i].Spawn(left, right, ontime, offtime, offset, state, grow, flow);
            return;
        }
    }
}

void spawnConveyor(int startPoint, int endPoint, int dir) {
    for (int i = 0; i < CONVEYOR_COUNT; i++) {
        if (!conveyorPool[i]._alive) {
            conveyorPool[i].Spawn(startPoint, endPoint, dir);
            return;
        }
    }
}

void cleanupLevel() {
    for (int i = 0; i < ENEMY_COUNT;    i++) enemyPool[i].Kill();
    for (int i = 0; i < PARTICLE_COUNT; i++) particlePool[i].Kill();
    for (int i = 0; i < SPAWN_COUNT;    i++) spawnPool[i].Kill();
    for (int i = 0; i < LAVA_COUNT;     i++) lavaPool[i].Kill();
    for (int i = 0; i < CONVEYOR_COUNT; i++) conveyorPool[i].Kill();
    boss.Kill();
}

void levelComplete() {
    stageStartTime = millis();
    stage = WIN;
    if (lastLevel) stage = BOSS_KILLED;
    if (levelNumber != 0) {
        for (auto& p : players) {
            if (p.active && p.alive) p.score += (p.lives * 10);
        }
    }
}

void nextLevel() {
    levelNumber++;
    if (lastLevel) {
        stage = STARTUP;
        stageStartTime = millis();
        for (auto& p : players) if (p.active) p.lives = g_settings.lives_per_level;
    } else {
        for (auto& p : players) if (p.active) p.lives = g_settings.lives_per_level;
        loadLevel();
    }
}

void gameOver() { levelNumber = 0; loadLevel(); }

void die(uint8_t idx) {
    Player& p = players[idx];
    if (!p.active) return;
    p.alive = false;
    if (levelNumber > 0 && p.lives > 0) p.lives--;
    if (p.lives == 0) {
        p.game_over = true;
    }
    for (int part = 0; part < PARTICLE_COUNT; part++) particlePool[part].Spawn(p.position);
    p.killTime = millis();
}

// -- TICKS & RENDERS -------------------------------------------------------
void tickStartup(long mm) {
    FastLED.clear();
    if (stageStartTime + STARTUP_WIPEUP_DUR > mm) {
        int n = _min(map((mm - stageStartTime), 0, STARTUP_WIPEUP_DUR, 0, g_settings.led_count),
                     g_settings.led_count);
        for (int i = 0; i <= n; i++) g_leds[i] = CRGB(0, 255, 0);
    } else if (stageStartTime + STARTUP_SPARKLE_DUR > mm) {
        for (int i = 0; i < g_settings.led_count; i++) {
            if (random8(30) < 28) {
                g_leds[i] = CRGB(0, 255, 0);
            } else {
                int flicker = random8(250);
                g_leds[i] = CRGB(flicker, 150, flicker);
            }
        }
    } else if (stageStartTime + STARTUP_FADE_DUR > mm) {
        int n = _max(map((mm - stageStartTime), STARTUP_SPARKLE_DUR, STARTUP_FADE_DUR, 0,
                         g_settings.led_count), 0);
        int brightness = _max(map((mm - stageStartTime), STARTUP_SPARKLE_DUR, STARTUP_FADE_DUR,
                                  255, 0), 0);
        for (int i = n; i < g_settings.led_count; i++) {
            g_leds[i] = CRGB(0, brightness, 0);
        }
    }
    SFXFreqSweepWarble(STARTUP_FADE_DUR, millis() - stageStartTime, 40, 400, 20);
}

void tickEnemies() {
    for (int i = 0; i < ENEMY_COUNT; i++) {
        Enemy& e = enemyPool[i];
        if (!e.Alive()) continue;
        e.Tick();

        // Any alive, attacking player in range kills the enemy.
        bool enemy_killed = false;
        for (uint8_t pi = 0; pi < kMaxPlayers; ++pi) {
            Player& p = players[pi];
            if (!p.active || !p.alive || !p.attacking) continue;
            if (e._pos > p.position - (p.attack_width / 2) &&
                e._pos < p.position + (p.attack_width / 2)) {
                e.Kill();
                SFXkill();
                p.score += 1;
                enemy_killed = true;
                break;
            }
        }
        if (enemy_killed) continue;

        if (inLava(e._pos)) {
            e.Kill();
            SFXkill();
            continue;
        }

        if (e.Alive()) g_leds[getLED(e._pos)] = CRGB(255, 0, 0);

        // Enemy touching a player -> that player dies.
        for (uint8_t pi = 0; pi < kMaxPlayers; ++pi) {
            Player& p = players[pi];
            if (!p.active || !p.alive) continue;
            if ((e.playerSide ==  1 && e._pos <= p.position) ||
                (e.playerSide == -1 && e._pos >= p.position)) {
                die(pi);
            }
        }
    }
}

void tickBoss() {
    if (!boss.Alive()) return;

    boss._ticks++;
    for (int i = getLED(boss._pos - BOSS_WIDTH / 2);
         i <= getLED(boss._pos + BOSS_WIDTH / 2); i++) {
        g_leds[i] = CRGB::DarkRed;
        g_leds[i] %= 100;
    }

    // Damage from any player; boss kills any player in its hitbox.
    for (uint8_t pi = 0; pi < kMaxPlayers; ++pi) {
        Player& p = players[pi];
        if (!p.active || !p.alive) continue;

        if (getLED(p.position) > getLED(boss._pos - BOSS_WIDTH / 2) &&
            getLED(p.position) < getLED(boss._pos + BOSS_WIDTH)) {
            die(pi);
            continue;
        }

        if (p.attacking) {
            if ((getLED(p.position + (p.attack_width / 2)) >= getLED(boss._pos - BOSS_WIDTH / 2) &&
                 getLED(p.position + (p.attack_width / 2)) <= getLED(boss._pos + BOSS_WIDTH / 2)) ||
                (getLED(p.position - (p.attack_width / 2)) <= getLED(boss._pos + BOSS_WIDTH / 2) &&
                 getLED(p.position - (p.attack_width / 2)) >= getLED(boss._pos - BOSS_WIDTH / 2))) {
                boss.Hit();
                if (boss.Alive()) moveBoss();
                else { spawnPool[0].Kill(); spawnPool[1].Kill(); }
                p.score += 10;
            }
        }
    }
}

void drawPlayer(uint8_t idx) {
    Player& p = players[idx];
    if (!p.active || !p.alive) return;
    g_leds[getLED(p.position)] = p.cursor_color;
}

void drawExit() {
    if (!boss.Alive()) {
        g_leds[g_settings.led_count - 1] = CRGB(0, 0, 255);
    }
}

void tickSpawners() {
    long mm = millis();
    for (int s = 0; s < SPAWN_COUNT; s++) {
        if (spawnPool[s].Alive() && spawnPool[s]._activate < mm) {
            if (spawnPool[s]._lastSpawned + spawnPool[s]._rate < mm ||
                spawnPool[s]._lastSpawned == 0) {
                spawnEnemy(spawnPool[s]._pos, spawnPool[s]._dir, spawnPool[s]._sp, 0,
                           leader_position());
                spawnPool[s]._lastSpawned = mm;
            }
        }
    }
}

void tickLava() {
    long mm = millis();
    for (int i = 0; i < LAVA_COUNT; i++) {
        Lava LP = lavaPool[i];
        if (LP.Alive()) {
            LP.Update();
            int A = getLED(LP._left);
            int B = getLED(LP._right);
            if (LP._state == Lava::OFF) {
                if (LP._lastOn + LP._offtime < mm) {
                    LP._state = Lava::ON;
                    LP._lastOn = mm;
                }
                for (int p = A; p <= B; p++) {
                    int flicker = random8(LAVA_OFF_BRIGHTNESS);
                    g_leds[p] = CRGB(LAVA_OFF_BRIGHTNESS + flicker,
                                     (LAVA_OFF_BRIGHTNESS + flicker) / 1.5f, 0);
                }
            } else {
                if (LP._lastOn + LP._ontime < mm) {
                    LP._state = Lava::OFF;
                    LP._lastOn = mm;
                }
                for (int p = A; p <= B; p++) {
                    if (random8(30) < 29) g_leds[p] = CRGB(150, 0, 0);
                    else                  g_leds[p] = CRGB(180, 100, 0);
                }
            }
        }
        lavaPool[i] = LP;
    }
}

bool tickParticles() {
    bool active = false;
    uint8_t brightness;
    for (int pidx = 0; pidx < PARTICLE_COUNT; pidx++) {
        if (particlePool[pidx].Alive()) {
            particlePool[pidx].Tick(USE_GRAVITY);
            if (particlePool[pidx]._power < 5) {
                brightness = (5 - particlePool[pidx]._power) * 10;
                g_leds[getLED(particlePool[pidx]._pos)] += CRGB(brightness, brightness / 2, brightness / 2);
            } else {
                g_leds[getLED(particlePool[pidx]._pos)] += CRGB(particlePool[pidx]._power, 0, 0);
            }
            active = true;
        }
    }
    return active;
}

void tickConveyors() {
    int b, speed, n, ss, ee, led;
    long m = 10000 + millis();
    for (auto& pl : players) pl.positionModifier = 0;
    int levels = 5;

    for (int i = 0; i < CONVEYOR_COUNT; i++) {
        if (conveyorPool[i]._alive) {
            speed = constrain(conveyorPool[i]._speed, -MAX_PLAYER_SPEED + 1, MAX_PLAYER_SPEED - 1);
            ss = getLED(conveyorPool[i]._startPoint);
            ee = getLED(conveyorPool[i]._endPoint);
            for (led = ss; led < ee; led++) {
                n = (-led + (m / 100)) % levels;
                if (speed < 0) n = (led + (m / 100)) % levels;
                b = map(n, 5, 0, 0, CONVEYOR_BRIGHTNESS);
                if (b > 0) g_leds[led] = CRGB(0, 0, b);
            }
            for (auto& pl : players) {
                if (!pl.active || !pl.alive) continue;
                if (pl.position > conveyorPool[i]._startPoint &&
                    pl.position < conveyorPool[i]._endPoint) {
                    pl.positionModifier = speed;
                }
            }
        }
    }
}

void tickBossKilled(long mm) {
    static uint8_t gHue = 0;
    FastLED.setBrightness(255);
    int brightness = 0;
    FastLED.clear();

    if (stageStartTime + 6500 > mm) {
        gHue++;
        fill_rainbow(g_leds, g_settings.led_count, gHue, 7);
        if (random8() < 200) g_leds[random16(g_settings.led_count)] += CRGB::White;
        SFXbosskilled();
    } else if (stageStartTime + 7000 > mm) {
        int n = _max(map((mm - stageStartTime), 5000, 5500, g_settings.led_count, 0), 0);
        for (int i = 0; i < n; i++) {
            brightness = (sin(((i * 10) + mm) / 500.0f) + 1) * 255;
            g_leds[i].setHSV(brightness, 255, 50);
        }
        SFXcomplete();
    } else {
        nextLevel();
    }
}

void tickDie(long mm) {
    const int duration = 200;
    const int width = 20;
    // Render death visual for any player currently in DEAD visual state.
    // (All recently-killed players; we use the latest killTime.)
    int anchor_pos = 0;
    long anchor_kill = 0;
    for (auto& p : players) {
        if (p.active && !p.alive && p.killTime > anchor_kill) {
            anchor_kill = p.killTime;
            anchor_pos  = p.position;
        }
    }
    if (stageStartTime + duration > mm) {
        int brightness = map((mm - stageStartTime), 0, duration, 255, 150);
        int n = _max(map((mm - stageStartTime), 0, duration, getLED(anchor_pos),
                         getLED(anchor_pos) + width), 0);
        for (int i = getLED(anchor_pos); i <= n; i++) {
            g_leds[i] = CRGB(255, brightness, brightness);
        }
        n = _max(map((mm - stageStartTime), 0, duration, getLED(anchor_pos),
                     getLED(anchor_pos) - width), 0);
        for (int i = getLED(anchor_pos); i >= n; i--) {
            g_leds[i] = CRGB(255, brightness, brightness);
        }
    }
}

void tickGameover(long mm) {
    int brightness = 0;
    int anchor_pos = 0;
    long anchor_kill = 0;
    for (auto& p : players) {
        if (p.active && p.killTime > anchor_kill) {
            anchor_kill = p.killTime;
            anchor_pos  = p.position;
        }
    }
    if (stageStartTime + GAMEOVER_SPREAD_DURATION > mm) {
        int n = _max(map((mm - stageStartTime), 0, GAMEOVER_SPREAD_DURATION,
                         getLED(anchor_pos), g_settings.led_count), 0);
        for (int i = getLED(anchor_pos); i <= n; i++) g_leds[i] = CRGB(255, 0, 0);
        n = _max(map((mm - stageStartTime), 0, GAMEOVER_SPREAD_DURATION,
                     getLED(anchor_pos), 0), 0);
        for (int i = getLED(anchor_pos); i >= n; i--) g_leds[i] = CRGB(255, 0, 0);
        SFXgameover();
    } else if (stageStartTime + GAMEOVER_FADE_DURATION > mm) {
        int n = _max(map((mm - stageStartTime), GAMEOVER_FADE_DURATION,
                         GAMEOVER_SPREAD_DURATION, g_settings.led_count, 0), 0);
        brightness = map((mm - stageStartTime), GAMEOVER_SPREAD_DURATION,
                         GAMEOVER_FADE_DURATION, 200, 0);
        for (int i = 0; i <= n; i++) g_leds[i] = CRGB(brightness, 0, 0);
        SFXcomplete();
    }
}

void tickWin(long mm) {
    int brightness = 0;
    FastLED.clear();
    if (stageStartTime + WIN_FILL_DURATION > mm) {
        int n = _max(map((mm - stageStartTime), 0, WIN_FILL_DURATION, g_settings.led_count, 0), 0);
        for (int i = g_settings.led_count; i >= n; i--) {
            brightness = g_settings.led_brightness;
            g_leds[i] = CRGB(0, brightness, 0);
        }
        SFXwin();
    } else if (stageStartTime + WIN_CLEAR_DURATION > mm) {
        int n = _max(map((mm - stageStartTime), WIN_FILL_DURATION, WIN_CLEAR_DURATION,
                         g_settings.led_count, 0), 0);
        for (int i = 0; i < n; i++) {
            brightness = g_settings.led_brightness;
            g_leds[i] = CRGB(0, brightness, 0);
        }
        SFXwin();
    } else if (stageStartTime + WIN_OFF_DURATION > mm) {
        g_leds[0] = CRGB(0, g_settings.led_brightness, 0);
    } else {
        nextLevel();
    }
}

void drawLives() {
    // Lives strip shows player 0 for a quick visual check; other players' lives
    // are surfaced via telemetry HUD on their phones.
    SFXcomplete();
    FastLED.clear();
    int pos = 0;
    int lives_display = players[0].active ? players[0].lives : g_settings.lives_per_level;
    for (int i = 0; i < lives_display; i++) {
        for (int j = 0; j < 4; j++) {
            g_leds[pos++] = CRGB(0, 255, 0);
            led_output::show();
        }
        g_leds[pos++] = CRGB(0, 0, 0);
        delay(20);
    }
    led_output::show();
    delay(400);
    FastLED.clear();
}

void drawAttack(uint8_t idx) {
    Player& p = players[idx];
    if (!p.active || !p.alive || !p.attacking) return;
    int n = map(millis() - p.attackMillis, 0, ATTACK_DURATION, 100, 5);
    for (int i = getLED(p.position - (p.attack_width / 2)) + 1;
         i <= getLED(p.position + (p.attack_width / 2)) - 1; i++) {
        g_leds[i] = CRGB(0, 0, n);
    }
    if (n > 90) {
        n = 255;
        g_leds[getLED(p.position)] = CRGB(255, 255, 255);
    } else {
        n = 0;
        g_leds[getLED(p.position)] = p.cursor_color;
    }
    g_leds[getLED(p.position - (p.attack_width / 2))] = CRGB(n, n, 255);
    g_leds[getLED(p.position + (p.attack_width / 2))] = CRGB(n, n, 255);
}

int getLED(int pos) {
    return constrain((int)map(pos, 0, VIRTUAL_LED_COUNT, 0, g_settings.led_count - 1),
                     0, g_settings.led_count - 1);
}

bool inLava(int pos) {
    for (int i = 0; i < LAVA_COUNT; i++) {
        Lava LP = lavaPool[i];
        if (LP.Alive() && LP._state == Lava::ON) {
            if (LP._left <= pos && LP._right >= pos) return true;
        }
    }
    return false;
}

void updateLives() { drawLives(); }

void save_game_stats(bool bossKill) {
    int total_score = 0;
    int best_score  = 0;
    for (auto& p : players) if (p.active) {
        total_score += p.score;
        if (p.score > best_score) best_score = p.score;
    }
    g_settings.games_played += 1;
    g_settings.total_points += total_score;
    if (best_score > g_settings.high_score) g_settings.high_score = best_score;
    if (bossKill) g_settings.boss_kills += 1;
    twang_settings::save();
}

// -- Screensaver -----------------------------------------------------------
void screenSaverTick() {
    long mm = millis();
    int mode = (mm / 30000) % 4;
    SFXcomplete();
    if      (mode == 0) Fire2012();
    else if (mode == 1) sinelon();
    else if (mode == 2) juggle();
    else                random_LED_flashes();
}

void Fire2012() {
    static byte heat[VIRTUAL_LED_COUNT];
    bool gReverse = false;
    for (int i = 0; i < g_settings.led_count; i++) {
        heat[i] = qsub8(heat[i], random8(0, ((COOLING * 10) / g_settings.led_count) + 2));
    }
    for (int k = g_settings.led_count - 1; k >= 2; k--) {
        heat[k] = (heat[k - 1] + heat[k - 2] + heat[k - 2]) / 3;
    }
    if (random8() < SPARKING) {
        int y = random8(7);
        heat[y] = qadd8(heat[y], random8(160, 255));
    }
    for (int j = 0; j < g_settings.led_count; j++) {
        CRGB color = HeatColor(heat[j]);
        int pixel = gReverse ? (g_settings.led_count - 1 - j) : j;
        g_leds[pixel] = color;
    }
}

void LED_march() {
    long mm = millis();
    for (int i = 0; i < g_settings.led_count; i++) g_leds[i].nscale8(250);
    int n = (mm / 250) % 10;
    int c = 20 + ((sin(mm / 5000.0f) + 1) * 33);
    for (int i = 0; i < g_settings.led_count; i++) {
        if (i % 10 == n) g_leds[i] = CHSV(c, 255, 150);
    }
}

void random_LED_flashes() {
    long mm = millis();
    for (int i = 0; i < g_settings.led_count; i++) g_leds[i].nscale8(250);
    randomSeed(mm);
    for (int i = 0; i < g_settings.led_count; i++) {
        if (random8(20) == 0) g_leds[i] = CHSV(25, 255, 100);
    }
}

void sinelon() {
    static uint8_t gHue = 0;
    gHue++;
    fadeToBlackBy(g_leds, g_settings.led_count, 20);
    int pos = beatsin16(13, 0, g_settings.led_count);
    g_leds[pos] += CHSV(gHue, 255, 192);
}

void juggle() {
    fadeToBlackBy(g_leds, g_settings.led_count, 20);
    byte dothue = 0;
    for (int i = 0; i < 4; i++) {
        g_leds[beatsin16(i + 7, 0, g_settings.led_count - 1)] |= CHSV(dothue, 200, 255);
        dothue += 64;
    }
}

// -- SFX --------------------------------------------------------------------
long map_constrain(long x, long in_min, long in_max, long out_min, long out_max) {
    if (in_max > in_min) x = constrain(x, in_min, in_max);
    else                 x = constrain(x, in_max, in_min);
    return map(x, in_min, in_max, out_min, out_max);
}

void SFXFreqSweepWarble(int duration, int elapsedTime, int freqStart, int freqEnd, int warble) {
    int freq = map_constrain(elapsedTime, 0, duration, freqStart, freqEnd);
    if (warble) warble = map(sin(millis() / 20.0f) * 1000.0f, -1000, 1000, 0, warble);
    sound(freq + warble, g_settings.audio_volume);
}

void SFXFreqSweepNoise(int duration, int elapsedTime, int freqStart, int freqEnd, uint8_t noiseFactor) {
    int freq = (elapsedTime > duration) ? freqEnd :
                                          map(elapsedTime, 0, duration, freqStart, freqEnd);
    if (noiseFactor) noiseFactor = noiseFactor - random8(noiseFactor / 2);
    sound(freq + noiseFactor, g_settings.audio_volume);
}

void SFXtilt(int amount) {
    if (amount == 0) { SFXcomplete(); return; }
    int f = map(abs(amount), 0, 90, 80, 900) + random8(100);
    int vol = map(abs(amount), 0, 90, g_settings.audio_volume / 2, g_settings.audio_volume * 3 / 4);
    sound(f, vol);
}

void SFXattacking() {
    int freq = map(sin(millis() / 2.0f) * 1000.0f, -1000, 1000, 500, 600);
    if (random8(5) == 0) freq *= 3;
    sound(freq, g_settings.audio_volume);
}
void SFXdead() {
    long latest = 0;
    for (auto& p : players) if (p.killTime > latest) latest = p.killTime;
    SFXFreqSweepNoise(1000, millis() - latest, 1000, 10, 200);
}
void SFXgameover() {
    long latest = 0;
    for (auto& p : players) if (p.killTime > latest) latest = p.killTime;
    SFXFreqSweepWarble(GAMEOVER_SPREAD_DURATION, millis() - latest, 440, 20, 60);
}
void SFXkill()        { sound(2000, g_settings.audio_volume); }
void SFXwin()         { SFXFreqSweepWarble(WIN_OFF_DURATION, millis() - stageStartTime, 40, 400, 20); }
void SFXbosskilled()  { SFXFreqSweepWarble(7000, millis() - stageStartTime, 75, 1100, 60); }
void SFXcomplete()    { soundOff(); }

} // namespace
