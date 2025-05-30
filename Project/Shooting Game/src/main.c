#include "lcd/lcd.h"
#include "utils.h" // Assuming get_timer_value() and delay_1ms() are here
#include "assembly/example.h"
#include "assembly/select.h"

// Game constants - 大幅增加数量
#define SCREEN_WIDTH 120
#define SCREEN_HEIGHT 80
#define PLAYER_SIZE 4
#define ENEMY_SIZE 3
#define BULLET_SIZE 2
#define MAX_BULLETS 300         // 增加到300个玩家子弹
#define MAX_ENEMY_BULLETS 500   // 增加到500个敌人子弹 (Note: original comment said 300)
#define MAX_ENEMIES 200

// FPS and entity counting constants
#define FPS_SAMPLE_FRAMES 8     // Sample over 8 frames (e.g., ~0.133s at 60fps)
// #define ENTITY_UPDATE_INTERVAL 3 // No longer used directly for displayed_entities averaging

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
    int dx, dy;  // Direction vectors
} Bullet;

// Enemy structure
typedef struct {
    int x, y;
    int active;
    int speed;
    int shoot_timer;
    int type;  // 新增：敌人类型
} Enemy;

// Game state
Player player;
Bullet player_bullets[MAX_BULLETS];
Bullet enemy_bullets[MAX_ENEMY_BULLETS];
Enemy enemies[MAX_ENEMIES];

// Random number generator state
static unsigned int rand_seed = 12345;

// Assume SystemCoreClock is defined elsewhere (e.g., system_gd32vf103.c)
// extern uint32_t SystemCoreClock;
// Assume get_timer_value() returns a uint64_t representing timer ticks
// extern uint64_t get_timer_value(void);


void Inp_init(void) {
  rcu_periph_clock_enable(RCU_GPIOA);
  rcu_periph_clock_enable(RCU_GPIOC);

  gpio_init(GPIOA, GPIO_MODE_IPD, GPIO_OSPEED_50MHZ,
            GPIO_PIN_0 | GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
  gpio_init(GPIOC, GPIO_MODE_IPD, GPIO_OSPEED_50MHZ,
            GPIO_PIN_13 | GPIO_PIN_14 | GPIO_PIN_15);
}

void IO_init(void) {
  Inp_init(); // inport init
  Lcd_Init(); // LCD init
}

// Simple random number generator
int simple_rand(void) {
    rand_seed = rand_seed * 1103515245 + 12345;
    return (rand_seed / 65536) % 32768;
}

// Performance counters and metrics
static unsigned long frame_counter = 0; // Increments each frame

// NEW variables for averaged metrics
static uint64_t frame_duration_ticks[FPS_SAMPLE_FRAMES]; // Stores duration of last N frames in timer ticks
static int current_frame_ticks_idx = 0;         // Index for frame_duration_ticks
static uint64_t last_frame_end_ticks = 0;        // To measure current frame duration

static int entity_counts_buffer[FPS_SAMPLE_FRAMES]; // Stores entity counts for averaging
static int entity_counts_idx = 0;                   // Index for entity_counts_buffer
static int entity_sum_for_avg = 0;                  // Sum of entities over the sample window

static int displayed_fps = 60; // Will be calculated
static int displayed_entities = 0; // Will be calculated

// Variables for flicker reduction on display
static int last_displayed_fps = -1;
static int last_displayed_entities = -1;


// Function to count all active game entities (player, bullets, enemies)
int count_active_entities(void) {
    int count = 1; // Player counts as 1
    
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (player_bullets[i].active) count++;
    }
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active) count++;
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) count++;
    }
    
    return count;
}

