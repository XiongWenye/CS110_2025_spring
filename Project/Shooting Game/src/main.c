#include "lcd/lcd.h"
#include "utils.h"
#include "assembly/example.h"
#include "assembly/select.h"
#include <math.h> // For sinf, cosf

// Game constants
#define SCREEN_WIDTH 120
#define SCREEN_HEIGHT 80

// Entity sizes (kept as int for pixel dimensions)
#define PLAYER_SIZE 6
#define ENEMY_SIZE 4
#define BULLET_SIZE 1
#define MAX_BULLETS 150
#define MAX_ENEMY_BULLETS 300
#define MAX_ENEMIES 15

// Tracking bullets
#define MAX_TRACKING_BULLETS 50
#define TRACKING_BULLET_SIZE 3
#define TRACKING_BULLET_SPEED 2.0f // Float speed

// Entity shapes
#define SHAPE_HOLLOW_CIRCLE 0
#define SHAPE_HOLLOW_SQUARE 1
#define SHAPE_SOLID_SQUARE 2

// Bullet patterns
#define PATTERN_NORMAL 0
#define PATTERN_SPIRAL 1
#define PATTERN_PARABOLA 2

// FPS and entity counting
#define FPS_SAMPLE_FRAMES 8
#define ENTITY_UPDATE_INTERVAL 20

// Player structure
typedef struct {
    float x, y;
    float speed;
} Player;

// Bullet structure
typedef struct {
    float x, y;
    int active;
    float speed;
    float dx, dy;       // Base direction vector
    int shape;
    // For special patterns
    int pattern;
    float angle;          // Current angle for spiral/wave
    float angle_increment; // How much angle changes per frame
    float amplitude;      // Width of the spiral/wave
} Bullet;

// Enemy structure
typedef struct {
    float x, y;
    int active;
    float speed;
    int shoot_timer;
    int type;
    int shape;
} Enemy;

// Tracking Bullet structure
typedef struct {
    float x, y;
    int active;
    int target_enemy_index;
    int lifetime;
} TrackingBullet;

// Game state
Player player;

Bullet player_bullets[MAX_BULLETS];
Bullet enemy_bullets[MAX_ENEMY_BULLETS];
Enemy enemies[MAX_ENEMIES];
TrackingBullet tracking_bullets[MAX_TRACKING_BULLETS];

// Active list management
int active_player_bullet_indices[MAX_BULLETS];
int num_active_player_bullets = 0;

int active_enemy_bullet_indices[MAX_ENEMY_BULLETS];
int num_active_enemy_bullets = 0;

int active_enemy_indices[MAX_ENEMIES];
int num_active_enemies = 0;

int active_tracking_bullet_indices[MAX_TRACKING_BULLETS];
int num_active_tracking_bullets = 0;

// Random number generator state
static unsigned int rand_seed = 12345;

// Performance counters
static int displayed_fps = 60;
static int displayed_entities = 0;
static int entity_update_counter = 0;
static int last_displayed_fps = -1;
static int last_displayed_entities = -1;
static uint64_t fps_measurement_start_time = 0;
static uint32_t fps_frame_count = 0;


void Inp_init(void) {
  rcu_periph_clock_enable(RCU_GPIOA);
  rcu_periph_clock_enable(RCU_GPIOC);
  gpio_init(GPIOA, GPIO_MODE_IPD, GPIO_OSPEED_50MHZ, GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
  gpio_init(GPIOC, GPIO_MODE_IPD, GPIO_OSPEED_50MHZ, GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
}

void IO_init(void) {
  Inp_init();
  Lcd_Init();
}

int simple_rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed / 65536) % 32768;
}

float random_float(float min, float max) {
    if (min >= max) return min;
    return min + ((float)simple_rand() / 32767.0f) * (max - min);
}

