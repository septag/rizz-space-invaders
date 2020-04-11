#include "sx/allocator.h"
#include "sx/math.h"
#include "sx/string.h"
#include "sx/timer.h"

#include "rizz/2dtools.h"
#include "rizz/imgui-extra.h"
#include "rizz/imgui.h"
#include "rizz/rizz.h"

RIZZ_STATE static rizz_api_core* the_core;
RIZZ_STATE static rizz_api_gfx* the_gfx;
RIZZ_STATE static rizz_api_app* the_app;
RIZZ_STATE static rizz_api_imgui* the_imgui;
RIZZ_STATE static rizz_api_asset* the_asset;
RIZZ_STATE static rizz_api_imgui_extra* the_imguix;
RIZZ_STATE static rizz_api_camera* the_camera;
RIZZ_STATE static rizz_api_vfs* the_vfs;
RIZZ_STATE static rizz_api_sprite* the_sprite;
RIZZ_STATE static rizz_api_plugin* the_plugin;
RIZZ_STATE static rizz_api_camera* the_camera;

RIZZ_STATE rizz_gfx_stage g_stage;

#define ENEMIES_PER_ROW 11
#define NUM_ROWS 5
#define MAX_ENEMIES (ENEMIES_PER_ROW * NUM_ROWS)
#define MAX_BULLETS 6
#define GAME_BOARD_WIDTH 1.0f
#define ENEMY_WAIT_DURATION 0.7f

typedef enum enemy_direction_t {
    ENEMY_MOVEMENT_RIGHT = 0,
    ENEMY_MOVEMENT_DOWN,
    ENEMY_MOVEMENT_LEFT
} enemy_direction_t;

typedef struct enemy_t {
    sx_vec2 start_pos;
    sx_vec2 target_pos;
    sx_vec2 pos;
    float wait_tm;
    float move_tm;
    float wait_duration;
    float xoffset;
    enemy_direction_t move_dir;
    bool move;
} enemy_t;

typedef struct bullet_t {
    sx_vec2 pos;
    float t;
} bullet_t;

typedef struct player_t {
    sx_vec2 pos;
    float t;
} player_t;

typedef struct cover_t {
    sx_vec2 pos;
} cover_t;

typedef struct saucer_t {
    sx_vec2 pos;
    float t;
} saucer_t;

typedef struct game_t {
    enemy_t enemies[MAX_ENEMIES];
    bullet_t bullets[MAX_BULLETS];
    rizz_sprite_animclip enemy_idle_clips[MAX_ENEMIES];
    rizz_sprite enemy_sprites[MAX_ENEMIES];
    rizz_sprite_animclip enemy_clips[MAX_ENEMIES];
    int num_bullets;
    player_t player;
    rizz_asset game_atlas;
    rizz_camera cam;
    float enemy_move_dist;
    float enemy_move_duration;
    float tile_size;
    enemy_direction_t enemy_move_dir;
    enemy_direction_t enemy_next_move_dir;
    enemy_t enemy_group_controller;
} game_t;

RIZZ_STATE static game_t the_game;