// NEW: Unified function to update FPS and Entity Count metrics with averaging
void update_performance_metrics(void) {
    uint64_t current_ticks = get_timer_value();

    // --- FPS Calculation (Moving Average) ---
    if (last_frame_end_ticks > 0) { 
        uint64_t elapsed_ticks_for_frame = current_ticks - last_frame_end_ticks;
        
        frame_duration_ticks[current_frame_ticks_idx] = elapsed_ticks_for_frame;
        current_frame_ticks_idx = (current_frame_ticks_idx + 1) % FPS_SAMPLE_FRAMES;

        uint64_t total_ticks_for_samples = 0;
        int samples_to_average = (frame_counter < FPS_SAMPLE_FRAMES) ? (frame_counter + 1) : FPS_SAMPLE_FRAMES;

        for (int i = 0; i < samples_to_average; i++) {
             // Use the actual stored values if available, otherwise estimate for initial frames if not yet filled
            if (frame_counter < FPS_SAMPLE_FRAMES && i >= frame_counter +1) {
                 total_ticks_for_samples += (SystemCoreClock / 4) / 60; // Estimate for unfilled slots initially
            } else {
                 total_ticks_for_samples += frame_duration_ticks[i];
            }
        }
        
        if (total_ticks_for_samples > 0 && samples_to_average > 0) {
            uint64_t avg_ticks_per_frame = total_ticks_for_samples / samples_to_average;
            if (avg_ticks_per_frame > 0) {
                uint64_t ticks_per_second = SystemCoreClock / 4; 
                displayed_fps = ticks_per_second / avg_ticks_per_frame;
            } else {
                displayed_fps = 99; 
            }
        } else {
            displayed_fps = (elapsed_ticks_for_frame > 0) ? (SystemCoreClock / 4) / elapsed_ticks_for_frame : 60;
        }

        if (displayed_fps > 99) displayed_fps = 99;
        if (displayed_fps < 1) displayed_fps = 1;
    }
    last_frame_end_ticks = current_ticks;

    // --- Entity Count Calculation (Moving Average) ---
    int current_total_entities = count_active_entities(); 

    entity_sum_for_avg -= entity_counts_buffer[entity_counts_idx];
    entity_counts_buffer[entity_counts_idx] = current_total_entities;
    entity_sum_for_avg += entity_counts_buffer[entity_counts_idx];
    entity_counts_idx = (entity_counts_idx + 1) % FPS_SAMPLE_FRAMES;

    int entity_samples_to_average = (frame_counter < FPS_SAMPLE_FRAMES) ? (frame_counter + 1) : FPS_SAMPLE_FRAMES;
    
    if (entity_samples_to_average > 0) {
        displayed_entities = entity_sum_for_avg / entity_samples_to_average;
    } else {
        displayed_entities = current_total_entities;
    }
    
    if (displayed_entities > 999) displayed_entities = 999;
}


// Optimized drawing function - 减少像素操作
void draw_rect_optimized(int x, int y, int width, int height, u16 color) {
    int start_x = (x < 0) ? 0 : x;
    int start_y = (y < 0) ? 0 : y;
    int end_x = (x + width > SCREEN_WIDTH) ? SCREEN_WIDTH : x + width;
    int end_y = (y + height > SCREEN_HEIGHT) ? SCREEN_HEIGHT : y + height;
    
    if (start_x >= end_x || start_y >= end_y) return;
    
    // Consider if LCD_FillRect or similar is available for better performance
    for (int j = start_y; j < end_y; j++) {
        for (int i = start_x; i < end_x; i++) {
            LCD_DrawPoint(i, j, color);
        }
    }
}

// Replace all draw_rect calls with draw_rect_optimized (already done by user)
#define draw_rect draw_rect_optimized

// Safe LCD drawing function with bounds checking
void safe_draw_pixel(int x, int y, u16 color) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        LCD_DrawPoint(x, y, color);
    }
}

// Clear a rectangle (erase)
void clear_rect(int x, int y, int width, int height) {
    draw_rect(x, y, width, height, BLACK);
}