void draw_rect_optimized(int x, int y, int width, int height, u16 color) {
    int x0 = x;
    int y0 = y;
    int x1 = x + width - 1;
    int y1 = y + height - 1;

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= SCREEN_WIDTH) x1 = SCREEN_WIDTH - 1;
    if (y1 >= SCREEN_HEIGHT) y1 = SCREEN_HEIGHT - 1;

    if (x0 > x1 || y0 > y1) return;
    LCD_Fill(x0, y0, x1, y1, color);
}

void clear_rect(float x, float y, int width, int height) {
    draw_rect_optimized((int)x, (int)y, width, height, BLACK);
}

static void LCD_DrawHollowRect(int x1, int y1, int x2, int y2, u16 color) {
    if (x1 > x2) { int temp = x1; x1 = x2; x2 = temp; }
    if (y1 > y2) { int temp = y1; y1 = y2; y2 = temp; }

    LCD_Fill(x1, y1, x2, y1, color);
    LCD_Fill(x1, y2, x2, y2, color);
    if (y1 + 1 <= y2 -1) {
        LCD_Fill(x1, y1 + 1, x1, y2 - 1, color);
        LCD_Fill(x2, y1 + 1, x2, y2 - 1, color);
    }
}

void clear_shape(float x, float y, int size, int shape) {
    switch(shape) {
        case SHAPE_HOLLOW_CIRCLE:
            LCD_DrawCircle((int)(x + size/2.0f), (int)(y + size/2.0f), size, BLACK);
            break;
        case SHAPE_HOLLOW_SQUARE:
            LCD_DrawHollowRect((int)x, (int)y, (int)(x + size - 1), (int)(y + size - 1), BLACK);
            break;
        case SHAPE_SOLID_SQUARE:
            draw_rect_optimized((int)x, (int)y, size, size, BLACK);
            break;
    }
}

void draw_shape(float x, float y, int size, int shape, u16 color) {
    switch(shape) {
        case SHAPE_HOLLOW_CIRCLE:
            LCD_DrawCircle((int)(x + size/2.0f), (int)(y + size/2.0f), size, color);
            break;
        case SHAPE_HOLLOW_SQUARE:
            LCD_DrawHollowRect((int)x, (int)y, (int)(x + size - 1), (int)(y + size - 1), color);
            break;
        case SHAPE_SOLID_SQUARE:
            draw_rect_optimized((int)x, (int)y, size, size, color);
            break;
    }
}

void update_fps_counter(void) {
    uint64_t current_time = get_timer_value();
    fps_frame_count++;
    if (fps_measurement_start_time == 0) {
        fps_measurement_start_time = current_time;
        fps_frame_count = 0;
        return;
    }
    if (fps_frame_count >= 30) {
        uint64_t elapsed_time = current_time - fps_measurement_start_time;
        if (elapsed_time > 0) {
            uint64_t ticks_per_second = SystemCoreClock / 4;
            displayed_fps = (fps_frame_count * ticks_per_second) / elapsed_time;
            if (displayed_fps > 99) displayed_fps = 99;
            if (displayed_fps < 0) displayed_fps = 0;
        }
        fps_measurement_start_time = current_time;
        fps_frame_count = 0;
    }
}

int count_active_entities_quick(void) {
    return num_active_player_bullets + num_active_enemy_bullets + num_active_tracking_bullets ;
}

void update_entity_counter_optimized(void) {
    entity_update_counter++;
    if (entity_update_counter >= ENTITY_UPDATE_INTERVAL) {
        entity_update_counter = 0;
        displayed_entities = count_active_entities_quick();
        if (displayed_entities > 999) displayed_entities = 999;
    }
}

