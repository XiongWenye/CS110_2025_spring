#include "lcd/lcd.h"
#include "utils.h"
#include "assembly/example.h"
#include "assembly/select.h"
#include <math.h> // For M_PI, cos, sin - if using float math for bullets. Otherwise, not needed for integer lookup.

// Game constants
#define SCREEN_WIDTH 120
#define SCREEN_HEIGHT 80
#define PLAYER_SIZE 6
#define ENEMY_SIZE 4
#define BULLET_SIZE 2
#define MAX_BULLETS 300
#define MAX_ENEMY_BULLETS 300 // Keep high or increase if many bullets per enemy volley
// MODIFICATION: Fewer enemies
#define MAX_ENEMIES 20 // Was 100

// Tracking bullets
#define MAX_TRACKING_BULLETS 50
#define TRACKING_BULLET_SIZE 3
#define TRACKING_BULLET_SPEED 2

// Entity shapes
#define SHAPE_HOLLOW_CIRCLE 0
#define SHAPE_HOLLOW_SQUARE 1
#define SHAPE_SOLID_SQUARE 2

// FPS and entity counting
#define FPS_SAMPLE_FRAMES 8
#define ENTITY_UPDATE_INTERVAL 1

// Player structure
typedef struct {
    int x, y;
    int speed;
} Player;

// Bullet structure
typedef struct {
    int x, y;
    int active;
    int speed;
    int dx, dy;
    int shape;
} Bullet;

// Enemy structure
typedef struct {
    int x, y;
    int active;
    int speed;
    int shoot_timer;
    int type; // 0: normal, 1: circular shooter
    int shape;
} Enemy;

