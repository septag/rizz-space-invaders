#include "sx/allocator.h"
#include "sx/bitarray.h"
#include "sx/macros.h"
#include "sx/math.h"
#include "sx/string.h"
#include "sx/timer.h"
#include "sx/os.h"
#include "sx/rng.h"

#include "rizz/2dtools.h"
#include "rizz/imgui-extra.h"
#include "rizz/imgui.h"
#include "rizz/input.h"
#include "rizz/rizz.h"
#include "rizz/sound.h"

#define CUTE_C2_IMPLEMENTATION
SX_PRAGMA_DIAGNOSTIC_PUSH()
SX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG("-Wswitch")
SX_PRAGMA_DIAGNOSTIC_IGNORED_CLANG("-Wunused-function")
#include "cute_c2.h"
SX_PRAGMA_DIAGNOSTIC_POP()

RIZZ_STATE static rizz_api_core* the_core;
RIZZ_STATE static rizz_api_gfx* the_gfx;
RIZZ_STATE static rizz_api_app* the_app;
RIZZ_STATE static rizz_api_imgui* the_imgui;
RIZZ_STATE static rizz_api_asset* the_asset;
RIZZ_STATE static rizz_api_camera* the_camera;
RIZZ_STATE static rizz_api_vfs* the_vfs;
RIZZ_STATE static rizz_api_2d* the_2d;
RIZZ_STATE static rizz_api_plugin* the_plugin;
RIZZ_STATE static rizz_api_camera* the_camera;
RIZZ_STATE static rizz_api_input* the_input;
RIZZ_STATE static rizz_api_snd* the_sound;

#define ENEMIES_PER_ROW 11
#define NUM_ROWS 5
#define MAX_ENEMIES (ENEMIES_PER_ROW * NUM_ROWS)
#define MAX_BULLETS 20
#define GAME_BOARD_WIDTH 1.0f
#define GAME_BOARD_HEIGHT 1.2f
#define ENEMY_WAIT_DURATION 0.5f
#define PLAYER_BULLET_INTERVAL 0.5f
#define ENEMY_EXPLODE_TIME 0.2f
#define ENEMY_SHOOT_INTERVAL 1.0f
#define ENEMY_TILE_MOVE_DURATION 0.5f
#define PLAYER_EXPLOSION_DURATION 1.0f
#define NUM_COVERS 4
#define HEARTBEAT_INTERVAL 1.5f
#define NUM_LIVES 3
#define GAME_STATE_DURATION 2000

#define KEY_LEFT 1
#define KEY_RIGHT 2
#define KEY_SHOOT 3
#define KEY_MOVEX_ANALOG 4

#define SCORE_FOURCC sx_makefourcc('S', 'C', 'O', 'R')

typedef enum enemy_direction_t {
    ENEMY_MOVEMENT_RIGHT = 0,
    ENEMY_MOVEMENT_DOWN,
    ENEMY_MOVEMENT_LEFT
} enemy_direction_t;

typedef enum bullet_type_t {
    BULLET_TYPE_PLAYER = 0,
    BULLET_TYPE_ALIEN1,
    BULLET_TYPE_COUNT
} bullet_type_t;

typedef enum sound_type_t {
    SOUND_EXPLODE1 = 0,
    SOUND_EXPLODE2,
    SOUND_EXPLODE3,
    SOUND_EXPLODE4,
    SOUND_HEARTBEAT,
    SOUND_HIT,
    SOUND_SHOOT,
    SOUND_WIN,
    SOUND_BONUS,
    SOUND_SAUCER,
    SOUND_COUNT
} sound_type_t;

typedef enum render_stage_t {
    RENDER_STAGE_GAME = 0,
    RENDER_STAGE_UI,
    RENDER_STAGE_COUNT
} render_stage_t;

typedef enum game_state_t {
    GAME_STATE_INGAME = 0,
    GAME_STATE_GAMEOVER,
    GAME_STATE_WIN
} game_state_t;

typedef struct enemy_t {
    sx_vec2 start_pos;
    sx_vec2 target_pos;
    sx_vec2 pos;
    sound_type_t explode_sound;
    float wait_tm;
    float move_tm;
    float wait_duration;
    int xstep;
    enemy_direction_t dir;
    float shoot_wait_interval;
    int hit_score;
    bool move;
    bool allow_next_move;
    bool dead;
} enemy_t;

typedef struct player_t {
    sx_vec2 pos;
    float bullet_tm;
    rizz_sprite sprite;
    float speed;
    bool dead;
} player_t;

typedef struct cover_t {
    sx_vec2 pos;
    int health;
    bool dead;
} cover_t;

typedef struct saucer_t {
    bool dead;
    sx_vec2 pos;
    rizz_sprite sprite;
    float t;
    float speed;
    float wait_tm;
    float wait_duration;
    int hit_score;
} saucer_t;

typedef struct bullet_t {
    sx_vec2 pos;
    bullet_type_t type;
    rizz_sprite sprite;
    float speed;
    int damage;
} bullet_t;

typedef struct explosion_t {
    sx_vec2 pos;
    rizz_sprite sprite;
    float wait_tm;
} explosion_t;

typedef struct collision_data_t {
    int bullet_index;
    int hit_index;
} collision_data_t;

typedef enum debugger_t {
    DEBUGGER_MEMORY = 0,
    DEBUGGER_LOG,
    DEBUGGER_GRAPHICS,
    DEBUGGER_SPRITES,
    DEBUGGER_SOUND,
    DEBUGGER_INPUT,
    DEBUGGER_COUNT
} debugger_t;

typedef struct game_t {
    sx_alloc* alloc;
    sx_rng rng;
    enemy_t enemies[MAX_ENEMIES];
    enemy_t dummy_enemy;
    cover_t covers[NUM_COVERS];
    rizz_sprite bullet_sprites[BULLET_TYPE_COUNT];
    rizz_sprite enemy_sprites[MAX_ENEMIES];
    rizz_sprite_animclip enemy_clips[MAX_ENEMIES];
    rizz_sprite enemy_explosion_sprite;
    rizz_sprite bounds_explosion_sprite;
    rizz_sprite cover_sprite;
    rizz_asset sounds[SOUND_COUNT];
    bullet_t bullets[MAX_BULLETS];
    saucer_t saucer;
    int num_bullets;
    int num_bullets_spawned;
    explosion_t explosions[MAX_BULLETS];
    int num_explosions;
    int num_explosions_spawned;
    player_t player;
    rizz_asset game_atlas;
    rizz_camera cam;
    rizz_input_device keyboard;
    rizz_input_device gamepad;
    float tile_size;
    bool enemy_explosion;
    float enemy_explosion_tm;
    sx_vec2 enemy_explosion_pos;
    float enemy_shoot_tm;
    float enemy_shoot_interval;
    float game_over_point;
    explosion_t player_explosion;
    int player_lives;
    bool player_died;
    float heartbeat_tm;
    int player_score;
    rizz_gfx_stage render_stages[RENDER_STAGE_COUNT];
    rizz_asset font;
    game_state_t state;
    int stage;
    int high_score;
    bool show_dev_menu;
    bool show_debuggers[DEBUGGER_COUNT];
} game_t;