void draw_performance_counters(void) {
    if (displayed_fps != last_displayed_fps) {
        draw_rect_optimized(SCREEN_WIDTH + 5, 0, 35, 25, BLACK);
        LCD_ShowString(SCREEN_WIDTH + 5 , 5, (u8*)"FPS", WHITE);
        LCD_ShowNum(SCREEN_WIDTH + 5, 20, displayed_fps, 2, WHITE);
        last_displayed_fps = displayed_fps;
    }
    if (displayed_entities != last_displayed_entities) {
        draw_rect_optimized(SCREEN_WIDTH + 5, 25, 35, 25, BLACK);
        LCD_ShowString(SCREEN_WIDTH + 5, 35, (u8*)"ENT", WHITE);
        LCD_ShowNum(SCREEN_WIDTH +5 , 50, displayed_entities, 3, WHITE);
        last_displayed_entities = displayed_entities;
    }
}

int find_nearest_enemy(float x, float y) {
    int nearest_global_idx = -1;
    float min_distance_squared = 9999999.0f;

    for (int i = 0; i < num_active_enemies; i++) {
        int enemy_idx = active_enemy_indices[i];
        // Ensure we are checking against an active enemy
        if (!enemies[enemy_idx].active) continue; 
        float dx = enemies[enemy_idx].x - x;
        float dy = enemies[enemy_idx].y - y;
        float distance_squared = dx * dx + dy * dy;
        if (distance_squared < min_distance_squared) {
            min_distance_squared = distance_squared;
            nearest_global_idx = enemy_idx;
        }
    }
    return nearest_global_idx;
}

void fire_tracking_bullet(void) {
    int target_idx = find_nearest_enemy(player.x, player.y);
    if (target_idx == -1 || num_active_tracking_bullets >= MAX_TRACKING_BULLETS) return;

    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) {
        if (!tracking_bullets[i].active) {
            tracking_bullets[i].x = player.x + PLAYER_SIZE / 2.0f - TRACKING_BULLET_SIZE / 2.0f;
            tracking_bullets[i].y = player.y;
            tracking_bullets[i].active = 1;
            tracking_bullets[i].target_enemy_index = target_idx;
            tracking_bullets[i].lifetime = 300;

            active_tracking_bullet_indices[num_active_tracking_bullets++] = i;
            break;
        }
    }
}

void spawn_random_enemy(void) {
    if (num_active_enemies >= MAX_ENEMIES) return;
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            enemies[i].x = random_float(0.0f, (float)SCREEN_WIDTH - ENEMY_SIZE);
            enemies[i].y = random_float(5.0f, 25.0f); // Spawns near top of screen
            // To spawn above screen: enemies[i].y = -random_float(5.0f, 25.0f);
            enemies[i].active = 1;
            enemies[i].speed = random_float(0.5f, 1.5f); 
            enemies[i].shoot_timer = simple_rand() % 30 + 15;
            enemies[i].type = simple_rand() % 3; 
            enemies[i].shape = simple_rand() % 3;
            
            active_enemy_indices[num_active_enemies++] = i;

            if (enemies[i].type == 0 || enemies[i].type == 1) {
                enemies[i].speed = 0.0f;
            }
            if (enemies[i].type == 0) {
                enemies[i].y = SCREEN_HEIGHT - ENEMY_SIZE - 10.0f; // Bottom spawn
            }
            break;
        }
    }
}

void init_game(void) { // Consider taking (int difficulty) if it affects init
    player.x = SCREEN_WIDTH / 2.0f;
    player.y = (float)SCREEN_HEIGHT - PLAYER_SIZE - 2.0f;
    player.speed = 2.5f;

    num_active_player_bullets = 0;
    for (int i = 0; i < MAX_BULLETS; i++) player_bullets[i].active = 0;

    num_active_enemy_bullets = 0;
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) enemy_bullets[i].active = 0;
    
    num_active_tracking_bullets = 0;
    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) tracking_bullets[i].active = 0;

    num_active_enemies = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = 0;

    for (int i = 0; i < 5; i++) spawn_random_enemy();

    fps_measurement_start_time = 0;
    fps_frame_count = 0;
    // Reset last displayed counters to force redraw on first frame
    last_displayed_fps = -1;
    last_displayed_entities = -1; 
}