// Tracking Bullet structure
typedef struct {
    int x, y;
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

int active_player_bullet_indices[MAX_BULLETS];
int num_active_player_bullets = 0;
int active_enemy_bullet_indices[MAX_ENEMY_BULLETS];
int num_active_enemy_bullets = 0;
int active_enemy_indices[MAX_ENEMIES];
int num_active_enemies = 0;
int active_tracking_bullet_indices[MAX_TRACKING_BULLETS];
int num_active_tracking_bullets = 0;

static unsigned int rand_seed = 12345;

static int displayed_fps = 60;
static int displayed_entities = 0;
static int entity_update_counter = 0;
static unsigned long frame_counter = 0;
static int last_displayed_fps = -1;
static int last_displayed_entities = -1;
static uint64_t fps_measurement_start_time = 0;
static uint32_t fps_frame_count = 0;

// NEW: Lookup table for 8-directional shots (integer dx, dy)
// These are not perfectly normalized for speed, but are simple.
// Bullet's `speed` parameter will scale these.
const int EIGHT_WAY_DX[] = {0, 1, 1, 1, 0, -1, -1, -1};
const int EIGHT_WAY_DY[] = {-1, -1, 0, 1, 1, 1, 0, -1};
#define NUM_EIGHT_WAY_DIRS 8


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

void clear_rect(int x, int y, int width, int height) {
    draw_rect_optimized(x, y, width, height, BLACK);
}

static void LCD_DrawHollowRect(u16 x1, u16 y1, u16 x2, u16 y2, u16 color) {
    if (x1 > x2) { u16 temp = x1; x1 = x2; x2 = temp; }
    if (y1 > y2) { u16 temp = y1; y1 = y2; y2 = temp; }
    LCD_Fill(x1, y1, x2, y1, color);
    LCD_Fill(x1, y2, x2, y2, color);
    if (y1 + 1 <= y2 -1) {
        LCD_Fill(x1, y1 + 1, x1, y2 - 1, color);
        LCD_Fill(x2, y1 + 1, x2, y2 - 1, color);
    }
}

void clear_shape(int x, int y, int size, int shape) {
    switch(shape) {
        case SHAPE_HOLLOW_CIRCLE: LCD_DrawCircle(x + size/2, y + size/2, size/2, BLACK); break;
        case SHAPE_HOLLOW_SQUARE: LCD_DrawHollowRect(x, y, x + size - 1, y + size - 1, BLACK); break;
        case SHAPE_SOLID_SQUARE: draw_rect_optimized(x, y, size, size, BLACK); break;
    }
}

void draw_shape(int x, int y, int size, int shape, u16 color) {
    switch(shape) {
        case SHAPE_HOLLOW_CIRCLE: LCD_DrawCircle(x + size/2, y + size/2, size/2, color); break;
        case SHAPE_HOLLOW_SQUARE: LCD_DrawHollowRect(x, y, x + size - 1, y + size - 1, color); break;
        case SHAPE_SOLID_SQUARE: draw_rect_optimized(x, y, size, size, color); break;
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
    return 1 + num_active_player_bullets + num_active_enemy_bullets + num_active_tracking_bullets + num_active_enemies;
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
    // Assuming LCD_W is physical width (e.g. 160) and SCREEN_WIDTH is game width (e.g. 120)
    // This draws in the margin to the right of the game screen.
    int ui_x_start = SCREEN_WIDTH + 5;
    if (ui_x_start + 35 > LCD_W) { // Check if there's actually space on the physical LCD
        // Not enough space, don't draw or draw elsewhere
        return;
    }

    if (displayed_fps != last_displayed_fps) {
        // It's better to use your LCD library's background color for clearing if available
        // For now, assuming BLACK is the background for the UI area.
        LCD_Fill(ui_x_start, 0, ui_x_start + 35 -1 , 25 -1, BLACK); // Clear FPS area
        LCD_ShowString(ui_x_start , 5, (u8*)"FPS", WHITE);
        LCD_ShowNum(ui_x_start, 20, displayed_fps, 2, WHITE);
        last_displayed_fps = displayed_fps;
    }
    if (displayed_entities != last_displayed_entities) {
        LCD_Fill(ui_x_start, 25, ui_x_start + 35 -1, 50 -1, BLACK); // Clear ENT area
        LCD_ShowString(ui_x_start, 35, (u8*)"ENT", WHITE);
        LCD_ShowNum(ui_x_start , 50, displayed_entities, 3, WHITE);
        last_displayed_entities = displayed_entities;
    }
}

int find_nearest_enemy(int x, int y) {
    int nearest_global_idx = -1;
    int min_distance_squared = 9999999;
    for (int i = 0; i < num_active_enemies; i++) {
        int enemy_idx = active_enemy_indices[i];
        int dx = enemies[enemy_idx].x - x;
        int dy = enemies[enemy_idx].y - y;
        int distance_squared = dx * dx + dy * dy;
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
            tracking_bullets[i].x = player.x + PLAYER_SIZE / 2 - TRACKING_BULLET_SIZE / 2;
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
            enemies[i].x = simple_rand() % (SCREEN_WIDTH - ENEMY_SIZE);
            enemies[i].y = -(simple_rand() % 10 + ENEMY_SIZE); // Spawn just above screen
            enemies[i].active = 1;
            enemies[i].speed = 1; // Slower enemies
            enemies[i].shoot_timer = simple_rand() % 100 + 50; // Longer initial timer
            // MODIFICATION: Assign enemy type for different shooting patterns
            enemies[i].type = simple_rand() % 2; // 0 for normal, 1 for circular for now
            enemies[i].shape = simple_rand() % 3;
            active_enemy_indices[num_active_enemies++] = i;
            break;
        }
    }
}

void init_game(void) {
    player.x = SCREEN_WIDTH / 2;
    player.y = SCREEN_HEIGHT - PLAYER_SIZE - 2;
    player.speed = 2;

    num_active_player_bullets = 0;
    for (int i = 0; i < MAX_BULLETS; i++) player_bullets[i].active = 0;
    num_active_enemy_bullets = 0;
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) enemy_bullets[i].active = 0;
    num_active_tracking_bullets = 0;
    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) tracking_bullets[i].active = 0;
    num_active_enemies = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = 0;

    // MODIFICATION: Fewer initial enemies
    for (int i = 0; i < 3; i++) spawn_random_enemy();

    frame_counter = 0;
    fps_measurement_start_time = 0;
    fps_frame_count = 0;
    last_displayed_fps = -1; // Force redraw of counters on first frame
    last_displayed_entities = -1;
}