RIZZ_STATE static game_t the_game;

static void create_sounds(void)
{
    static const char* sound_files[SOUND_COUNT] = {
        "explode1.wav",
        "explode2.wav",
        "explode3.wav",
        "explode4.wav",
        "heartbeat.wav",
        "hit.wav",
        "shoot.wav",
        "win.wav",
        "bonus.wav",
        "saucer.wav"
    };

    char filepath[128];
    rizz_snd_load_params sparams = { 0 };
    for (int i = 0; i < SOUND_COUNT; i++) {
        sx_snprintf(filepath, sizeof(filepath), "/assets/sounds/%s", sound_files[i]);
        the_game.sounds[i] = the_asset->load("sound", filepath, &sparams, 0, NULL, 0);
    }
}

static void save_high_score(void) 
{
    sx_file f;
    if (sx_file_open(&f, "highscore.dat", SX_FILE_WRITE)) {
        uint32_t sign = SCORE_FOURCC;
        sx_file_write(&f, &sign, sizeof(sign));
        sx_file_write(&f, &the_game.high_score, sizeof(the_game.high_score));
        sx_file_close(&f);
    }
}

static void load_high_score(void)
{
    sx_file f;
    if (sx_file_open(&f, "highscore.dat", SX_FILE_READ)) {
        uint32_t sign;
        sx_file_read(&f, &sign, sizeof(sign));
        if (sign == SCORE_FOURCC) {
            sx_file_read(&f, &the_game.high_score, sizeof(the_game.high_score));
        }
        sx_file_close(&f);
    }
}

static void refresh_game(void)
{
    float tile_size = the_game.tile_size;
    float half_width = GAME_BOARD_WIDTH * 0.5f;
    float start_x = -half_width + 2.5f * tile_size;
    float x = start_x;
    float y = GAME_BOARD_HEIGHT * 0.5f - tile_size - tile_size*0.5f*((float)the_game.stage);

    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (i % 11 == 0) {
            x = start_x;
            y -= tile_size;
        }

        the_game.enemies[i].dead = false;
        the_game.enemies[i].xstep = 0;
        the_game.enemies[i].shoot_wait_interval = 0;
        the_game.enemies[i].move_tm = 0;
        the_game.enemies[i].wait_tm = 0;
        the_game.enemies[i].move = 0;
        the_game.enemies[i].pos = sx_vec2f(x, y);
        the_game.enemies[i].start_pos = the_game.enemies[i].pos;
        the_game.enemies[i].target_pos = the_game.enemies[i].pos;
        the_game.enemies[i].dir = 0;

        float dd = ((float)i / (float)MAX_ENEMIES);
        the_game.enemies[i].wait_duration = dd + ENEMY_WAIT_DURATION;
        the_game.enemies[i].allow_next_move = true;

        x += tile_size;
    }

    for (int i = 0; i < NUM_COVERS; i++) {
        the_game.covers[i].dead = false;
        the_game.covers[i].health = 100;
    }

    sx_memset(&the_game.dummy_enemy, 0x0, sizeof(the_game.dummy_enemy));
    the_game.dummy_enemy.allow_next_move = true;
    the_game.dummy_enemy.wait_duration = 1.0f + ENEMY_WAIT_DURATION;
    the_game.dummy_enemy.dead = true;

    player_t* player = &the_game.player;
    player->bullet_tm = 0;
    player->pos = sx_vec2f(0, -GAME_BOARD_HEIGHT * 0.5f + the_game.tile_size * 2.0f);
    player->bullet_tm = PLAYER_BULLET_INTERVAL;

    the_game.num_bullets_spawned = the_game.num_bullets = 0;
    the_game.num_explosions_spawned = the_game.num_explosions = 0;

    the_game.saucer.dead = true;

    the_sound->stop_all();

    the_game.player_lives = NUM_LIVES;
    the_game.stage = 0;
}

rizz_coro_declare(state_control)
{
    if (the_game.player_score > the_game.high_score) {
        the_game.high_score = the_game.player_score;
        save_high_score();
    }
    
    if (the_game.state == GAME_STATE_GAMEOVER) {
        the_game.player_score = 0;
    }

    rizz_coro_wait(GAME_STATE_DURATION);
    refresh_game();
    the_game.state = GAME_STATE_INGAME;
    rizz_coro_end();
}


static void create_enemies()
{
    float half_width = GAME_BOARD_WIDTH * 0.5f;
    float tile_size = the_game.tile_size;

    static const rizz_sprite_animclip_frame_desc enemy1_frames[] = { { .name = "enemy1-a.png" },
                                                                     { .name = "enemy1-b.png" },
                                                                     { 0 } };
    static const rizz_sprite_animclip_frame_desc enemy2_frames[] = { { .name = "enemy2-a.png" },
                                                                     { .name = "enemy2-b.png" },
                                                                     { 0 } };
    static const rizz_sprite_animclip_frame_desc enemy3_frames[] = { { .name = "enemy3-a.png" },
                                                                     { .name = "enemy3-b.png" },
                                                                     { 0 } };

    // clang-format off
    static const int layout[MAX_ENEMIES] = {
        2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
    };

    static const char* enemy_names[3] = {
        "enemy3",
        "enemy1",
        "enemy2"
    };

    static const int hit_scores[3] = {
        10,
        15,
        20
    };

    static sound_type_t enemy_explode_sounds[3] = {
        SOUND_EXPLODE3,
        SOUND_EXPLODE1,
        SOUND_EXPLODE2
    };

    static const rizz_sprite_animclip_frame_desc* frame_descs[3] = {
        enemy3_frames, 
        enemy1_frames,
        enemy2_frames
    };
    // clang-format on

    float start_x = -half_width + 2.5f * tile_size;
    float x = start_x;
    float y = GAME_BOARD_HEIGHT * 0.5f - tile_size;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (i % 11 == 0) {
            x = start_x;
            y -= tile_size;
        }

        int enemy = layout[i];

        the_game.enemy_clips[i] = the_2d->sprite.animclip_create(
            &(rizz_sprite_animclip_desc){ .atlas = the_game.game_atlas,
                                          .frames = frame_descs[enemy],
                                          .fps = sx_rng_genf(&the_game.rng) * 0.1f + 0.8f,
                                          .alloc = the_game.alloc });

        the_game.enemy_sprites[i] =
            the_2d->sprite.create(&(rizz_sprite_desc){ .name = enemy_names[enemy],
                                                    .atlas = the_game.game_atlas,
                                                    .size = sx_vec2f(tile_size * 0.8f, 0),
                                                    .color = sx_colorn((0xffffffff)),
                                                    .clip = the_game.enemy_clips[i] });

        the_game.enemies[i].pos = sx_vec2f(x, y);
        the_game.enemies[i].start_pos = the_game.enemies[i].pos;
        the_game.enemies[i].explode_sound = enemy_explode_sounds[enemy];

        float dd = ((float)i / (float)MAX_ENEMIES);
        the_game.enemies[i].wait_duration = dd + ENEMY_WAIT_DURATION;
        the_game.enemies[i].allow_next_move = true;
        the_game.enemies[i].hit_score = hit_scores[enemy];

        switch (enemy) {
        case 0:
            the_game.enemies[i].shoot_wait_interval = 1.0f;
            break;
        case 1:
            the_game.enemies[i].shoot_wait_interval = 2.0f;
            break;
        case 2:
            the_game.enemies[i].shoot_wait_interval = 3.0f;
            break;
        }

        x += tile_size;
    }

    the_game.dummy_enemy.allow_next_move = true;
    the_game.dummy_enemy.dead = true;
    the_game.dummy_enemy.wait_duration = 1.0f + ENEMY_WAIT_DURATION;
}