int button1_debounce1 = 0;
int button1_debounce2 = 400;
int millisecond(){
    uint64_t start_mtime;
    uint64_t tmp = get_timer_value();
    do {
        start_mtime = get_timer_value();
    } while (start_mtime == tmp);
    return (start_mtime / (SystemCoreClock / 4000)); // Convert to milliseconds
}// return the now time
void update_player(void) {
    static float prev_x = -1.0f, prev_y = -1.0f;
    static int button2_debounce = 0;
    static int center_button_debounce = 0;

    if (prev_x != -1.0f) { // Check if prev_x is initialized
         clear_rect(prev_x, prev_y, PLAYER_SIZE, PLAYER_SIZE);
    }
    prev_x = player.x;
    prev_y = player.y;

    if (Get_Button(JOY_LEFT) && player.x > 0.0f) player.x -= player.speed;
    if (Get_Button(JOY_RIGHT) && player.x < (float)SCREEN_WIDTH - PLAYER_SIZE) player.x += player.speed;
    if (Get_Button(JOY_UP) && player.y > 0.0f) player.y -= player.speed;
    if (Get_Button(JOY_DOWN) && player.y < (float)SCREEN_HEIGHT - PLAYER_SIZE) player.y += player.speed;

    // Debounce logic for BUTTON_1
    if (Get_Button(BUTTON_1)) {
        button1_debounce2 = millisecond();
        if (button1_debounce2 - button1_debounce1 >= 300) {
            button1_debounce1 = button1_debounce2; // Reset debounce timer
            for (int burst = 0; burst < 3; burst++) {
                if (num_active_player_bullets < MAX_BULLETS) {
                    for (int i = 0; i < MAX_BULLETS; i++) {
                        if (!player_bullets[i].active) {
                            player_bullets[i].x = player.x + PLAYER_SIZE / 2.0f - BULLET_SIZE / 2.0f + (burst - 1) * (BULLET_SIZE + 1.5f);
                            player_bullets[i].y = player.y - BULLET_SIZE - burst * 2.0f;
                            player_bullets[i].active = 1;
                            player_bullets[i].speed = 4.5f;
                            player_bullets[i].dx = 0.0f;
                            player_bullets[i].dy = -1.0f;
                            player_bullets[i].shape = SHAPE_SOLID_SQUARE;
                            player_bullets[i].pattern = PATTERN_NORMAL;
                            active_player_bullet_indices[num_active_player_bullets++] = i;
                            break;
                        }
                    }
                }
            }
        }
    }

    if (Get_Button(BUTTON_2)) { 
        if (button2_debounce == 0) {
            button2_debounce = 20; 
            float directions[5][2] = {{0, -1}, {0.382f, -0.923f}, {-0.382f, -0.923f}, {0.707f, -0.707f}, {-0.707f, -0.707f}}; 
            int num_directions = 5;
            for (int dir = 0; dir < num_directions; dir++) {
                if (num_active_player_bullets < MAX_BULLETS) {
                     for (int i = 0; i < MAX_BULLETS; i++) {
                        if (!player_bullets[i].active) {
                            player_bullets[i].x = player.x + PLAYER_SIZE / 2.0f - BULLET_SIZE / 2.0f;
                            player_bullets[i].y = player.y;
                            player_bullets[i].active = 1;
                            player_bullets[i].speed = 3.5f;
                            player_bullets[i].dx = directions[dir][0];
                            player_bullets[i].dy = directions[dir][1];
                            player_bullets[i].shape = SHAPE_SOLID_SQUARE;
                            player_bullets[i].pattern = PATTERN_NORMAL; 
                            active_player_bullet_indices[num_active_player_bullets++] = i;
                            break;
                        }
                    }
                }
            }
        }
    }
     if (button2_debounce > 0) button2_debounce--;

    if (Get_Button(JOY_CTR)) { 
        if (center_button_debounce == 0) {
            center_button_debounce = 30;
            fire_tracking_bullet();
        }
    }
    if (center_button_debounce > 0) center_button_debounce--;
    
    draw_rect_optimized((int)player.x, (int)player.y, PLAYER_SIZE, PLAYER_SIZE, BLUE);
}

