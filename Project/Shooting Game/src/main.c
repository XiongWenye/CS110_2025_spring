#include "lcd/lcd.h"
#include "utils.h"
#include "assembly/example.h"
#include "assembly/select.h"

// Game constants
#define SCREEN_WIDTH 120
#define SCREEN_HEIGHT 80
// OPTIMIZATION: Adjusted entity sizes, especially bullets
#define PLAYER_SIZE 6
#define ENEMY_SIZE 4
#define BULLET_SIZE 2         // OPTIMIZATION: Smaller bullets (2x2)
#define MAX_BULLETS 300
#define MAX_ENEMY_BULLETS 300 // 增加敌人子弹上限
#define MAX_ENEMIES 30        // 减少敌人数量从100到30

// Tracking bullets
#define MAX_TRACKING_BULLETS 50
#define TRACKING_BULLET_SIZE 3    // OPTIMIZATION: Can be 2x2 or 3x3 for visibility
#define TRACKING_BULLET_SPEED 2

// Entity shapes
#define SHAPE_HOLLOW_CIRCLE 0
#define SHAPE_HOLLOW_SQUARE 1
#define SHAPE_SOLID_SQUARE 2

// FPS and entity counting
#define FPS_SAMPLE_FRAMES 8
#define ENTITY_UPDATE_INTERVAL 20// Update entity counter every frame now that it's cheap

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
    int shape; // Kept for flexibility, though player bullets might always be solid
} Bullet;

// Enemy structure
typedef struct {
    int x, y;
    int active;
    int speed;
    int shoot_timer;
    int type;
    int shape;
} Enemy;

// Tracking Bullet structure
typedef struct {
    int x, y;
    int active;
    int target_enemy_index; // Stores the actual index in the main enemies array
    int lifetime;
} TrackingBullet;

// Game state
Player player;

Bullet player_bullets[MAX_BULLETS];
Bullet enemy_bullets[MAX_ENEMY_BULLETS];
Enemy enemies[MAX_ENEMIES];
TrackingBullet tracking_bullets[MAX_TRACKING_BULLETS];

// OPTIMIZATION: Active list management
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
static unsigned long frame_times[FPS_SAMPLE_FRAMES]; // Original FPS calculation, might be superseded by new one
static int frame_index = 0;                         // Original FPS calculation
static int displayed_fps = 60;
static int displayed_entities = 0;
static int entity_update_counter = 0;
static unsigned long frame_counter = 0;
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
  Lcd_Init(); // LCD init
  // IMPORTANT: Assumes Lcd_Init() sets up LCD_W and LCD_H correctly if used by LCD_Clear.
  // IMPORTANT: Assumes LCD_Clear in lcd.c is now optimized to use LCD_Fill.
}

int simple_rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed / 65536) % 32768;
}