static void create_player()
{
    player_t* player = &the_game.player;
    player->sprite = the_2d->sprite.create(&(rizz_sprite_desc){ .name = "player.png",
                                                             .atlas = the_game.game_atlas,
                                                             .size = {{the_game.tile_size, 0}},
                                                             .color = sx_colorn(0xffffffff) });
    player->pos = sx_vec2f(0, -GAME_BOARD_HEIGHT * 0.5f + the_game.tile_size * 2.0f);
    player->speed = 0.1f;
    player->bullet_tm = PLAYER_BULLET_INTERVAL;
}

static void create_bullet_sprites()
{
    static const char* bullet_names[BULLET_TYPE_COUNT] = { "bullet0.png", "bullet1.png" };

    const float bullet_sizes[BULLET_TYPE_COUNT] = { the_game.tile_size * 0.5f,
                                                    the_game.tile_size * 0.5f };
    const float bullet_origins[BULLET_TYPE_COUNT] = { -0.5f, 0.5f };
    for (int i = 0; i < BULLET_TYPE_COUNT; i++) {
        the_game.bullet_sprites[i] =
            the_2d->sprite.create(&(rizz_sprite_desc){ .name = bullet_names[i],
                                                    .atlas = the_game.game_atlas,
                                                    .size = sx_vec2f(0, bullet_sizes[i]),
                                                    .origin = sx_vec2f(0, bullet_origins[i]),
                                                    .color = sx_colorn(0xffffffff) });
    }
}

static void create_explosion_sprites()
{
    the_game.enemy_explosion_sprite =
        the_2d->sprite.create(&(rizz_sprite_desc){ .name = "explode.png",
                                                .atlas = the_game.game_atlas,
                                                .size = sx_vec2f(the_game.tile_size, 0),
                                                .color = sx_colorn(0xffffffff) });
    the_game.bounds_explosion_sprite =
        the_2d->sprite.create(&(rizz_sprite_desc){ .name = "explode2.png",
                                                .atlas = the_game.game_atlas,
                                                .size = sx_vec2f(the_game.tile_size, 0),
                                                .origin = sx_vec2f(0, -0.5f) });
}

static void create_bullet(sx_vec2 pos, bullet_type_t type)
{
    sx_assert(type < BULLET_TYPE_COUNT);

    static const float bullet_speeds[BULLET_TYPE_COUNT] = { 1.5f, 0.75f };
    static const int bullet_damages[BULLET_TYPE_COUNT] = { 10, 20 };

    bullet_t bullet = { .pos = pos,
                        .type = type,
                        .sprite = the_game.bullet_sprites[type],
                        .damage = bullet_damages[type],
                        .speed =
                            bullet_speeds[type] * (type == BULLET_TYPE_PLAYER ? 1.0f : -1.0f) };

    ++the_game.num_bullets_spawned;

    int index;
    if (the_game.num_bullets < MAX_BULLETS) {
        index = the_game.num_bullets;
        ++the_game.num_bullets;
    } else {
        index = the_game.num_bullets_spawned % MAX_BULLETS;
    }

    the_game.bullets[index] = bullet;
}

static void create_explosion(sx_vec2 pos, rizz_sprite sprite)
{
    explosion_t explosion = { .pos = pos, .sprite = sprite };
    ++the_game.num_explosions_spawned;

    int index;
    if (the_game.num_explosions < MAX_BULLETS) {
        index = the_game.num_explosions;
        ++the_game.num_explosions;
    } else {
        index = the_game.num_explosions_spawned % MAX_BULLETS;
    }

    the_game.explosions[index] = explosion;
}

static void create_saucer(void)
{
    rizz_sprite sprite =
        the_2d->sprite.create(&(rizz_sprite_desc){ .name = "saucer.png",
                                                .atlas = the_game.game_atlas,
                                                .size = sx_vec2f(the_game.tile_size, 0) });
    the_game.saucer = (saucer_t){ .dead = true,
                                  .sprite = sprite,
                                  .wait_duration = 30.0f + (sx_rng_genf(&the_game.rng) * 20.0f - 10.0f),
                                  .hit_score = 100 };
}

static void remove_bullet(int index)
{
    if (index < the_game.num_bullets - 1) {
        the_game.bullets[index] = the_game.bullets[the_game.num_bullets - 1];
    }

    --the_game.num_bullets;
}

static void create_covers(void)
{
    float tile_size = GAME_BOARD_WIDTH / 9.0f;
    float half_width = GAME_BOARD_WIDTH * 0.5f;
    int count = 0;
    for (float x = -half_width + tile_size; x < half_width && count < NUM_COVERS;
         x += tile_size * 2.0f) {
        cover_t* cover = &the_game.covers[count++];
        cover->pos = sx_vec2f(x, -GAME_BOARD_HEIGHT * 0.5f + tile_size * 2.0f);
        cover->health = 100;
    }

    the_game.cover_sprite = the_2d->sprite.create(&(rizz_sprite_desc){ .name = "cover.png",
                                                                       .atlas = the_game.game_atlas,
                                                                       .size = sx_vec2f(tile_size, 0),
                                                                       .origin = sx_vec2f(-0.5f, -0.5f) });
}