void update_player_bullets_optimized(void) {
    for (int i = 0; i < num_active_player_bullets; i++) {
        int bullet_idx = active_player_bullet_indices[i];
        Bullet* b = &player_bullets[bullet_idx];

        clear_rect(b->x, b->y, BULLET_SIZE, BULLET_SIZE);

        if (b->pattern == PATTERN_NORMAL) {
            b->x += b->dx * b->speed;
            b->y += b->dy * b->speed;
        } else if (b->pattern == PATTERN_SPIRAL) { 
            float base_move_x = b->dx * b->speed;
            float base_move_y = b->dy * b->speed;
            float perp_dx = -b->dy;
            float perp_dy = b->dx;
            if (perp_dx == 0.0f && perp_dy == 0.0f) perp_dx = 1.0f;
            float offset = sinf(b->angle) * b->amplitude;
            b->x += base_move_x + perp_dx * offset;
            b->y += base_move_y + perp_dy * offset;
            b->angle += b->angle_increment;
        }

        if (b->x < -BULLET_SIZE || b->x > SCREEN_WIDTH || b->y < -BULLET_SIZE || b->y > SCREEN_HEIGHT) {
            b->active = 0;
        }

        if (!b->active) {
            active_player_bullet_indices[i] = active_player_bullet_indices[--num_active_player_bullets];
            i--;
        } else {
            u16 bullet_color = WHITE; 
            if (b->dx != 0.0f || (b->dx == 0.0f && b->dy != -1.0f && b->dy != 0.0f) ) { 
                 bullet_color = CYAN;
            }
            draw_rect_optimized((int)b->x, (int)b->y, BULLET_SIZE, BULLET_SIZE, bullet_color);
        }
    }
}

void update_tracking_bullets(void) {
    for (int i = 0; i < num_active_tracking_bullets; i++) {
        int tb_idx = active_tracking_bullet_indices[i];
        TrackingBullet* tb = &tracking_bullets[tb_idx];

        clear_rect(tb->x, tb->y, TRACKING_BULLET_SIZE, TRACKING_BULLET_SIZE);

        tb->lifetime--;
        if (tb->lifetime <= 0) {
            tb->active = 0;
        } else {
            int target_global_idx = tb->target_enemy_index;
            if (target_global_idx < 0 || target_global_idx >= MAX_ENEMIES || !enemies[target_global_idx].active) {
                tb->target_enemy_index = find_nearest_enemy(tb->x, tb->y); // Find new target if old one is invalid
                target_global_idx = tb->target_enemy_index; // Update target_global_idx with the new target
            }

            if (target_global_idx != -1 && enemies[target_global_idx].active) { 
                float target_x = enemies[target_global_idx].x + ENEMY_SIZE / 2.0f;
                float target_y = enemies[target_global_idx].y + ENEMY_SIZE / 2.0f;
                float dx_track = target_x - (tb->x + TRACKING_BULLET_SIZE / 2.0f);
                float dy_track = target_y - (tb->y + TRACKING_BULLET_SIZE / 2.0f);

                float dist = sqrtf(dx_track * dx_track + dy_track * dy_track);
                if (dist > TRACKING_BULLET_SPEED) { // Only normalize and move if further than one step
                    tb->x += (dx_track / dist) * TRACKING_BULLET_SPEED;
                    tb->y += (dy_track / dist) * TRACKING_BULLET_SPEED;
                } else if (dist > 0) { // Closer than one step, just move to target
                     tb->x += dx_track;
                     tb->y += dy_track;
                }
            } else { 
                tb->y -= TRACKING_BULLET_SPEED; 
            }
        }
        
        if (tb->x < -TRACKING_BULLET_SIZE || tb->x > SCREEN_WIDTH || tb->y < -TRACKING_BULLET_SIZE || tb->y > SCREEN_HEIGHT) {
             tb->active = 0;
        }

        if (!tb->active) {
            active_tracking_bullet_indices[i] = active_tracking_bullet_indices[--num_active_tracking_bullets];
            i--;
        } else {
            draw_rect_optimized((int)tb->x, (int)tb->y, TRACKING_BULLET_SIZE, TRACKING_BULLET_SIZE, GREEN);
        }
    }
}