// MODIFIED: draw_performance_counters with optimized clearing and adjusted positions
void draw_performance_counters(void) {
    int counter_area_width = 40; 
    int counter_area_x = SCREEN_WIDTH - counter_area_width; 
    // Assuming text fits. Adjust X offset and Y spacing if needed.

    if (displayed_fps != last_displayed_fps) {
        clear_rect(counter_area_x, 0, counter_area_width, 20); 
        LCD_ShowString(counter_area_x + 2, 2, (u8*)"FPS", WHITE);
        LCD_ShowNum(counter_area_x + 10, 10, displayed_fps, 2, WHITE); // Adjusted for 2 digits
        last_displayed_fps = displayed_fps;
    }
    
    if (displayed_entities != last_displayed_entities) {
        clear_rect(counter_area_x, 20, counter_area_width, 20);
        LCD_ShowString(counter_area_x + 2, 22, (u8*)"ENT", WHITE);
        LCD_ShowNum(counter_area_x + 2, 30, displayed_entities, 3, WHITE); // Adjusted for 3 digits
        last_displayed_entities = displayed_entities;
    }
}


// Initialize game objects 
void init_game(void) {
    // Initialize player
    player.x = SCREEN_WIDTH / 2;
    player.y = SCREEN_HEIGHT - 20;
    player.speed = 2;
    
    // Initialize bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
        player_bullets[i].active = 0;
    }
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        enemy_bullets[i].active = 0;
    }
    
    // Initialize enemies
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = 0;
    }
    for (int i = 0; i < 10; i++) {
        spawn_random_enemy();
    }
    
    // Initialize performance counters (NEW/MODIFIED section)
    frame_counter = 0;
    current_frame_ticks_idx = 0;
    last_frame_end_ticks = 0; // Will be set at the start of the game loop before first calculation

    entity_counts_idx = 0;
    entity_sum_for_avg = 0;

    uint64_t typical_frame_ticks = (SystemCoreClock > 0) ? (SystemCoreClock / 4) / 60 : 16666; // Approx 60 FPS or 16ms
    for (int i = 0; i < FPS_SAMPLE_FRAMES; i++) {
        frame_duration_ticks[i] = typical_frame_ticks; 
        entity_counts_buffer[i] = 0; 
    }
    
    displayed_fps = 60; 
    displayed_entities = count_active_entities(); // Get initial count
    last_displayed_fps = -1;
    last_displayed_entities = -1;
}

// Spawn a random enemy
void spawn_random_enemy(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            enemies[i].x = simple_rand() % (SCREEN_WIDTH - ENEMY_SIZE);
            enemies[i].y = -(simple_rand() % 20 + 5); 
            enemies[i].active = 1;
            enemies[i].speed = 1 + simple_rand() % 2; // Speed 1 or 2
            enemies[i].shoot_timer = simple_rand() % 60 + 20;
            enemies[i].type = simple_rand() % 4; 
            break;
        }
    }
}

// Spawn enemy formation
void spawn_enemy_formation(void) {
    int formation_size = 3 + simple_rand() % 3;  // 3-5 enemies
    int start_x = simple_rand() % (SCREEN_WIDTH - formation_size * 25);
    if (start_x < 0) start_x = 0;
    
    for (int i = 0; i < formation_size; i++) {
        for (int j = 0; j < MAX_ENEMIES; j++) {
            if (!enemies[j].active) {
                enemies[j].x = start_x + i * 25;
                enemies[j].y = -(simple_rand() % 15 + 5);
                enemies[j].active = 1;
                enemies[j].speed = 1;
                enemies[j].shoot_timer = simple_rand() % 40;
                enemies[j].type = simple_rand() % 4;
                break;
            }
        }
    }
}