static bool init()
{
    the_game.alloc = the_core->trace_alloc_create("Game",  RIZZ_MEMOPTION_INHERIT, NULL, the_core->heap_alloc());

    sx_rng_seed_time(&the_game.rng );

    the_game.render_stages[RENDER_STAGE_GAME] = the_gfx->stage_register("game", (rizz_gfx_stage){ .id = 0 });
    the_game.render_stages[RENDER_STAGE_UI] = the_gfx->stage_register("ui", the_game.render_stages[RENDER_STAGE_GAME]);
    sx_assert(the_game.render_stages[RENDER_STAGE_GAME].id);
    sx_assert(the_game.render_stages[RENDER_STAGE_UI].id);

    the_vfs->mount("assets", "/assets");

    // camera
    // projection: setup for ortho, total-width = GAME_BOARD_WIDTH
    // view: Y-UP
    sx_vec2 screen_size;
    the_app->window_size(&screen_size);
    const float view_width = GAME_BOARD_WIDTH * 0.5f;
    const float view_height = screen_size.y * view_width / screen_size.x;
    the_camera->init(&the_game.cam, 50.0f,
                     sx_rectf(-view_width, -view_height, view_width, view_height), -5.0f, 5.0f);
    the_camera->lookat(&the_game.cam, sx_vec3f(0, 0.0f, 1.0), SX_VEC3_ZERO, SX_VEC3_UNITY);

    //
    the_game.keyboard = the_input->create_device(RIZZ_INPUT_DEVICETYPE_KEYBOARD);
    the_input->map_bool(the_game.keyboard, RIZZ_INPUT_KBKEY_LEFT, KEY_LEFT);
    the_input->map_bool(the_game.keyboard, RIZZ_INPUT_KBKEY_RIGHT, KEY_RIGHT);
    the_input->map_bool(the_game.keyboard, RIZZ_INPUT_KBKEY_SPACE, KEY_SHOOT);

    the_game.gamepad = the_input->create_device(RIZZ_INPUT_DEVICETYPE_PAD);
    if (the_game.gamepad.id) {
        the_input->map_float(the_game.gamepad, RIZZ_INPUT_PADBUTTON_LEFTSTICKX, KEY_MOVEX_ANALOG, 0,
                             1.0f,
                             NULL, NULL);
        the_input->map_bool(the_game.gamepad, RIZZ_INPUT_PADBUTTON_A, KEY_SHOOT);
    }

    // graphics and fonts
    the_game.game_atlas =
        the_asset->load("atlas", "/assets/sprites/game-sprites",
                        &(rizz_atlas_load_params){ .min_filter = SG_FILTER_NEAREST,
                                                   .mag_filter = SG_FILTER_NEAREST },
                        0, the_game.alloc, 0);
    the_game.font = the_asset->load("font", "/assets/fonts/5x5_pixel.ttf",
                                    &(rizz_font_load_params){ 0 }, 0, the_game.alloc, 0);

    // TODO: creating sprites should be easier (from data)
    //
    the_game.tile_size = GAME_BOARD_WIDTH / 15.0f;
    the_game.enemy_shoot_interval = ENEMY_SHOOT_INTERVAL;
    the_game.player_lives = NUM_LIVES;

    create_enemies();
    create_player();
    create_bullet_sprites();
    create_explosion_sprites();
    create_covers();
    create_sounds();
    create_saucer();

    // first bus (#0) is used for sfx
    // second bus (#1) is used for heart-beat music effect
    the_sound->bus_set_max_lanes(0, 4);
    the_sound->bus_set_max_lanes(1, 1);

    // the_sound->set_master_volume(0.0f);

    load_high_score();

    return true;
}

static void shutdown() 
{
    the_asset->unload(the_game.game_atlas);
    the_asset->unload(the_game.font);
    for (int i = 0; i < SOUND_COUNT; i++) {
        the_asset->unload(the_game.sounds[i]);
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        the_2d->sprite.animclip_destroy(the_game.enemy_clips[i]);
        the_2d->sprite.destroy(the_game.enemy_sprites[i]);
    }

    for (int i = 0; i < BULLET_TYPE_COUNT; i++) {
        the_2d->sprite.destroy(the_game.bullet_sprites[i]);
    }

    the_2d->sprite.destroy(the_game.enemy_explosion_sprite);
    the_2d->sprite.destroy(the_game.bounds_explosion_sprite);
    the_2d->sprite.destroy(the_game.cover_sprite);
    the_2d->sprite.destroy(the_game.player.sprite);
    the_2d->sprite.destroy(the_game.saucer.sprite);
    the_core->trace_alloc_destroy(the_game.alloc);
}

static void update_enemy(enemy_t* e, float dt)
{
    if (!e->move && e->allow_next_move) {
        e->wait_tm += dt;
        if (e->wait_tm >= e->wait_duration) {
            e->wait_tm = 0;
            switch (e->dir) {
            case ENEMY_MOVEMENT_DOWN:
                e->target_pos = sx_vec2f(e->pos.x, e->pos.y - the_game.tile_size);
                break;
            case ENEMY_MOVEMENT_LEFT:
                e->target_pos = sx_vec2f(e->pos.x - the_game.tile_size, e->pos.y);
                break;
            case ENEMY_MOVEMENT_RIGHT:
                e->target_pos = sx_vec2f(e->pos.x + the_game.tile_size, e->pos.y);
                break;
            }
            e->move = true;
        }
    } else if (e->allow_next_move) {
        // move to target
        float t = e->move_tm / ENEMY_TILE_MOVE_DURATION;
        t = sx_min(e->move_tm, 1.0f);
        e->pos = sx_vec2_lerp(e->start_pos, e->target_pos, t);
        e->move_tm += dt;

        if (t >= 1.0f) {
            if (e->dir == ENEMY_MOVEMENT_LEFT) {
                --e->xstep;
                if (e->xstep <= -2) {
                    e->dir = ENEMY_MOVEMENT_DOWN;
                }
            } else if (e->dir == ENEMY_MOVEMENT_RIGHT) {
                ++e->xstep;
                if (e->xstep >= 2) {
                    e->dir = ENEMY_MOVEMENT_DOWN;
                }
            } else if (e->dir == ENEMY_MOVEMENT_DOWN) {
                e->dir = e->xstep < 0 ? ENEMY_MOVEMENT_RIGHT : ENEMY_MOVEMENT_LEFT;
            }

            e->move_tm = 0;
            e->start_pos = e->target_pos;
            e->move = false;
            e->allow_next_move = false;
        }
    }

    if (!e->dead && e->pos.y <= (the_game.player.pos.y + the_game.tile_size*0.5f)) {
        the_game.state = GAME_STATE_GAMEOVER;
        rizz_coro_invoke(state_control, NULL);
    }
}

static void spawn_saucer(void)
{
    float side = sx_sign(sx_rng_genf(&the_game.rng) * 2.0f - 1.0f);
    if (side == 0) {
        side = 1.0f;
    }

    saucer_t* saucer = &the_game.saucer;
    saucer->pos.x = side * (GAME_BOARD_WIDTH * 0.5f + the_game.tile_size);
    saucer->pos.y = GAME_BOARD_HEIGHT * 0.5f - the_game.tile_size;
    saucer->speed = -side * 0.3f;
    saucer->wait_duration = 30.0f + (sx_rng_genf(&the_game.rng) * 20.0f - 10.0f);
    saucer->dead = false;

    the_sound->play(the_sound->source_get(the_game.sounds[SOUND_SAUCER]), 0, 1.0f, 0, false);
}