void update_enemies(void) {
    static int spawn_timer = 0;

    spawn_timer++;
    if (spawn_timer >= 100) { 
        spawn_timer = 0;
        spawn_random_enemy(); 
    }

    for (int i = 0; i < num_active_enemies; i++) {
        int enemy_idx = active_enemy_indices[i];
        Enemy* e = &enemies[enemy_idx];

        clear_shape(e->x, e->y, ENEMY_SIZE, e->shape);

        e->y += e->speed;

        if (e->y > SCREEN_HEIGHT) {
            e->y = SCREEN_HEIGHT;
            e->speed = -e->speed; // Reverse direction if it goes off the bottom
        }

        if (e->y < 0.0f) {
            e->y = 0.0f;
            e->speed = -e->speed; // Reverse direction if it goes off the top
        }

        if (e->x < 0.0f) {
            e->x = 0.0f;
            e->speed = -e->speed; // Reverse direction if it goes off the left
        }
        else if (e->x > (float)SCREEN_WIDTH - ENEMY_SIZE) {
            e->x = (float)SCREEN_WIDTH - ENEMY_SIZE;
            e->speed = -e->speed; // Reverse direction if it goes off the right
        }

       
        if (!e->active) {
            active_enemy_indices[i] = active_enemy_indices[--num_active_enemies];
            i--;
        } else {
            u16 enemy_color;
            switch (e->type) {
                case 0: enemy_color = RED; break;       
                case 1: enemy_color = MAGENTA; break;
                case 2: enemy_color = YELLOW; break;
                default: enemy_color = RED; break;
            }
            draw_shape(e->x, e->y, ENEMY_SIZE, e->shape, enemy_color);
        }
    }
}