// Handle player input and movement
void update_player(void) {
    static int prev_x = -1, prev_y = -1;
    static int button1_debounce = 0;
    static int button2_debounce = 0;
    // static int debounce_counter = 0; // This seemed to affect movement responsiveness too much.
                                      // Individual button debounces are usually better.

    if (prev_x != -1 && prev_y != -1) { // Check if valid previous position exists
         clear_rect(prev_x, prev_y, PLAYER_SIZE, PLAYER_SIZE);
    }
    
    // Handle movement
    if (Get_Button(JOY_LEFT) && player.x > 0) {
        player.x -= player.speed;
    }
    if (Get_Button(JOY_RIGHT) && player.x < SCREEN_WIDTH - PLAYER_SIZE) {
        player.x += player.speed;
    }
    if (Get_Button(JOY_UP) && player.y > 0) {
        player.y -= player.speed;
    }
    if (Get_Button(JOY_DOWN) && player.y < SCREEN_HEIGHT - PLAYER_SIZE) {
        player.y += player.speed;
    }

    // Handle BUTTON_1 - 高频射击
    if (Get_Button(BUTTON_1)) {
        if (button1_debounce == 0) {
            button1_debounce = 3; 
            for (int burst = 0; burst < 3; burst++) {
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (!player_bullets[i].active) {
                        player_bullets[i].x = player.x + PLAYER_SIZE / 2 + (burst -1); // Center bullets a bit
                        player_bullets[i].y = player.y - burst * 2; // Slight vertical spread for burst
                        player_bullets[i].active = 1;
                        player_bullets[i].speed = 4;
                        player_bullets[i].dx = 0;
                        player_bullets[i].dy = -1; 
                        break;
                    }
                }
            }
        }
    } else {
        button1_debounce = 0; 
    }

    // Handle BUTTON_2 - 超级扩散射击
    if (Get_Button(BUTTON_2)) {
        if (button2_debounce == 0) {
            button2_debounce = 15; 
            
            int directions[16][2] = {
                {0, -2}, {1, -2}, {2, -2}, {2, -1}, {2, 0}, {2, 1}, {2, 2}, {1, 2},
                {0, 2}, {-1, 2}, {-2, 2}, {-2, 1}, {-2, 0}, {-2, -1}, {-2, -2}, {-1, -2}
            }; // Normalized roughly, speed handles magnitude
            
            int bullets_fired_this_shot = 0;
            for (int dir_idx = 0; dir_idx < 16 && bullets_fired_this_shot < 16; dir_idx++) {
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (!player_bullets[i].active) {
                        player_bullets[i].x = player.x + PLAYER_SIZE / 2 - BULLET_SIZE / 2;
                        player_bullets[i].y = player.y + PLAYER_SIZE / 2 - BULLET_SIZE / 2;
                        player_bullets[i].active = 1;
                        player_bullets[i].speed = 2; // Slower spread bullets
                        player_bullets[i].dx = directions[dir_idx][0];
                        player_bullets[i].dy = directions[dir_idx][1];
                        bullets_fired_this_shot++;
                        break; 
                    }
                }
            }
        }
    } else {
        button2_debounce = 0;
    }
    
    if (button1_debounce > 0) button1_debounce--;
    if (button2_debounce > 0) button2_debounce--;
    
    draw_rect(player.x, player.y, PLAYER_SIZE, PLAYER_SIZE, BLUE);
    prev_x = player.x;
    prev_y = player.y;
}

// 优化的子弹更新 - 批量处理
void update_player_bullets_optimized(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!player_bullets[i].active) continue;
        
        clear_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
        
        player_bullets[i].x += player_bullets[i].dx * player_bullets[i].speed;
        player_bullets[i].y += player_bullets[i].dy * player_bullets[i].speed;
        
        if (player_bullets[i].y < -BULLET_SIZE || player_bullets[i].y > SCREEN_HEIGHT ||
            player_bullets[i].x < -BULLET_SIZE || player_bullets[i].x > SCREEN_WIDTH) {
            player_bullets[i].active = 0;
        }
    }
    
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (player_bullets[i].active) {
            u16 bullet_color = (player_bullets[i].dy == -1 && player_bullets[i].dx == 0) ? WHITE : CYAN;
            draw_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE, bullet_color);
        }
    }
}
#define update_player_bullets update_player_bullets_optimized