static void update_player(float dt)
{
    if (the_game.player_died) {
        return;
    }

    player_t* player = &the_game.player;
    float speed = 0.4f;
    if (the_input->get_bool(KEY_LEFT)) {
        float left_limit = -GAME_BOARD_WIDTH * 0.5f +
                           sx_rect_width(the_2d->sprite.draw_bounds(the_game.player.sprite)) * 0.5f +
                           the_game.tile_size * 0.1f;
        player->pos.x -= dt * speed;
        player->pos.x = sx_max(left_limit, player->pos.x);
    }

    if (the_input->get_bool(KEY_RIGHT)) {
        float right_limit = GAME_BOARD_WIDTH * 0.5f -
                            sx_rect_width(the_2d->sprite.draw_bounds(the_game.player.sprite)) * 0.5f -
                            the_game.tile_size * 0.1f;
        player->pos.x += dt * speed;
        player->pos.x = sx_min(right_limit, player->pos.x);
    }

    { // analog stick
        float right_limit = GAME_BOARD_WIDTH * 0.5f -
                            sx_rect_width(the_2d->sprite.draw_bounds(the_game.player.sprite)) * 0.5f -
                            the_game.tile_size * 0.1f;
        float left_limit = -GAME_BOARD_WIDTH * 0.5f +
                           sx_rect_width(the_2d->sprite.draw_bounds(the_game.player.sprite)) * 0.5f +
                           the_game.tile_size * 0.1f;
        float movex_analog = the_input->get_float(KEY_MOVEX_ANALOG);
        movex_analog = sx_sign(movex_analog) * sx_easeout_quad(sx_abs(movex_analog));
        speed = movex_analog * speed;
        player->pos.x += dt * speed;
        player->pos.x = sx_clamp(player->pos.x, left_limit, right_limit);
    }
    

    player->bullet_tm += dt;
    if (the_input->get_bool(KEY_SHOOT) && !the_game.enemy_explosion) {
        if (player->bullet_tm > PLAYER_BULLET_INTERVAL) {
            create_bullet(
                sx_vec2f(player->pos.x,
                         player->pos.y +
                             sx_rect_height(the_2d->sprite.draw_bounds(player->sprite)) * 0.5f),
                BULLET_TYPE_PLAYER);
            player->bullet_tm = 0;

            the_sound->play(the_sound->source_get(the_game.sounds[SOUND_SHOOT]), 0, 1.0f, 0, false);
        }
    }
}

static void check_bullet_collision_cb(int start, int end, int thrd_index, void* user)
{
    collision_data_t* cdata = user;

    if (cdata->hit_index != -1) {
        return;
    }

    bullet_t* bullet = &the_game.bullets[cdata->bullet_index];
    sx_rect bullet_rc = sx_rect_move(the_2d->sprite.draw_bounds(bullet->sprite), bullet->pos);
    c2AABB bullet_aabb = { { bullet_rc.xmin, bullet_rc.ymax }, { bullet_rc.xmax, bullet_rc.ymin } };

    for (int i = start; i < end; i++) {
        if (the_game.enemies[i].dead) {
            continue;
        }

        sx_rect enemy_rc = sx_rect_move(the_2d->sprite.draw_bounds(the_game.enemy_sprites[i]),
                                        the_game.enemies[i].pos);
        c2AABB enemy_aabb = { { enemy_rc.xmin, enemy_rc.ymax }, { enemy_rc.xmax, enemy_rc.ymin } };
        if (c2AABBtoAABB(bullet_aabb, enemy_aabb)) {
            cdata->hit_index = i;
            break;
        }
    }
}

static void kill_player(void)
{
    sx_assert(!the_game.player_died);

    the_game.player_died = true;
    the_game.player_explosion.pos = the_game.player.pos;
    --the_game.player_lives;

    the_sound->play(the_sound->source_get(the_game.sounds[SOUND_EXPLODE4]), 0, 1.0f, 0, false);
}

static void kill_saucer(void)
{
    saucer_t* saucer = &the_game.saucer;

    saucer->dead = true;

    the_game.enemy_explosion = true;
    the_game.enemy_explosion_tm = 0;
    the_game.enemy_explosion_pos = saucer->pos;

    the_game.player_score += saucer->hit_score;

    the_sound->play(the_sound->source_get(the_game.sounds[SOUND_BONUS]), 1, 1.0f, 0, false);
}

static void update_bullets(float dt)
{
    // TODO: remove c2AABB
    for (int i = 0; i < the_game.num_bullets; i++) {
        bullet_t* bullet = &the_game.bullets[i];

        bullet->pos.y += bullet->speed * dt;

        if (bullet->pos.y >= GAME_BOARD_HEIGHT * 0.5f ||
            bullet->pos.y <= -GAME_BOARD_HEIGHT * 0.5f) {

            if (bullet->pos.y <= -GAME_BOARD_HEIGHT * 0.5f) {
                create_explosion(sx_vec2f(bullet->pos.x, -GAME_BOARD_HEIGHT * 0.5f),
                                 the_game.bounds_explosion_sprite);
            }

            remove_bullet(i);
            i--;
            continue;
        }

        sx_rect bullet_rc = sx_rect_move(the_2d->sprite.draw_bounds(bullet->sprite), bullet->pos);
        c2AABB bullet_aabb = { { bullet_rc.xmin, bullet_rc.ymax },
                               { bullet_rc.xmax, bullet_rc.ymin } };

        // check collision with the world based on bullet type
        if (bullet->type == BULLET_TYPE_PLAYER) {
            collision_data_t cdata = { .bullet_index = i, .hit_index = -1 };
            sx_job_t job = the_core->job_dispatch(MAX_ENEMIES, check_bullet_collision_cb, &cdata,
                                                  SX_JOB_PRIORITY_NORMAL, 0);
            the_core->job_wait_and_del(job);
            if (cdata.hit_index != -1) {
                the_game.enemies[cdata.hit_index].dead = true;

                // enter explosion state
                the_game.enemy_explosion = true;
                the_game.enemy_explosion_tm = 0;
                the_game.enemy_explosion_pos = the_game.enemies[cdata.hit_index].pos;

                the_game.player_score += the_game.enemies[cdata.hit_index].hit_score;

                the_sound->play(
                    the_sound->source_get(
                        the_game.sounds[the_game.enemies[cdata.hit_index].explode_sound]),
                    0, 1.0f, 0, false);

                remove_bullet(i);
                i--;
                continue;
            }

            // collision of the player bullet with saucer
            saucer_t* saucer = &the_game.saucer;
            if (!saucer->dead) {
                sx_rect saucer_rc =
                    sx_rect_move(the_2d->sprite.draw_bounds(saucer->sprite), saucer->pos);
                c2AABB saucer_aabb = { { saucer_rc.xmin, saucer_rc.ymax },
                                       { saucer_rc.xmax, saucer_rc.ymin } };
                if (c2AABBtoAABB(saucer_aabb, bullet_aabb)) {
                    kill_saucer();
                    remove_bullet(i);
                    i--;
                    continue;
                }
            }
        } else if (bullet->type == BULLET_TYPE_ALIEN1) {
            // check collision with player
            sx_rect player_rc =
                sx_rect_move(the_2d->sprite.draw_bounds(the_game.player.sprite), the_game.player.pos);
            c2AABB player_aabb = { { player_rc.xmin, player_rc.ymax },
                                   { player_rc.xmax, player_rc.ymin } };
            if (c2AABBtoAABB(bullet_aabb, player_aabb) && !the_game.player_died) {
                kill_player();

                remove_bullet(i);
                i--;
                continue;
            }
        }

        // collision with covers
        {
            bool bullet_hit = false;
            for (int ic = 0; ic < NUM_COVERS; ic++) {
                cover_t* cover = &the_game.covers[ic];
                if (!cover->dead) {
                    sx_rect cover_rc =
                        sx_rect_move(the_2d->sprite.draw_bounds(the_game.cover_sprite), cover->pos);
                    c2AABB cover_aabb = { { cover_rc.xmin, cover_rc.ymax },
                                          { cover_rc.xmax, cover_rc.ymin } };
                    if (c2AABBtoAABB(bullet_aabb, cover_aabb)) {
                        bullet_hit = true;
                        cover->health -= bullet->damage;
                        cover->health = sx_max(cover->health, 0);
                        create_explosion(bullet->pos, the_game.enemy_explosion_sprite);
                        the_sound->play(the_sound->source_get(the_game.sounds[SOUND_HIT]), 0, 1.0f,
                                        0, false);
                        if (cover->health <= 0) {
                            cover->dead = true;
                        }
                        break;
                    }
                }
            }

            if (bullet_hit) {
                remove_bullet(i);
                i--;
                continue;
            }
        }

        // collision with other bullets
        {
            for (int ib = 0; ib < the_game.num_bullets; ib++) {
                if (i != ib) {
                    bullet_t* bullet2 = &the_game.bullets[ib];
                    sx_rect bullet2_rc =
                        sx_rect_move(the_2d->sprite.draw_bounds(bullet2->sprite), bullet2->pos);
                    c2AABB bullet2_aabb = { { bullet2_rc.xmin, bullet2_rc.ymax },
                                            { bullet2_rc.xmax, bullet2_rc.ymin } };
                    if (c2AABBtoAABB(bullet_aabb, bullet2_aabb)) {
                        create_explosion(sx_vec2_mulf(sx_vec2_add(bullet->pos, bullet2->pos), 0.5f),
                                         the_game.enemy_explosion_sprite);
                        remove_bullet(ib);
                        remove_bullet(i);
                        the_sound->play(the_sound->source_get(the_game.sounds[SOUND_HIT]), 0, 1.0f,
                                        0, false);
                        i--;
                    }
                }
            }
        }
    }
}