// OPTIMIZATION: Uses LCD_Fill for speed.
void draw_rect_optimized(int x, int y, int width, int height, u16 color) {
    int x0 = x;
    int y0 = y;
    int x1 = x + width - 1;  // LCD_Fill uses inclusive coordinates
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

// OPTIMIZATION: Helper for drawing hollow rectangles efficiently using LCD_Fill
static void LCD_DrawHollowRect(u16 x1, u16 y1, u16 x2, u16 y2, u16 color) {
    // Ensure x1<=x2 and y1<=y2
    if (x1 > x2) { u16 temp = x1; x1 = x2; x2 = temp; }
    if (y1 > y2) { u16 temp = y1; y1 = y2; y2 = temp; }

    LCD_Fill(x1, y1, x2, y1, color); // Top line
    LCD_Fill(x1, y2, x2, y2, color); // Bottom line
    if (y1 + 1 <= y2 -1) { // Avoid overdraw if height is too small
        LCD_Fill(x1, y1 + 1, x1, y2 - 1, color); // Left line
        LCD_Fill(x2, y1 + 1, x2, y2 - 1, color); // Right line
    }
}

void clear_shape(int x, int y, int size, int shape) {
    switch(shape) {
        case SHAPE_HOLLOW_CIRCLE:
            LCD_DrawCircle(x + size/2, y + size/2, size/2, BLACK); // Slow, consider alternatives if bottleneck
            break;
        case SHAPE_HOLLOW_SQUARE:
            LCD_DrawHollowRect(x, y, x + size - 1, y + size - 1, BLACK);
            break;
        case SHAPE_SOLID_SQUARE:
            draw_rect_optimized(x, y, size, size, BLACK); // Fast
            break;
    }
}

void draw_shape(int x, int y, int size, int shape, u16 color) {
    switch(shape) {
        case SHAPE_HOLLOW_CIRCLE:
            LCD_DrawCircle(x + size/2, y + size/2, size/2, color); // Slow, consider alternatives if bottleneck
            break;
        case SHAPE_HOLLOW_SQUARE:
            LCD_DrawHollowRect(x, y, x + size - 1, y + size - 1, color);
            break;
        case SHAPE_SOLID_SQUARE:
            draw_rect_optimized(x, y, size, size, color); // Fast
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
    if (fps_frame_count >= 30) { // Update display about every 0.5s at 60fps
        uint64_t elapsed_time = current_time - fps_measurement_start_time;
        if (elapsed_time > 0) {
            uint64_t ticks_per_second = SystemCoreClock / 4; // Assuming SysTick based timer
            displayed_fps = (fps_frame_count * ticks_per_second) / elapsed_time;
            if (displayed_fps > 99) displayed_fps = 99;
            if (displayed_fps < 0) displayed_fps = 0; // Can be 0 if stalls
        }
        fps_measurement_start_time = current_time;
        fps_frame_count = 0;
    }
}

// OPTIMIZATION: Uses num_active_X counters
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
    if (displayed_fps != last_displayed_fps) {
        clear_rect(SCREEN_WIDTH + 5, 0, 35, 25); // Area for FPS
        LCD_ShowString(SCREEN_WIDTH + 5 , 5, (u8*)"FPS", WHITE);
        LCD_ShowNum(SCREEN_WIDTH + 5, 20, displayed_fps, 2, WHITE);
        last_displayed_fps = displayed_fps;
    }
    if (displayed_entities != last_displayed_entities) {
        clear_rect(SCREEN_WIDTH + 5, 25, 35, 25); // Area for Entities
        LCD_ShowString(SCREEN_WIDTH + 5, 35, (u8*)"ENT", WHITE);
        LCD_ShowNum(SCREEN_WIDTH +5 , 50, displayed_entities, 3, WHITE);
        last_displayed_entities = displayed_entities;
    }
}

int find_nearest_enemy(int x, int y) {
    int nearest_global_idx = -1;
    int min_distance_squared = 9999999;

    for (int i = 0; i < num_active_enemies; i++) {
        int enemy_idx = active_enemy_indices[i];
        // enemies[enemy_idx] is the active enemy
        int dx = enemies[enemy_idx].x - x;
        int dy = enemies[enemy_idx].y - y;
        int distance_squared = dx * dx + dy * dy;
        if (distance_squared < min_distance_squared) {
            min_distance_squared = distance_squared;
            nearest_global_idx = enemy_idx; // Store the index from the main enemies array
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
            tracking_bullets[i].lifetime = 300; // ~5 seconds at 60FPS

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
            enemies[i].y = -(simple_rand() % 20 + 5); // Spawn above screen
            enemies[i].active = 1;
            enemies[i].speed = 1 + simple_rand() % 2; // Speed 1-2
            enemies[i].shoot_timer = simple_rand() % 30 + 15; // 减少射击间隔
            enemies[i].type = simple_rand() % 3;
            enemies[i].shape = simple_rand() % 3;
            
            active_enemy_indices[num_active_enemies++] = i;
            break;
        }
    }
}

void init_game(void) {
    player.x = SCREEN_WIDTH / 2;
    player.y = SCREEN_HEIGHT - PLAYER_SIZE - 2; // Ensure player is fully on screen
    player.speed = 2;

    num_active_player_bullets = 0;
    for (int i = 0; i < MAX_BULLETS; i++) player_bullets[i].active = 0;

    num_active_enemy_bullets = 0;
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) enemy_bullets[i].active = 0;
    
    num_active_tracking_bullets = 0;
    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) tracking_bullets[i].active = 0;

    num_active_enemies = 0;
    for (int i = 0; i < MAX_ENEMIES; i++) enemies[i].active = 0;

    for (int i = 0; i < 5; i++) spawn_random_enemy(); // 减少初始敌人数量

    frame_counter = 0;
    fps_measurement_start_time = 0; // Reset FPS counter
    fps_frame_count = 0;
}