// Update enemies with movement and random spawning
void update_enemies(void) {
    static int spawn_timer = 0;
    static int formation_timer = 0;
    
    spawn_timer++;
    formation_timer++;
    
    if (spawn_timer >= 30) { 
        spawn_timer = 0;
        if (simple_rand() % 100 < 30) { 
            spawn_random_enemy();
        }
    }
    
    if (formation_timer >= 300) { 
        formation_timer = 0;
        if (simple_rand() % 100 < 20) { 
            spawn_enemy_formation();
        }
    }
    
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            clear_rect(enemies[i].x, enemies[i].y, ENEMY_SIZE, ENEMY_SIZE);
            
            enemies[i].y += enemies[i].speed;
            
            if (enemies[i].y > SCREEN_HEIGHT) { // Goes off bottom
                enemies[i].active = 0; // Deactivate instead of bouncing for simplicity
                                       // Or bounce: enemies[i].y = SCREEN_HEIGHT - ENEMY_SIZE; enemies[i].speed *= -1;
                continue;
            }
            if (enemies[i].y < -ENEMY_SIZE * 5 && enemies[i].speed < 0) { // Well off top if moving up
                 enemies[i].active = 0; // Deactivate
            }


            if (simple_rand() % 100 < 5) { 
                int direction = (simple_rand() % 3) - 1; 
                enemies[i].x += direction * 2;
                
                if (enemies[i].x < 0) enemies[i].x = 0;
                if (enemies[i].x > SCREEN_WIDTH - ENEMY_SIZE) enemies[i].x = SCREEN_WIDTH - ENEMY_SIZE;
            }
        }
    }
    
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            u16 enemy_color;
            switch (enemies[i].type) {
                case 0: enemy_color = RED; break;
                case 1: enemy_color = MAGENTA; break;
                case 2: enemy_color = YELLOW; break;
                case 3: enemy_color = GREEN; break; 
                default: enemy_color = RED; break;
            }
            draw_rect(enemies[i].x, enemies[i].y, ENEMY_SIZE, ENEMY_SIZE, enemy_color);
        }
    }
}

// 优化的敌人子弹系统
void update_enemy_bullets_optimized(void) {
    int active_enemy_bullets_count = 0;
    for(int i=0; i<MAX_ENEMY_BULLETS; ++i) if(enemy_bullets[i].active) active_enemy_bullets_count++;

    // Limit total active enemy bullets to prevent overwhelming screen, e.g. 100
    // This is a gameplay choice as much as performance. MAX_ENEMY_BULLETS is the hard cap.
    int max_simultaneous_enemy_bullets = 100; 


    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;
        if (active_enemy_bullets_count >= max_simultaneous_enemy_bullets && enemies[i].type != 3) continue; // Type 3 can always try to shoot

        enemies[i].shoot_timer++;
        int shoot_interval;
        switch (enemies[i].type) {
            case 0: shoot_interval = 120; break; 
            case 1: shoot_interval = 80; break;
            case 2: shoot_interval = 160; break;
            case 3: shoot_interval = 60; break; // Faster shooting type
            default: shoot_interval = 120; break;
        }
            
        if (enemies[i].shoot_timer >= shoot_interval) {
            enemies[i].shoot_timer = 0;
            
            for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                if (!enemy_bullets[j].active) {
                    enemy_bullets[j].x = enemies[i].x + ENEMY_SIZE / 2 - BULLET_SIZE / 2;
                    enemy_bullets[j].y = enemies[i].y + ENEMY_SIZE;
                    enemy_bullets[j].active = 1;
                    enemy_bullets[j].speed = 2;
                    
                    // Aim at player (simple)
                    int dx = player.x - enemy_bullets[j].x;
                    int dy = player.y - enemy_bullets[j].y;
                    // Normalize (approximately, to avoid sqrt)
                    int abs_dx = (dx > 0) ? dx : -dx;
                    int abs_dy = (dy > 0) ? dy : -dy;

                    if (abs_dx > abs_dy) {
                        enemy_bullets[j].dx = (dx > 0) ? 1 : -1;
                        enemy_bullets[j].dy = (dy * 1) / ((abs_dx == 0) ? 1 : abs_dx); // Scale dy
                        if (dy !=0 && enemy_bullets[j].dy == 0) enemy_bullets[j].dy = (dy > 0) ? 1: -1;


                    } else if (abs_dy > 0) {
                        enemy_bullets[j].dy = (dy > 0) ? 1 : -1;
                        enemy_bullets[j].dx = (dx * 1) / abs_dy; // Scale dx
                        if (dx !=0 && enemy_bullets[j].dx == 0) enemy_bullets[j].dx = (dx > 0) ? 1: -1;
                    } else { // Directly on top
                        enemy_bullets[j].dx = 0;
                        enemy_bullets[j].dy = 1;
                    }
                    // For type 3, make bullets faster or different pattern
                    if (enemies[i].type == 3) {
                         enemy_bullets[j].speed = 3;
                    } else { // Default aiming downwards if player is directly above or other issue
                        if (dx == 0 && dy == 0) {
                             enemy_bullets[j].dx = 0;
                             enemy_bullets[j].dy = 1;
                        }
                    }

                    active_enemy_bullets_count++;
                    break; 
                }
            }
        }
    }
    
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (!enemy_bullets[i].active) continue;
        
        clear_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
        
        enemy_bullets[i].x += enemy_bullets[i].dx * enemy_bullets[i].speed;
        enemy_bullets[i].y += enemy_bullets[i].dy * enemy_bullets[i].speed;
        
        if (enemy_bullets[i].y > SCREEN_HEIGHT || enemy_bullets[i].y < -BULLET_SIZE ||
            enemy_bullets[i].x > SCREEN_WIDTH || enemy_bullets[i].x < -BULLET_SIZE) {
            enemy_bullets[i].active = 0;
        }
    }
    
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active) {
            draw_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE, YELLOW);
        }
    }
}
#define update_enemy_bullets update_enemy_bullets_optimized