static void update_explosions(float dt)
{
    for (int i = 0; i < the_game.num_explosions; i++) {
        explosion_t* explosion = &the_game.explosions[i];
        if (explosion->wait_tm >= ENEMY_EXPLODE_TIME) {
            if (i < the_game.num_explosions - 1) {
                the_game.explosions[i] = the_game.explosions[the_game.num_explosions - 1];
            }

            --the_game.num_explosions;
            explosion->wait_tm = 0;
        }

        explosion->wait_tm += dt;
    }
}

static int check_collision_with_covers(enemy_t* e) 
{
    if (e->pos.y > 0) {
        return -1;
    }

    sx_rect enemy_rc = sx_rect_move(the_2d->sprite.draw_bounds(the_game.cover_sprite), e->pos);
    c2AABB enemy_aabb = { { enemy_rc.xmin, enemy_rc.ymax }, { enemy_rc.xmax, enemy_rc.ymin } };

    for (int i = 0; i < NUM_COVERS; i++) {
        cover_t* cover = &the_game.covers[i];
        if (cover->dead) {
            continue;
        }

        sx_rect cover_rc = sx_rect_move(the_2d->sprite.draw_bounds(the_game.cover_sprite), cover->pos);
        c2AABB cover_aabb = { { cover_rc.xmin, cover_rc.ymax }, { cover_rc.xmax, cover_rc.ymin } };
        
        if (c2AABBtoAABB(enemy_aabb, cover_aabb)) {
            return i;
        }
    }

    return -1;
}

static void update(float dt)
{
    if (the_game.state != GAME_STATE_INGAME) {
        return;
    }

    rizz_profile_begin(UPDATE, 0);

    float dtp = dt;
    float dte;

    if (the_game.player_died) {
        dt = 0;
    }

    update_player(dt);

    // update enemies
    {
        int alive_enemies[MAX_ENEMIES];
        int num_alive = 0;
        for (int i = 0; i < MAX_ENEMIES; i++) {
            enemy_t* e = &the_game.enemies[i];
            if (!e->dead) {
                alive_enemies[num_alive++] = i;
            }
        }

        if (num_alive == 0) {
            // player won the game
            the_game.state = GAME_STATE_WIN;
            ++the_game.stage;
            rizz_coro_invoke(state_control, NULL);
        }

        float speed = sx_lerp(1.0f, 4.0f, 1.0f - ((float)num_alive / (float)MAX_ENEMIES));
        dte = the_game.enemy_explosion ? 0.0f : (dt * speed);
        the_2d->sprite.animclip_update_batch(the_game.enemy_clips, MAX_ENEMIES, dte);

        for (int i = 0; i < num_alive; i++) {
            enemy_t* e = &the_game.enemies[alive_enemies[i]];
            update_enemy(e, dte);
        }
        update_enemy(&the_game.dummy_enemy, dte);

        if (!the_game.dummy_enemy.allow_next_move) {
            for (int i = 0; i < num_alive; i++) {
                the_game.enemies[alive_enemies[i]].allow_next_move = true;
            }
            the_game.dummy_enemy.allow_next_move = true;
        }

        // enemy shoot
        if (the_game.enemy_shoot_tm >= the_game.enemy_shoot_interval && num_alive > 0) {
            int alive_index = sx_rng_gen_rangei(&the_game.rng, 0, num_alive - 1);
            create_bullet(the_game.enemies[alive_enemies[alive_index]].pos, BULLET_TYPE_ALIEN1);
            the_game.enemy_shoot_tm = 0;
            the_game.enemy_shoot_interval =
                ENEMY_SHOOT_INTERVAL + (sx_rng_genf(&the_game.rng) * 2.0f - 1.0f) * 0.2f;
        }
        the_game.enemy_shoot_tm += dte / speed;

        for (int i = 0; i < num_alive; i++) {
            enemy_t* e = &the_game.enemies[alive_enemies[i]];
            int cover_index = check_collision_with_covers(e);
            if (cover_index != -1) {
                cover_t* cover = &the_game.covers[cover_index];
                cover->health -= 30;
                cover->health = sx_max(cover->health, 0);
                if (cover->health <= 0) {
                    cover->dead = true;
                }

                the_game.enemy_explosion = true;
                the_game.enemy_explosion_tm = 0;
                the_game.enemy_explosion_pos = e->pos;

                the_sound->play(the_sound->source_get(the_game.sounds[e->explode_sound]), 0, 1.0f,
                                0, false);

                e->dead = true;
                break;
            }
        }
    }

    update_bullets(dt);

    if (the_game.enemy_explosion) {
        if (the_game.enemy_explosion_tm >= ENEMY_EXPLODE_TIME) {
            the_game.enemy_explosion = false;
        }
        the_game.enemy_explosion_tm += dt;
    }
    update_explosions(dt);

    if (the_game.player_died) {
        if (the_game.player_explosion.wait_tm >= PLAYER_EXPLOSION_DURATION) {
            the_game.player_died = false;
            the_game.player_explosion.wait_tm = 0;
            if (the_game.player_lives == 0) {
                the_game.state = GAME_STATE_GAMEOVER;
                rizz_coro_invoke(state_control, NULL);
            }
        }
        the_game.player_explosion.wait_tm += dtp;
    }

    // saucer
    saucer_t* saucer = &the_game.saucer;
    if (saucer->dead) {
        if (saucer->wait_tm >= saucer->wait_duration) {
            spawn_saucer();
            saucer->wait_tm = 0;
        }
        saucer->wait_tm += dt;
    } else {
        saucer->pos.x += saucer->speed * dt;
        if (saucer->pos.x < (-GAME_BOARD_WIDTH*0.5f - the_game.tile_size) ||
            saucer->pos.x > (GAME_BOARD_WIDTH*0.5f + the_game.tile_size))
        {
            saucer->dead = true;
        }
    }

     // heartbeat sound
    the_game.heartbeat_tm += dte;
    if (the_game.heartbeat_tm >= HEARTBEAT_INTERVAL) {
        the_sound->play(the_sound->source_get(the_game.sounds[SOUND_HEARTBEAT]), 1, 1.0f, 0, false);
        the_game.heartbeat_tm = 0;
    }

    rizz_profile_end(UPDATE);
}