void spawn_enemy_formation(void) {
    // Simplified: just spawn a few random enemies
    for (int i = 0; i < 3 + simple_rand() % 3; i++) {
        spawn_random_enemy();
    }
}

void update_player(void) {
    static int prev_x = -1, prev_y = -1;
    static int button1_debounce = 0;
    static int button2_debounce = 0;
    static int center_button_debounce = 0;
    // Note: The original debounce_counter for all inputs might be too restrictive.
    // Consider removing it or making its effect minimal if input feels laggy.

    if (prev_x != -1) { // Clear previous player position if valid
         clear_rect(prev_x, prev_y, PLAYER_SIZE, PLAYER_SIZE);
    }
    prev_x = player.x; // Store current position for next frame's clear
    prev_y = player.y;

    if (Get_Button(JOY_LEFT) && player.x > 0) player.x -= player.speed;
    if (Get_Button(JOY_RIGHT) && player.x < SCREEN_WIDTH - PLAYER_SIZE) player.x += player.speed;
    if (Get_Button(JOY_UP) && player.y > 0) player.y -= player.speed;
    if (Get_Button(JOY_DOWN) && player.y < SCREEN_HEIGHT - PLAYER_SIZE) player.y += player.speed;

    if (Get_Button(BUTTON_1)) {
        if (button1_debounce == 0) {
            button1_debounce = 5; // Cooldown for 3-burst shot
            for (int burst = 0; burst < 3; burst++) {
                if (num_active_player_bullets < MAX_BULLETS) {
                    for (int i = 0; i < MAX_BULLETS; i++) { // Find inactive slot
                        if (!player_bullets[i].active) {
                            player_bullets[i].x = player.x + PLAYER_SIZE / 2 - BULLET_SIZE / 2 + (burst -1) * (BULLET_SIZE + 1) ; // Slight spread
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
            button2_debounce = 20; // Cooldown for spread shot
            int directions[8][2] = {{0, -1}, {1, -1}, {1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}}; // 8 directions
            for (int dir = 0; dir < 8; dir++) {
                if (num_active_player_bullets < MAX_BULLETS) {
                     for (int i = 0; i < MAX_BULLETS; i++) { // Find inactive slot
                        if (!player_bullets[i].active) {
                            player_bullets[i].x = player.x + PLAYER_SIZE / 2 - BULLET_SIZE / 2;
                            player_bullets[i].y = player.y;
                            player_bullets[i].active = 1;
                            player_bullets[i].speed = 3;
                            player_bullets[i].dx = directions[dir][0];
                            player_bullets[i].dy = directions[dir][1];
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
            b->active = 0; // Mark as inactive
        }

        if (!b->active) { // Remove from active list
            active_player_bullet_indices[i] = active_player_bullet_indices[--num_active_player_bullets];
            i--; // Re-process the swapped element in the next iteration
        } else {
            // OPTIMIZATION: Player bullets are always solid squares and small
            u16 bullet_color = (b->dx == 0 && b->dy == -1) ? WHITE : CYAN; // Straight vs Spread
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
            // Check if current target is still valid (active and within bounds)
            if (target_global_idx < 0 || target_global_idx >= MAX_ENEMIES || !enemies[target_global_idx].active) {
                tb->target_enemy_index = find_nearest_enemy(tb->x, tb->y); // Find new target
                target_global_idx = tb->target_enemy_index;
            }

            if (target_global_idx != -1) { // If target exists
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
            } else { // No target, fly straight up
                tb->y -= TRACKING_BULLET_SPEED;
            }
        }
        
        // Screen bounds check
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
    static int formation_timer = 0; // Currently not used for complex formations

    spawn_timer++;
    if (spawn_timer >= 40) { // 降低敌人生成频率
        spawn_timer = 0;
        if (simple_rand() % 100 < 25) spawn_random_enemy(); // 降低生成概率
    }
    // formation_timer can be used for more complex spawning patterns later

    for (int i = 0; i < num_active_enemies; i++) {
        int enemy_idx = active_enemy_indices[i];
        Enemy* e = &enemies[enemy_idx];

        clear_shape(e->x, e->y, ENEMY_SIZE, e->shape);

        e->y += e->speed;

        // Simple boundary bounce (can be improved)
        if (e->y > SCREEN_HEIGHT - ENEMY_SIZE) {
            e->y = SCREEN_HEIGHT - ENEMY_SIZE;
            e->speed *= -1;
        } else if (e->y < 0) {
            e->y = 0;
            e->speed *= -1;
        }
        

        if (!e->active) { // Should not happen here, but as a safeguard
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
    // Enemy shooting logic (moved from update_enemies)
    for (int i = 0; i < num_active_enemies; i++) {
        int enemy_idx = active_enemy_indices[i];
        Enemy* e = &enemies[enemy_idx];
        if (!e->active) continue; // Should not happen if active list is correct

        e->shoot_timer++;
        int shoot_interval;
        int bullet_count;
        switch (e->type) {
            case 0: 
                shoot_interval = 40; // 更频繁射击
                bullet_count = 3; // 射击3发子弹
                break; 
            case 1: 
                shoot_interval = 35; // 更频繁射击
                bullet_count = 4; // 射击4发子弹
                break;
            case 2: 
                shoot_interval = 50; // 更频繁射击
                bullet_count = 8; // 向8个方向射击
                break;
            default: 
                shoot_interval = 40; 
                bullet_count = 3;
                break;
        }

        if (e->shoot_timer >= shoot_interval) {
            e->shoot_timer = 0;
            
            if (e->type == 2) {
                // Type 2: 向8个方向发射子弹
                int directions[8][2] = {{0, 1}, {1, 1}, {1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}};
                for (int dir = 0; dir < 8 && num_active_enemy_bullets < MAX_ENEMY_BULLETS; dir++) {
                    for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                        if (!enemy_bullets[j].active) {
                            enemy_bullets[j].x = e->x + ENEMY_SIZE / 2 - BULLET_SIZE / 2;
                            enemy_bullets[j].y = e->y + ENEMY_SIZE / 2 - BULLET_SIZE / 2;
                            enemy_bullets[j].active = 1;
                            enemy_bullets[j].speed = 2;
                            enemy_bullets[j].dx = directions[dir][0];
                            enemy_bullets[j].dy = directions[dir][1];
                            enemy_bullets[j].shape = e->shape;
                            active_enemy_bullet_indices[num_active_enemy_bullets++] = j;
                            break;
                        }
                    }
                }
            } else {
                // Type 0 和 1: 向下方发射多发子弹
                for (int bullet = 0; bullet < bullet_count && num_active_enemy_bullets < MAX_ENEMY_BULLETS; bullet++) {
                    for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                        if (!enemy_bullets[j].active) {
                            enemy_bullets[j].x = e->x + ENEMY_SIZE / 2 - BULLET_SIZE / 2 + (bullet - bullet_count/2) * (BULLET_SIZE + 1);
                            enemy_bullets[j].y = e->y + ENEMY_SIZE;
                            enemy_bullets[j].active = 1;
                            enemy_bullets[j].speed = 2;
                            enemy_bullets[j].dx = 0;
                            enemy_bullets[j].dy = 1; // Move down
                            enemy_bullets[j].shape = e->shape;
                            active_enemy_bullet_indices[num_active_enemy_bullets++] = j;
                            break;
                        }
                    }
                }
            }
        }
    }

    // Update and draw active enemy bullets
    for (int i = 0; i < num_active_enemy_bullets; i++) {
        int bullet_idx = active_enemy_bullet_indices[i];
        Bullet* b = &enemy_bullets[bullet_idx];

        clear_shape(b->x, b->y, BULLET_SIZE, b->shape); // Using BULLET_SIZE

        b->x += b->dx * b->speed;
        b->y += b->dy * b->speed;

        if (b->x < BULLET_SIZE || b->x > SCREEN_WIDTH - BULLET_SIZE || b->y < BULLET_SIZE || b->y > SCREEN_HEIGHT - BULLET_SIZE) {
            b->active = 0;
        }

        if (!b->active) {
            active_enemy_bullet_indices[i] = active_enemy_bullet_indices[--num_active_enemy_bullets];
            i--; 
        } else {
            // Enemy bullets are yellow and use their shape (or BULLET_SIZE if shape is solid)
            if (b->shape == SHAPE_SOLID_SQUARE) {
                 draw_rect_optimized(b->x, b->y, BULLET_SIZE, BULLET_SIZE, YELLOW);
            } else {
                 draw_shape(b->x, b->y, BULLET_SIZE, b->shape, YELLOW); // Draw with BULLET_SIZE
            }
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

            // AABB collision: Player Bullet (size BULLET_SIZE) vs Enemy (size ENEMY_SIZE)
            if (pb->x < e->x + ENEMY_SIZE &&
                pb->x + BULLET_SIZE > e->x &&
                pb->y < e->y + ENEMY_SIZE &&
                pb->y + BULLET_SIZE > e->y) {
                
                clear_rect(pb->x, pb->y, BULLET_SIZE, BULLET_SIZE); // Clear bullet
                clear_shape(e->x, e->y, ENEMY_SIZE, e->shape);   // Clear enemy
                
                pb->active = 0; // Will be removed by its update loop
                e->active = 0;  // Will be removed by its update loop (or needs explicit removal here)
                                // For enemies, explicit removal from active list here is safer if no separate update path for inactive ones
                active_enemy_indices[j] = active_enemy_indices[--num_active_enemies];
                j--; // recheck swapped enemy

                // OPTIONAL: spawn new enemy on kill, perhaps less frequently
                // if(simple_rand() % 100 < 5) spawn_random_enemy(); 
                break; // Player bullet is destroyed, no need to check against other enemies
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

            // AABB: Tracking Bullet (TRACKING_BULLET_SIZE) vs Enemy (ENEMY_SIZE)
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
    
    // Enemy bullets vs Player
    for (int i = 0; i < num_active_enemy_bullets; i++) {
        int eb_idx = active_enemy_bullet_indices[i];
        Bullet* eb = &enemy_bullets[eb_idx];
        if (!eb->active) continue;

        // AABB: Enemy Bullet (BULLET_SIZE) vs Player (PLAYER_SIZE)
        if (eb->x < player.x + PLAYER_SIZE &&
            eb->x + BULLET_SIZE > player.x && // Assuming enemy bullet shape is drawn with BULLET_SIZE
            eb->y < player.y + PLAYER_SIZE &&
            eb->y + BULLET_SIZE > player.y) {
            
            clear_shape(eb->x, eb->y, BULLET_SIZE, eb->shape); // Clear bullet based on its shape and BULLET_SIZE
            eb->active = 0; 
            // Player Hit Logic (e.g., lose life, game over) - not implemented here
            // For now, player is invincible, but bullet is destroyed
        }
    }
}

void game_loop(int difficulty) {
    init_game();
    LCD_Clear(BLACK); // Assumes this is now fast

    // Pre-draw static UI elements if any (outside game area)
    // Example: LCD_ShowString(SCREEN_WIDTH + 5, 0, (u8*)"STAT", WHITE); // Header for stats

    while (1) {
        frame_counter++;

        update_fps_counter();
        update_entity_counter_optimized(); // Now uses quick counts

        // Store player's old position before any updates for clean clearing
        int old_player_x = player.x;
        int old_player_y = player.y;

        update_player(); // Handles input, movement, and player firing, also draws player

        // Collisions before updates that might remove entities
        check_collisions_optimized();

        update_player_bullets_optimized(); // Clears, moves, draws, and manages active list
        update_tracking_bullets();         // Clears, moves, draws, and manages active list
        update_enemies();                  // Clears, moves, draws, and manages active list
        update_enemy_bullets_optimized();  // Handles enemy firing, clears, moves, draws, manages active list
        
        // Draw performance counters last so they are on top (if they are outside game area)
        // If they are inside game area, they might be overwritten.
        // The current draw_performance_counters clears its own background.
        draw_performance_counters();

        // OPTIMIZATION: Adjust delay. A small delay helps prevent 100% CPU usage
        // and can make the game feel smoother if FPS is very high.
        // If your target is 60 FPS (16ms/frame), and your loop takes ~10ms, delay_1ms(1) is fine.
        // If your loop takes >16ms, any delay makes it worse.
        // For development, you might remove delay to see max possible FPS.
        delay_1ms(16); // Minimal delay
    }
}

int main(void) {
  IO_init();
  int difficulty = select();
  game_loop(difficulty);
  return 0;
}