static void create_enemies()
{
    float half_width = GAME_BOARD_WIDTH * 0.5f;
    float tile_size = GAME_BOARD_WIDTH / 15.0f;

    the_game.game_atlas =
        the_asset->load("atlas", "/assets/sprites/game-sprites",
                        &(rizz_atlas_load_params){ .min_filter = SG_FILTER_NEAREST,
                                                   .mag_filter = SG_FILTER_NEAREST },
                        0, NULL, 0);

    static const rizz_sprite_animclip_frame_desc enemy1_frames[] = { { .name = "enemy1-a.png" },
                                                                     { .name = "enemy1-b.png" } };
    static const rizz_sprite_animclip_frame_desc enemy2_frames[] = { { .name = "enemy2-a.png" },
                                                                     { .name = "enemy2-b.png" } };
    static const rizz_sprite_animclip_frame_desc enemy3_frames[] = { { .name = "enemy3-a.png" },
                                                                     { .name = "enemy3-b.png" } };

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

    static const rizz_sprite_animclip_frame_desc* frame_descs[3] = {
        enemy3_frames, 
        enemy1_frames,
        enemy2_frames
    };
    // clang-format on

    float start_x = -half_width + 2.5f * tile_size;
    float x = start_x;
    float y = tile_size * NUM_ROWS;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (i % 11 == 0) {
            x = start_x;
            y -= tile_size;
        }

        int enemy = layout[i];

        the_game.enemy_clips[i] = the_sprite->animclip_create(
            &(rizz_sprite_animclip_desc){ .atlas = the_game.game_atlas,
                                          .frames = frame_descs[enemy],
                                          .num_frames = 2,
                                          .fps = /*the_core->randf()*0.2f + 0.7f*/ 0.8f });

        the_game.enemy_sprites[i] =
            the_sprite->create(&(rizz_sprite_desc){ .name = enemy_names[enemy],
                                                    .atlas = the_game.game_atlas,
                                                    .size = sx_vec2f(tile_size * 0.7f, 0),
                                                    .color = sx_colorn((0xffffffff)),
                                                    .clip = the_game.enemy_clips[i] });

        the_game.enemies[i].pos = sx_vec2f(x, y);
        the_game.enemies[i].start_pos = the_game.enemies[i].pos;

        float dd = ((float)i / (float)MAX_ENEMIES);
        the_game.enemies[i].wait_duration = dd + ENEMY_WAIT_DURATION;

        x += tile_size;
    }

    // enemy group properties
    float group_width = x - start_x;
    the_game.enemy_move_dist = /*(GAME_BOARD_WIDTH - group_width) * 0.5f - tile_size * 0.5f*/tile_size*2.0f - tile_size*0.5f;
    the_game.enemy_move_duration = 0.5f;
    the_game.tile_size = tile_size;
}

static bool init()
{
    // register main graphics stage.
    // at least one stage should be registered if you want to draw anything
    g_stage = the_gfx->stage_register("main", (rizz_gfx_stage){ .id = 0 });
    sx_assert(g_stage.id);

    the_vfs->mount("../../assets", "/assets");

    // camera
    // projection: setup for ortho, total-width = GAME_BOARD_WIDTH
    // view: Y-UP
    sx_vec2 screen_size = the_app->sizef();
    const float view_width = GAME_BOARD_WIDTH * 0.5f;
    const float view_height = screen_size.y * view_width / screen_size.x;
    the_camera->init(&the_game.cam, 50.0f,
                     sx_rectf(-view_width, -view_height, view_width, view_height), -5.0f, 5.0f);
    the_camera->lookat(&the_game.cam, sx_vec3f(0, 0.0f, 1.0), SX_VEC3_ZERO, SX_VEC3_UNITY);

    create_enemies();

    return true;
}

static void shutdown() {}