static void render_info_screen(game_state_t state) 
{
    rizz_api_gfx_draw* api = &the_gfx->staged;
    sg_pass_action pass_action = { .colors[0] = { SG_ACTION_CLEAR, { 0.0f, 0.0f, 0.0f, 1.0f } },
                                   .depth = { SG_ACTION_CLEAR, 1.0f } };

    api->begin(the_game.render_stages[RENDER_STAGE_GAME]);
    api->begin_default_pass(&pass_action, the_app->width(), the_app->height());

    float w = (float)the_app->width();
    float h = (float)the_app->height();

    const rizz_font* font = the_2d->font.get(the_game.font);
    sx_mat4 vp =
        sx_mat4_ortho_offcenter(0, h, w, 0, -5.0f, 5.0f, 0, the_gfx->GL_family());
    the_2d->font.set_viewproj_mat(font, &vp);
    char text[32];
    if (state == GAME_STATE_GAMEOVER) {
        sx_strcpy(text, sizeof(text), "GAME OVER");
    } else {
        sx_snprintf(text, sizeof(text), "STAGE  %d", the_game.stage + 1);
    }

    rizz_font_bounds bounds = the_2d->font.bounds(font, SX_VEC2_ZERO, text);
    sx_vec2 text_pos = sx_vec2f(w * 0.5f - sx_rect_width(bounds.rect) * 0.5f,
                                h * 0.5f + sx_rect_height(bounds.rect));
    the_2d->font.draw(font, text_pos, text);

    char highscore_text[32];
    sx_snprintf(highscore_text, sizeof(highscore_text), "HIGH SCORE  %d", the_game.high_score);
    bounds = the_2d->font.bounds(font, SX_VEC2_ZERO, highscore_text);
    the_2d->font.draw(font,
                   sx_vec2f(w*0.5f - sx_rect_width(bounds.rect) * 0.5f, text_pos.y - 30.0f),
                   highscore_text);

    api->end_pass();
    api->end();    // RENDER_STAGE_GAME
}

static void show_devmenu(void)
{
    bool* debug_mem = &the_game.show_debuggers[DEBUGGER_MEMORY];
    bool* show_log = &the_game.show_debuggers[DEBUGGER_LOG];
    bool* debug_gfx = &the_game.show_debuggers[DEBUGGER_GRAPHICS];
    bool* debug_sprites = &the_game.show_debuggers[DEBUGGER_SPRITES];
    bool* debug_sounds = &the_game.show_debuggers[DEBUGGER_SOUND];
    bool* debug_input = &the_game.show_debuggers[DEBUGGER_INPUT];
    if (the_imgui->BeginMainMenuBar())
    {
        if (the_imgui->BeginMenu("Debug", true)) {
            if (the_imgui->MenuItem_Bool("Memory", NULL, *debug_mem, true)) {
                *debug_mem = !(*debug_mem);
            }

            if (the_imgui->MenuItem_Bool("Log", NULL, *show_log, true)) {
                *show_log = !(*show_log);
            }

            if (the_imgui->MenuItem_Bool("Graphics", NULL, *debug_gfx, true)) {
                *debug_gfx = !(*debug_gfx);
            }

            if (the_imgui->MenuItem_Bool("Sprites", NULL, *debug_sprites, true)) {
                *debug_sprites = !(*debug_sprites);
            }

            if (the_imgui->MenuItem_Bool("Sounds", NULL, *debug_sounds, true)) {
                *debug_sounds = !(*debug_sounds);
            }

            if (the_imgui->MenuItem_Bool("Input", NULL, *debug_input, true)) {
                *debug_input = !(*debug_input);
            }
            the_imgui->EndMenu();

        }
     }
    the_imgui->EndMainMenuBar();

    if (*debug_mem) {
        the_core->show_memory_debugger(debug_mem);
    }
    if (*show_log) {
        the_core->show_log(show_log);
    }
    if (*debug_gfx) {
        the_core->show_graphics_debugger(debug_gfx);
    }
    if (*debug_sprites) {
        the_2d->sprite.show_debugger(debug_sprites);
    }
    if (*debug_sounds) {
        the_sound->show_debugger(debug_sounds);
    }
    if (*debug_input) {
        the_input->show_debugger(debug_input);
    }
}