void update_enemy_bullets_optimized(void) {
    // --- Enemy Shooting Logic ---
    for (int i = 0; i < num_active_enemies; i++) {
        int enemy_idx = active_enemy_indices[i];
        Enemy* e = &enemies[enemy_idx];
        if (!e->active) continue;

        e->shoot_timer++;
        int shoot_interval;
        int bullet_count = 1; 
        float bullet_speed = 0.8f;

        if (e->type == 0) { 
            shoot_interval = 20; 
            bullet_count = 50;    
        } else if (e->type == 1) { 
            shoot_interval = 30;
            bullet_count = 7;
            bullet_speed = 0.5f;
        } else if (e->type == 2) { 
            shoot_interval = 40;
            bullet_count = 25; 
            bullet_speed = 0.5f;
        } else { 
            shoot_interval = 60;
            bullet_count = 1;
        }

        if (e->shoot_timer >= shoot_interval) {
            e->shoot_timer = 0;
            
            if (e->type == 0) { 
                for (int k = 0; k < bullet_count && num_active_enemy_bullets < MAX_ENEMY_BULLETS; k++) {
                     for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                        if (!enemy_bullets[j].active) {
                            enemy_bullets[j].x = e->x + ENEMY_SIZE / 2.0f - BULLET_SIZE / 2.0f;
                            enemy_bullets[j].y = e->y - BULLET_SIZE; 
                            enemy_bullets[j].active = 1;
                            enemy_bullets[j].speed = 0.5f;      
                            enemy_bullets[j].dx = 0.0f;         
                            enemy_bullets[j].dy = -1.0f;        
                            enemy_bullets[j].shape = SHAPE_SOLID_SQUARE; 
                            enemy_bullets[j].pattern = PATTERN_SPIRAL;
                            enemy_bullets[j].angle = random_float(0.0f, 6.28318f); 
                            enemy_bullets[j].angle_increment = 0.15f; 
                            enemy_bullets[j].amplitude = 6.0f;   
                            active_enemy_bullet_indices[num_active_enemy_bullets++] = j;
                            break;
                        }
                    }
                }
            } else if (e->type == 2) { 
                float directions[8][2] = {{0,1},{0.707f,0.707f},{1,0},{0.707f,-0.707f},{0,-1},{-0.707f,-0.707f},{-1,0},{-0.707f,0.707f}};
                for (int dir = 0; dir < 8 && num_active_enemy_bullets < MAX_ENEMY_BULLETS; dir++) {
                    for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                        if (!enemy_bullets[j].active) {
                            enemy_bullets[j].x = e->x + ENEMY_SIZE / 2.0f - BULLET_SIZE / 2.0f;
                            enemy_bullets[j].y = e->y + ENEMY_SIZE / 2.0f - BULLET_SIZE / 2.0f;
                            enemy_bullets[j].active = 1;
                            enemy_bullets[j].speed = bullet_speed;
                            enemy_bullets[j].dx = directions[dir][0];
                            enemy_bullets[j].dy = directions[dir][1];
                            enemy_bullets[j].shape = e->shape;
                            enemy_bullets[j].pattern = PATTERN_PARABOLA;
                            active_enemy_bullet_indices[num_active_enemy_bullets++] = j;
                            break;
                        }
                    }
                }
            } else { 
                float total_spread_width = (bullet_count - 1) * (BULLET_SIZE + 1.0f);
                float start_x_offset = (bullet_count > 1) ? -total_spread_width / 2.0f : 0.0f;

                for (int k = 0; k < bullet_count && num_active_enemy_bullets < MAX_ENEMY_BULLETS; k++) {
                    for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                        if (!enemy_bullets[j].active) {
                            enemy_bullets[j].x = e->x + ENEMY_SIZE / 2.0f - BULLET_SIZE / 2.0f + start_x_offset + k * (BULLET_SIZE + 1.0f);
                            enemy_bullets[j].y = e->y + ENEMY_SIZE;
                            enemy_bullets[j].active = 1;
                            enemy_bullets[j].speed = bullet_speed;
                            enemy_bullets[j].dx = 0.0f;
                            enemy_bullets[j].dy = 1.0f; 
                            enemy_bullets[j].shape = e->shape;
                            enemy_bullets[j].pattern = PATTERN_NORMAL;
                            active_enemy_bullet_indices[num_active_enemy_bullets++] = j;
                            break;
                        }
                    }
                }
            }
        }
    }

    // --- Update and Draw Active Enemy Bullets ---
    for (int i = 0; i < num_active_enemy_bullets; i++) {
        int bullet_idx = active_enemy_bullet_indices[i];
        Bullet* b = &enemy_bullets[bullet_idx];

        // MODIFICATION: Use clear_rect for all enemy bullets as they are drawn as solid squares
        clear_rect(b->x, b->y, BULLET_SIZE, BULLET_SIZE);

        if (b->pattern == PATTERN_NORMAL) {
            b->x += b->dx * b->speed;
            b->y += b->dy * b->speed;
        } else if (b->pattern == PATTERN_SPIRAL) {
            float base_move_x = b->dx * b->speed;
            float base_move_y = b->dy * b->speed;
            float perp_dx = -b->dy;
            float perp_dy = b->dx;
            if (perp_dx == 0.0f && perp_dy == 0.0f && (b->dx != 0.0f || b->dy != 0.0f)) {  }
            else if (perp_dx == 0.0f && perp_dy == 0.0f) { perp_dx = 1.0f; } 


            float offset = sinf(b->angle) * b->amplitude;
            
            b->x += base_move_x + perp_dx * offset;
            b->y += base_move_y + perp_dy * offset;
            
            b->angle += b->angle_increment;
        } else if (b->pattern == PATTERN_PARABOLA) {
            b->x += b->dx * b->speed;
            b->y += b->dy * b->speed;
            b->dy += 0.1f; 
        }

        if (b->x < -BULLET_SIZE * 2 || b->x > SCREEN_WIDTH + BULLET_SIZE || b->y < -BULLET_SIZE * 2 || b->y > SCREEN_HEIGHT + BULLET_SIZE) { 
            b->active = 0;
        }

        if (!b->active) {
            active_enemy_bullet_indices[i] = active_enemy_bullet_indices[--num_active_enemy_bullets];
            i--; 
        } else {
            u16 bullet_color = YELLOW; 
            if (b->pattern == PATTERN_SPIRAL) {
                bullet_color = MAGENTA; 
            }
            draw_rect_optimized((int)b->x, (int)b->y, BULLET_SIZE, BULLET_SIZE, bullet_color);
        }
    }
}