// 优化的碰撞检测 - 使用空间分割 (currently AABB with pre-check)
void check_collisions_optimized(void) {
    // Player bullets vs Enemies
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!player_bullets[i].active) continue;
        
        // Optional: Screen bounds check for bullet first (already done in update loop mostly)
        // if (player_bullets[i].x < 0 || player_bullets[i].x > SCREEN_WIDTH || ...) continue;
        
        for (int j = 0; j < MAX_ENEMIES; j++) {
            if (!enemies[j].active) continue;
            
            // Quick distance check (squared distance)
            // Center of bullet vs center of enemy
            int bullet_cx = player_bullets[i].x + BULLET_SIZE / 2;
            int bullet_cy = player_bullets[i].y + BULLET_SIZE / 2;
            int enemy_cx = enemies[j].x + ENEMY_SIZE / 2;
            int enemy_cy = enemies[j].y + ENEMY_SIZE / 2;

            int dx = bullet_cx - enemy_cx;
            int dy = bullet_cy - enemy_cy;
            
            // Effective radius: (BULLET_SIZE/2 + ENEMY_SIZE/2) = (1+1.5) = 2.5. Squared = 6.25
            // Let's use a slightly larger combined radius for the coarse check
            // Sum of half-widths/heights: (BULLET_SIZE + ENEMY_SIZE)/2 = (2+3)/2 = 2.5
            // A slightly more generous check for square bounding boxes:
            int combined_half_size = (BULLET_SIZE + ENEMY_SIZE) / 2 + 1; // e.g. 2+3 -> 5/2=2 + 1 = 3
                                                                     // (4+3)/2 = 3 + 1 = 4

            if (dx * dx + dy * dy < combined_half_size * combined_half_size * 2) { // (radius_sum)^2 for circle, for squares more complex
                                                                                // This threshold needs tuning, using the user's 25
                if (dx * dx + dy * dy < 25) { // Original threshold 25 (radius 5)
                    // Precise AABB collision
                    if (player_bullets[i].x < enemies[j].x + ENEMY_SIZE &&
                        player_bullets[i].x + BULLET_SIZE > enemies[j].x &&
                        player_bullets[i].y < enemies[j].y + ENEMY_SIZE &&
                        player_bullets[i].y + BULLET_SIZE > enemies[j].y) {
                        
                        // clear_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE); // Cleared by update loop
                        // clear_rect(enemies[j].x, enemies[j].y, ENEMY_SIZE, ENEMY_SIZE); // Cleared by update loop
                        
                        player_bullets[i].active = 0;
                        enemies[j].active = 0; 
                        
                        // Optional: spawn a new enemy as replacement sometimes
                        // if (simple_rand() % 100 < 10) { spawn_random_enemy(); }
                        break; 
                    }
                }
            }
        }
    }
    
    // Enemy bullets vs Player
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (!enemy_bullets[i].active) continue;
        
        int bullet_cx = enemy_bullets[i].x + BULLET_SIZE / 2;
        int bullet_cy = enemy_bullets[i].y + BULLET_SIZE / 2;
        int player_cx = player.x + PLAYER_SIZE / 2;
        int player_cy = player.y + PLAYER_SIZE / 2;

        int dx = bullet_cx - player_cx;
        int dy = bullet_cy - player_cy;
        
        // Combined half_size: (BULLET_SIZE + PLAYER_SIZE)/2 = (2+4)/2 = 3
        // Original threshold 16 (radius 4)
        if (dx * dx + dy * dy < 25) { // A slightly more generous radius than strict minimum
            if (enemy_bullets[i].x < player.x + PLAYER_SIZE &&
                enemy_bullets[i].x + BULLET_SIZE > player.x &&
                enemy_bullets[i].y < player.y + PLAYER_SIZE &&
                enemy_bullets[i].y + BULLET_SIZE > player.y) {
                
                // clear_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE); // Cleared by update loop
                enemy_bullets[i].active = 0;
                
                // TODO: Player hit logic (e.g., lose life, game over)
                // For now, just destroy bullet. Player is invincible.
            }
        }
    }
}
#define check_collisions check_collisions_optimized