static void render(void)
{
    if (the_game.show_dev_menu) {
        show_devmenu();
    }    

    if (the_game.state != GAME_STATE_INGAME) {
        render_info_screen(the_game.state);
        return;
    }

    rizz_profile_begin(RENDER, 0);

    rizz_api_gfx_draw* api = &the_gfx->staged;
    sg_pass_action pass_action = { .colors[0] = { SG_ACTION_CLEAR, { 0.0f, 0.0f, 0.0f, 1.0f } },
                                   .depth = { SG_ACTION_CLEAR, 1.0f } };

    api->begin(the_game.render_stages[RENDER_STAGE_GAME]);
    api->begin_default_pass(&pass_action, the_app->width(), the_app->height());

    sx_mat4 proj, view;
    the_camera->ortho_mat(&the_game.cam, &proj);
    the_camera->view_mat(&the_game.cam, &view);
    sx_mat4 vp = sx_mat4_mul(&proj, &view);

    sx_mat3 enemy_mats[MAX_ENEMIES];
    rizz_sprite enemy_sprites[MAX_ENEMIES];
    int num_enemies = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!the_game.enemies[i].dead) {
            enemy_mats[num_enemies] = sx_mat3_translatev(the_game.enemies[i].pos);
            enemy_sprites[num_enemies] = the_game.enemy_sprites[i];
            num_enemies++;
        }
    }
    if (num_enemies > 0) {
        the_2d->sprite.draw_batch(enemy_sprites, num_enemies, &vp, enemy_mats, NULL);
    }

    // TODO: add another function for drawing by position
    if (!the_game.player_died) {
        sx_mat3 player_mat = sx_mat3_translatev(the_game.player.pos);
        the_2d->sprite.draw(the_game.player.sprite, &vp, &player_mat, SX_COLOR_WHITE);
    }

    if (the_game.num_bullets > 0) {
        sx_mat3 bullet_mats[MAX_BULLETS];
        rizz_sprite bullet_sprites[MAX_BULLETS];
        for (int i = 0; i < the_game.num_bullets; i++) {
            bullet_mats[i] = sx_mat3_translatev(the_game.bullets[i].pos);
            bullet_sprites[i] = the_game.bullets[i].sprite;
        }

        the_2d->sprite.draw_batch(bullet_sprites, the_game.num_bullets, &vp, bullet_mats, NULL);
    }

    // explosions
    {
        sx_mat3 explosion_mats[MAX_BULLETS + 2];
        rizz_sprite explosion_sprites[MAX_BULLETS + 2];
        for (int i = 0; i < the_game.num_explosions; i++) {
            explosion_mats[i] = sx_mat3_translatev(the_game.explosions[i].pos);
            explosion_sprites[i] = the_game.explosions[i].sprite;
        }
        int num_explosions = the_game.num_explosions;

        if (the_game.enemy_explosion) {
            explosion_mats[num_explosions] = sx_mat3_translatev(the_game.enemy_explosion_pos);
            explosion_sprites[num_explosions] = the_game.enemy_explosion_sprite;
            num_explosions++;
        }

        if (the_game.player_died) {
            explosion_mats[num_explosions] = sx_mat3_translatev(the_game.player_explosion.pos);
            explosion_sprites[num_explosions] = the_game.enemy_explosion_sprite;
            num_explosions++;
        }

        if (num_explosions > 0) {
            the_2d->sprite.draw_batch(explosion_sprites, num_explosions, &vp, explosion_mats, NULL);
        }
    }

    // covers
    {
        sx_mat3 cover_mats[NUM_COVERS];
        rizz_sprite cover_sprites[NUM_COVERS];
        sx_color cover_colors[NUM_COVERS];
        int count = 0;
        for (int i = 0; i < NUM_COVERS; i++) {
            if (!the_game.covers[i].dead) {
                cover_mats[count] = sx_mat3_translatev(the_game.covers[i].pos);
                cover_sprites[count] = the_game.cover_sprite;
                float color_val = (float)the_game.covers[i].health / 100.0f;
                cover_colors[count] = sx_color4f(1.0f - color_val, color_val, 0, 1.0f);
                count++;
            }
        }
        if (count > 0) {
            the_2d->sprite.draw_batch(cover_sprites, count, &vp, cover_mats, cover_colors);
        }
    }

    // saucer
    if (!the_game.saucer.dead) {
        sx_mat3 mat = sx_mat3_translatev(the_game.saucer.pos);
        the_2d->sprite.draw(the_game.saucer.sprite, &vp, &mat, SX_COLOR_WHITE);
    }

    api->end_pass();
    api->end(); // RENDER_STAGE_GAME
    
    api->begin(the_game.render_stages[RENDER_STAGE_UI]);
    {
        int w = the_app->width();
        int h = the_app->height();
        api->begin_default_pass(&(sg_pass_action) {
            .colors[0] = { SG_ACTION_DONTCARE }, .depth = {SG_ACTION_DONTCARE}}, w, h);

        const rizz_font* font = the_2d->font.get(the_game.font);
        sx_mat4 vp = sx_mat4_ortho_offcenter(0, (float)h, (float)w, 0, -5.0f, 5.0f, 0, the_gfx->GL_family());
        the_2d->font.set_viewproj_mat(font, &vp);
        the_2d->font.drawf(font, sx_vec2f(10.0f, 30.0f), "SCORE  %d", the_game.player_score);

        char lives[32];
        sx_snprintf(lives, sizeof(lives), "LIVES  %d", the_game.player_lives);
        rizz_font_bounds bounds = the_2d->font.bounds(font, SX_VEC2_ZERO, lives);
        the_2d->font.drawf(font, sx_vec2f((float)(w - sx_rect_width(bounds.rect)*1.1f), 30.0f), lives);
        api->end_pass();
    }
    api->end(); // RENDER_STAGE_UI

    rizz_profile_end(RENDER);
}

rizz_plugin_decl_main(space_invaders, plugin, e)
{
    switch (e) {
    case RIZZ_PLUGIN_EVENT_STEP: {
        update((float)sx_tm_sec(the_core->delta_tick()));
        render();
        break;
    }
    case RIZZ_PLUGIN_EVENT_INIT:
        // runs only once for application. Retreive needed APIs
        the_core = plugin->api->get_api(RIZZ_API_CORE, 0);
        the_gfx = plugin->api->get_api(RIZZ_API_GFX, 0);
        the_app = plugin->api->get_api(RIZZ_API_APP, 0);
        the_camera = plugin->api->get_api(RIZZ_API_CAMERA, 0);
        the_vfs = plugin->api->get_api(RIZZ_API_VFS, 0);
        the_asset = plugin->api->get_api(RIZZ_API_ASSET, 0);
        the_imgui = plugin->api->get_api_byname("imgui", 0);
        the_2d = plugin->api->get_api_byname("2dtools", 0);
        the_input = plugin->api->get_api_byname("input", 0);
        the_sound = plugin->api->get_api_byname("sound", 0);
        the_plugin = plugin->api;
        init();
        break;

    case RIZZ_PLUGIN_EVENT_LOAD:
        break;

    case RIZZ_PLUGIN_EVENT_UNLOAD:
        break;

    case RIZZ_PLUGIN_EVENT_SHUTDOWN:
        shutdown();
        break;
    }

    return 0;
}

rizz_plugin_decl_event_handler(space_invaders, e)
{
    switch (e->type) {
    case RIZZ_APP_EVENTTYPE_UPDATE_APIS:
        // reload APIs. This event happens when one of the plugins are reloaded
        the_imgui = the_plugin->get_api_byname("imgui", 0);
        the_2d = the_plugin->get_api_byname("2dtools", 0);
        the_input = the_plugin->get_api_byname("input", 0);
        the_sound = the_plugin->get_api_byname("sound", 0);
        break;

    case RIZZ_APP_EVENTTYPE_KEY_DOWN:
        if (e->key_code == RIZZ_APP_KEYCODE_F2) {
            the_game.show_dev_menu = !the_game.show_dev_menu;
        }

    default:
        break;
    }
}

rizz_game_decl_config(conf)
{
    // set plugin directory to the exe folder
    static char exe_path[RIZZ_MAX_PATH];
    sx_os_path_exepath(exe_path, sizeof(exe_path));
    sx_os_path_dirname(exe_path, sizeof(exe_path), exe_path);

    conf->app_name = "space-invaders";
    conf->app_version = 1000;
    conf->app_title = "space-invaders";
    conf->window_width = 600;
    conf->window_height = 800;
    conf->app_flags |= RIZZ_APP_FLAG_HIGHDPI;
    conf->core_flags |= RIZZ_CORE_FLAG_LOG_TO_FILE;
    conf->log_level = RIZZ_LOG_LEVEL_DEBUG;
    conf->swap_interval = 1;
    conf->plugin_path = exe_path;
    conf->plugins[0] = "imgui";
    conf->plugins[1] = "2dtools";
    conf->plugins[2] = "input";
    conf->plugins[3] = "sound";
}