void update_player(void) {
    static int prev_x = -1, prev_y = -1;
    static int button1_debounce = 0;
    static int button2_debounce = 0;
    static int center_button_debounce = 0;

    if (prev_x != -1) {
         clear_rect(prev_x, prev_y, PLAYER_SIZE, PLAYER_SIZE);
    }
    prev_x = player.x;
    prev_y = player.y;

    if (Get_Button(JOY_LEFT) && player.x > 0) player.x -= player.speed;
    if (Get_Button(JOY_RIGHT) && player.x < SCREEN_WIDTH - PLAYER_SIZE) player.x += player.speed;
    if (Get_Button(JOY_UP) && player.y > 0) player.y -= player.speed;
    if (Get_Button(JOY_DOWN) && player.y < SCREEN_HEIGHT - PLAYER_SIZE) player.y += player.speed;

    if (Get_Button(BUTTON_1)) {
        if (button1_debounce == 0) {
            button1_debounce = 5;
            for (int burst = 0; burst < 3; burst++) {
                if (num_active_player_bullets < MAX_BULLETS) {
                    for (int i = 0; i < MAX_BULLETS; i++) {
                        if (!player_bullets[i].active) {
                            player_bullets[i].x = player.x + PLAYER_SIZE / 2 - BULLET_SIZE / 2 + (burst -1) * (BULLET_SIZE + 1) ;
                            player_bullets[i].y = player.y - BULLET_SIZE - burst * 2;
                            player_bullets[i].active = 1;
                            player_bullets[i].speed = 4;
                            player_bullets[i].dx = 0;
                            player_bullets[i].dy = -1;
                            player_bullets[i].shape = SHAPE_SOLID_SQUARE;
                            active_player_bullet_indices[num_active_player_bullets++] = i;
                            break;
                        }
                    }
                }
            }
        }
    }
    if (button1_debounce > 0) button1_debounce--;

    if (Get_Button(BUTTON_2)) {
        if (button2_debounce == 0) {
            button2_debounce = 20;
            for (int dir = 0; dir < NUM_EIGHT_WAY_DIRS; dir++) { // Use the 8-way directions
                if (num_active_player_bullets < MAX_BULLETS) {
                     for (int i = 0; i < MAX_BULLETS; i++) {
                        if (!player_bullets[i].active) {
                            player_bullets[i].x = player.x + PLAYER_SIZE / 2 - BULLET_SIZE / 2;
                            player_bullets[i].y = player.y;
                            player_bullets[i].active = 1;
                            player_bullets[i].speed = 3; // Spread shot speed
                            player_bullets[i].dx = EIGHT_WAY_DX[dir];
                            player_bullets[i].dy = EIGHT_WAY_DY[dir];
                            player_bullets[i].shape = SHAPE_SOLID_SQUARE;
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

    draw_rect_optimized(player.x, player.y, PLAYER_SIZE, PLAYER_SIZE, BLUE);
}

void update_player_bullets_optimized(void) {
    for (int i = 0; i < num_active_player_bullets; i++) {
        int bullet_idx = active_player_bullet_indices[i];
        Bullet* b = &player_bullets[bullet_idx];
        clear_rect(b->x, b->y, BULLET_SIZE, BULLET_SIZE);
        b->x += b->dx * b->speed;
        b->y += b->dy * b->speed;
        if (b->x < -BULLET_SIZE || b->x > SCREEN_WIDTH || b->y < -BULLET_SIZE || b->y > SCREEN_HEIGHT) {
            b->active = 0;
        }
        if (!b->active) {
            active_player_bullet_indices[i] = active_player_bullet_indices[--num_active_player_bullets];
            i--;
        } else {
            u16 bullet_color = (b->dx == 0 && b->dy == -1) ? WHITE : CYAN;
            draw_rect_optimized(b->x, b->y, BULLET_SIZE, BULLET_SIZE, bullet_color);
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
                tb->target_enemy_index = find_nearest_enemy(tb->x, tb->y);
                target_global_idx = tb->target_enemy_index;
            }
            if (target_global_idx != -1) {
                int target_x = enemies[target_global_idx].x + ENEMY_SIZE / 2;
                int target_y = enemies[target_global_idx].y + ENEMY_SIZE / 2;
                int dx = target_x - (tb->x + TRACKING_BULLET_SIZE / 2);
                int dy = target_y - (tb->y + TRACKING_BULLET_SIZE / 2);
                if (dx > TRACKING_BULLET_SPEED) tb->x += TRACKING_BULLET_SPEED;
                else if (dx < -TRACKING_BULLET_SPEED) tb->x -= TRACKING_BULLET_SPEED;
                else tb->x += dx;
                if (dy > TRACKING_BULLET_SPEED) tb->y += TRACKING_BULLET_SPEED;
                else if (dy < -TRACKING_BULLET_SPEED) tb->y -= TRACKING_BULLET_SPEED;
                else tb->y += dy;
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
            draw_rect_optimized(tb->x, tb->y, TRACKING_BULLET_SIZE, TRACKING_BULLET_SIZE, GREEN);
        }
    }
}

void update_enemies(void) {
    static int spawn_timer = 0;
    // MODIFICATION: Slower enemy spawning
    spawn_timer++;
    if (spawn_timer >= 80) { // Was 20, then 60
        spawn_timer = 0;
        if (simple_rand() % 100 < 25) spawn_random_enemy(); // Was 40, then 15
    }

    for (int i = 0; i < num_active_enemies; i++) {
        int enemy_idx = active_enemy_indices[i];
        Enemy* e = &enemies[enemy_idx];
        clear_shape(e->x, e->y, ENEMY_SIZE, e->shape);
        e->y += e->speed;
        if (e->y > SCREEN_HEIGHT - ENEMY_SIZE) { e->y = SCREEN_HEIGHT - ENEMY_SIZE; e->speed *= -1; }
        else if (e->y < 0) { e->y = 0; e->speed *= -1; }
        if (simple_rand() % 100 < 5) { // Reduced horizontal jitter chance
            e->x += (simple_rand() % 3 - 1); // Smaller horizontal steps
            if (e->x < 0) e->x = 0;
            if (e->x > SCREEN_WIDTH - ENEMY_SIZE) e->x = SCREEN_WIDTH - ENEMY_SIZE;
        }
        if (!e->active) {
            active_enemy_indices[i] = active_enemy_indices[--num_active_enemies];
            i--;
        } else {
            u16 enemy_color;
            switch (e->type) {
                case 0: enemy_color = RED; break; // Standard enemy
                case 1: enemy_color = MAGENTA; break; // Circular shooter
                default: enemy_color = RED; break;
            }
            draw_shape(e->x, e->y, ENEMY_SIZE, e->shape, enemy_color);
        }
    }
}

void update_enemy_bullets_optimized(void) {
    for (int i = 0; i < num_active_enemies; i++) {
        int enemy_idx = active_enemy_indices[i];
        Enemy* e = &enemies[enemy_idx];
        if (!e->active) continue;

        e->shoot_timer++;
        int shoot_interval;
        // MODIFICATION: Longer intervals as volleys are bigger
        if (e->type == 1) { // Circular shooter
            shoot_interval = 150; // Shoots less frequently but a large volley
        } else { // Normal shooter
            shoot_interval = 75;
        }

        if (e->shoot_timer >= shoot_interval) {
            e->shoot_timer = 0;
            if (e->type == 1) { // MODIFICATION: Circular shot
                int bullets_to_fire_in_volley = NUM_EIGHT_WAY_DIRS; // Fire in all 8 directions
                for(int k=0; k < bullets_to_fire_in_volley; ++k) {
                    if (num_active_enemy_bullets >= MAX_ENEMY_BULLETS) break; // Stop if max bullets reached
                    for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                        if (!enemy_bullets[j].active) {
                            enemy_bullets[j].x = e->x + ENEMY_SIZE / 2 - BULLET_SIZE / 2;
                            enemy_bullets[j].y = e->y + ENEMY_SIZE / 2 - BULLET_SIZE /2; // Start from center of enemy
                            enemy_bullets[j].active = 1;
                            enemy_bullets[j].speed = 2; // Speed of circular bullets
                            enemy_bullets[j].dx = EIGHT_WAY_DX[k];
                            enemy_bullets[j].dy = EIGHT_WAY_DY[k];
                            enemy_bullets[j].shape = e->shape; // Inherit enemy shape
                            active_enemy_bullet_indices[num_active_enemy_bullets++] = j;
                            break;
                        }
                    }
                }
            } else { // Normal single shot downwards
                 if (num_active_enemy_bullets < MAX_ENEMY_BULLETS) {
                    for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                        if (!enemy_bullets[j].active) {
                            enemy_bullets[j].x = e->x + ENEMY_SIZE / 2 - BULLET_SIZE / 2;
                            enemy_bullets[j].y = e->y + ENEMY_SIZE;
                            enemy_bullets[j].active = 1;
                            enemy_bullets[j].speed = 2; // Speed of normal bullets
                            enemy_bullets[j].dx = 0;
                            enemy_bullets[j].dy = 1;
                            enemy_bullets[j].shape = e->shape;
                            active_enemy_bullet_indices[num_active_enemy_bullets++] = j;
                            break;
                        }
                    }
                }
            }
        }
    }

    for (int i = 0; i < num_active_enemy_bullets; i++) {
        int bullet_idx = active_enemy_bullet_indices[i];
        Bullet* b = &enemy_bullets[bullet_idx];
        clear_shape(b->x, b->y, BULLET_SIZE, b->shape);
        b->x += b->dx * b->speed;
        b->y += b->dy * b->speed;
        if (b->x < -BULLET_SIZE || b->x > SCREEN_WIDTH || b->y < -BULLET_SIZE || b->y > SCREEN_HEIGHT + 10) {
            b->active = 0;
        }
        if (!b->active) {
            active_enemy_bullet_indices[i] = active_enemy_bullet_indices[--num_active_enemy_bullets];
            i--;
        } else {
            draw_shape(b->x, b->y, BULLET_SIZE, b->shape, YELLOW);
        }
    }
}

void check_collisions_optimized(void) {
    for (int i = 0; i < num_active_player_bullets; i++) {
        int pb_idx = active_player_bullet_indices[i];
        Bullet* pb = &player_bullets[pb_idx];
        if (!pb->active) continue;
        for (int j = 0; j < num_active_enemies; j++) {
            int enemy_idx = active_enemy_indices[j];
            Enemy* e = &enemies[enemy_idx];
            if (!e->active) continue;
            if (pb->x < e->x + ENEMY_SIZE && pb->x + BULLET_SIZE > e->x &&
                pb->y < e->y + ENEMY_SIZE && pb->y + BULLET_SIZE > e->y) {
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
    for (int i = 0; i < num_active_tracking_bullets; i++) {
        int tb_idx = active_tracking_bullet_indices[i];
        TrackingBullet* tb = &tracking_bullets[tb_idx];
        if (!tb->active) continue;
        for (int j = 0; j < num_active_enemies; j++) {
            int enemy_idx = active_enemy_indices[j];
            Enemy* e = &enemies[enemy_idx];
            if (!e->active) continue;
            if (tb->x < e->x + ENEMY_SIZE && tb->x + TRACKING_BULLET_SIZE > e->x &&
                tb->y < e->y + ENEMY_SIZE && tb->y + TRACKING_BULLET_SIZE > e->y) {
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
    for (int i = 0; i < num_active_enemy_bullets; i++) {
        int eb_idx = active_enemy_bullet_indices[i];
        Bullet* eb = &enemy_bullets[eb_idx];
        if (!eb->active) continue;
        if (eb->x < player.x + PLAYER_SIZE && eb->x + BULLET_SIZE > player.x &&
            eb->y < player.y + PLAYER_SIZE && eb->y + BULLET_SIZE > player.y) {
            clear_shape(eb->x, eb->y, BULLET_SIZE, eb->shape);
            eb->active = 0;
        }
    }
}

void game_loop(int difficulty) {
    init_game();
    LCD_Clear(BLACK);

    while (1) {
        frame_counter++;
        update_fps_counter();
        update_entity_counter_optimized();

        // Player old position is handled inside update_player now
        update_player();
        check_collisions_optimized();
        update_player_bullets_optimized();
        update_tracking_bullets();
        update_enemies();
        update_enemy_bullets_optimized();

        // MODIFICATION: Always try to draw performance counters, assuming physical LCD is wider.
        // The function draw_performance_counters itself has a check for available space now.
        // Ensure LCD_W in your lcd_hal.c/lcd.h is the true physical width (e.g., 160).
        draw_performance_counters();

        delay_1ms(1);
    }
}

int main(void) {
  IO_init();
  // Make sure LCD_W is defined in your LCD library (e.g. in lcd.h)
  // and represents the PHYSICAL width of your LCD (e.g. 160)
  // for the performance counters to draw correctly in the margin.
  int difficulty = select();
  game_loop(difficulty);
  return 0;
}