void check_collisions_optimized(void) {
    // Player bullets vs Enemies
    for (int i = 0; i < num_active_player_bullets; i++) {
        int pb_idx = active_player_bullet_indices[i];
        Bullet* pb = &player_bullets[pb_idx];
        if (!pb->active) continue;

        for (int j = 0; j < num_active_enemies; j++) {
            int enemy_idx = active_enemy_indices[j];
            Enemy* e = &enemies[enemy_idx];
            if (!e->active) continue;

            if (pb->x < e->x + ENEMY_SIZE &&
                pb->x + BULLET_SIZE > e->x &&
                pb->y < e->y + ENEMY_SIZE &&
                pb->y + BULLET_SIZE > e->y) {
                
                clear_rect(pb->x, pb->y, BULLET_SIZE, BULLET_SIZE);
                clear_shape(e->x, e->y, ENEMY_SIZE, e->shape);
                
                pb->active = 0;
                e->active = 0; 
                active_enemy_indices[j] = active_enemy_indices[--num_active_enemies];
                j--; 
                break; 
            }
        }
    }

    // Tracking bullets vs Enemies
    for (int i = 0; i < num_active_tracking_bullets; i++) {
        int tb_idx = active_tracking_bullet_indices[i];
        TrackingBullet* tb = &tracking_bullets[tb_idx];
        if (!tb->active) continue;

        for (int j = 0; j < num_active_enemies; j++) {
            int enemy_idx = active_enemy_indices[j];
            Enemy* e = &enemies[enemy_idx];
            if (!e->active) continue;

             if (tb->x < e->x + ENEMY_SIZE &&
                tb->x + TRACKING_BULLET_SIZE > e->x &&
                tb->y < e->y + ENEMY_SIZE &&
                tb->y + TRACKING_BULLET_SIZE > e->y) {
                
                clear_rect(tb->x, tb->y, TRACKING_BULLET_SIZE, TRACKING_BULLET_SIZE);
                clear_shape(e->x, e->y, ENEMY_SIZE, e->shape);
                
                tb->active = 0; 
                e->active = 0;
                active_enemy_indices[j] = active_enemy_indices[--num_active_enemies];
                j--;
                break; 
            }
        }
    }

}

void game_loop(int difficulty) {
    init_game(); // Consider passing difficulty to init_game if needed
    LCD_Clear(BLACK);

    while (1) {
        update_fps_counter();
        update_entity_counter_optimized();
        
        update_player(); 
        update_enemies(); 
        
        update_player_bullets_optimized();
        update_tracking_bullets();
        update_enemy_bullets_optimized(); 
        
        check_collisions_optimized(); 
        
        draw_performance_counters();

        delay_1ms(1); 
    }
}

int main(void) {
  IO_init();
  int difficulty = select(); 
  game_loop(difficulty);
  return 0;
}