static void update(float dt)
{
    the_sprite->animclip_update_batch(the_game.enemy_clips, MAX_ENEMIES, dt);

    enemy_direction_t dir;
    bool allow_change_dir = true;
    for (int i = 0; i < MAX_ENEMIES - 1; i++) {
        if (the_game.enemies[i].move_dir != the_game.enemies[i+1].move_dir) {
            allow_change_dir = false;
            break;
        }
        dir = the_game.enemies[i].move_dir;
    }
    if (allow_change_dir) {
        the_game.enemy_move_dir = dir;
    }

    // update enemy movement
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_t* e = &the_game.enemies[i];
        if (!e->move) {
            e->wait_tm += dt;
            if (e->wait_tm >= e->wait_duration && e->move_dir == the_game.enemy_move_dir) {
                e->wait_tm = 0;
                switch (the_game.enemy_move_dir) {
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
        } else {
            // move to target
            float t = e->move_tm / the_game.enemy_move_duration;
            t = sx_min(e->move_tm, 1.0f);
            e->pos = sx_vec2_lerp(e->start_pos, e->target_pos, t);
            e->move_tm += dt;

            if (t >= 1.0f) {
                e->move_tm = 0;
                e->xoffset += (e->target_pos.x - e->start_pos.x);
                e->start_pos = e->target_pos;
                e->move = false;

                // change direction
                if (the_game.enemy_move_dir == ENEMY_MOVEMENT_LEFT &&
                    e->xoffset <= -the_game.enemy_move_dist) {
                    e->move_dir = ENEMY_MOVEMENT_DOWN;
                    the_game.enemy_next_move_dir = ENEMY_MOVEMENT_RIGHT;
                } else if (the_game.enemy_move_dir == ENEMY_MOVEMENT_RIGHT &&
                           e->xoffset >= the_game.enemy_move_dist) {
                    e->move_dir = ENEMY_MOVEMENT_DOWN;
                    the_game.enemy_next_move_dir = ENEMY_MOVEMENT_LEFT;
                } else if (the_game.enemy_move_dir = ENEMY_MOVEMENT_DOWN) {
                    e->move_dir = the_game.enemy_next_move_dir;
                }
            }
        }
    }
}

static void render()
{
    sg_pass_action pass_action = { .colors[0] = { SG_ACTION_CLEAR, { 0.0f, 0.0f, 0.0f, 1.0f } },
                                   .depth = { SG_ACTION_CLEAR, 1.0f } };

    the_gfx->staged.begin(g_stage);
    the_gfx->staged.begin_default_pass(&pass_action, the_app->width(), the_app->height());

    sx_mat4 proj = the_camera->ortho_mat(&the_game.cam);
    sx_mat4 view = the_camera->view_mat(&the_game.cam);
    sx_mat4 vp = sx_mat4_mul(&proj, &view);

    sx_mat3 enemy_mats[MAX_ENEMIES];
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemy_mats[i] = sx_mat3_translate(the_game.enemies[i].pos.x, the_game.enemies[i].pos.y);
    }
    the_sprite->draw_batch(the_game.enemy_sprites, MAX_ENEMIES, &vp, enemy_mats, NULL);

    the_gfx->staged.end_pass();
    the_gfx->staged.end();

    // Use imgui UI
    if (the_imgui) {
        the_imgui->SetNextWindowContentSize(sx_vec2f(100.0f, 50.0f));
        if (the_imgui->Begin("space-invaders", NULL, 0)) {
            the_imgui->LabelText("Fps", "%.3f", the_core->fps());
        }
        the_imgui->End();
    }
}

rizz_plugin_decl_main(pacman, plugin, e)
{
    switch (e) {
    case RIZZ_PLUGIN_EVENT_STEP:
        update((float)sx_tm_sec(the_core->delta_tick()));
        render();
        break;

    case RIZZ_PLUGIN_EVENT_INIT:
        // runs only once for application. Retreive needed APIs
        the_core = plugin->api->get_api(RIZZ_API_CORE, 0);
        the_gfx = plugin->api->get_api(RIZZ_API_GFX, 0);
        the_app = plugin->api->get_api(RIZZ_API_APP, 0);
        the_camera = plugin->api->get_api(RIZZ_API_CAMERA, 0);
        the_vfs = plugin->api->get_api(RIZZ_API_VFS, 0);
        the_asset = plugin->api->get_api(RIZZ_API_ASSET, 0);
        the_imgui = plugin->api->get_api_byname("imgui", 0);
        the_sprite = plugin->api->get_api_byname("sprite", 0);

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

rizz_plugin_decl_event_handler(pacman, e)
{
    switch (e->type) {
    case RIZZ_APP_EVENTTYPE_UPDATE_APIS:
        the_imgui = the_plugin->get_api_byname("imgui", 0);
        the_sprite = the_plugin->get_api_byname("sprite", 0);
        break;

    default:
        break;
    }
}

rizz_game_decl_config(conf)
{
    conf->app_name = "space-invaders";
    conf->app_version = 1000;
    conf->app_title = "space-invaders";
    conf->window_width = 800;
    conf->window_height = 600;
    conf->core_flags |= RIZZ_CORE_FLAG_VERBOSE;
    conf->multisample_count = 4;
    conf->swap_interval = 1;
    conf->plugins[0] = "imgui";
    conf->plugins[1] = "2dtools";
}