// This function is defined but not used. count_active_entities is used instead.
/*
int count_active_objects(void) {
    int bullet_count = 0;
    int enemy_count = 0;
    
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (player_bullets[i].active) bullet_count++;
    }
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active) bullet_count++;
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) enemy_count++;
    }
    return bullet_count + enemy_count;
}
*/

// Main game loop
void game_loop(int difficulty) {
    init_game(); 
    LCD_Clear(BLACK);
    
    // Initialize last_frame_end_ticks for the first frame's duration calculation
    // This ensures the first FPS calculation is based on some elapsed time.
    last_frame_end_ticks = get_timer_value(); 

    while (1) { 
        frame_counter++;
        
        update_performance_metrics(); 
        
        update_player();
        // It's often better to update positions THEN check collisions
        // then handle results of collisions, then draw.
        // Current order: Player moves -> Collisions -> Bullets move -> Enemies move -> Enemy Bullets move.
        // This means collisions are checked with player's new position, but bullets/enemies old positions from last frame's update.
        // For higher accuracy, all updates could happen, then all collisions.
        // For this game, the current order might be acceptable.
        
        update_player_bullets(); // Includes drawing
        update_enemies();        // Includes drawing
        update_enemy_bullets();  // Includes drawing
        
        check_collisions();      // Process collisions based on positions before some entities were redrawn
                                 // This might lead to bullets passing through if not careful.
                                 // Better: Update all logic -> Check all collisions -> Resolve collisions -> Draw all
                                 // However, current code structure intermingles draw and update logic.

        draw_performance_counters();
        
        delay_1ms(16); // Target ~60 FPS. If logic takes >16ms, FPS will be lower.
    }
}

int main(void) {
  IO_init();
  
  // Ensure SystemCoreClock is initialized before use by performance counters or delays
  // SystemInit(); // Or similar, often called before main for microcontrollers

  int difficulty = 0; // Default difficulty
  // difficulty = select(); // Call assembly function - assuming it returns a value
  
  game_loop(difficulty);
  
  return 0;
}