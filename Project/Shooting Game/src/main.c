#include "lcd/lcd.h"
#include "utils.h"
#include "assembly/example.h"
#include "assembly/select.h"

// Game constants - 大幅增加数量
#define SCREEN_WIDTH 100
#define SCREEN_HEIGHT 80
#define PLAYER_SIZE 4
#define ENEMY_SIZE 3
#define BULLET_SIZE 2
#define MAX_BULLETS 300         // 增加到300个玩家子弹
#define MAX_ENEMY_BULLETS 500   // 增加到300个敌人子弹
#define MAX_ENEMIES 200         

// FPS and entity counting constants
#define FPS_SAMPLE_FRAMES 8     // Sample over 8 frames (0.133s at 60fps)
#define ENTITY_UPDATE_INTERVAL 3  // Update entity counter every 4 frames

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

// Performance counters
static unsigned long frame_times[FPS_SAMPLE_FRAMES];
static int frame_index = 0;
static int displayed_fps = 60;
static int displayed_entities = 0;
static int entity_update_counter = 0;
static unsigned long frame_counter = 0;
// 添加这些变量来避免频闪
static int last_displayed_fps = -1;
static int last_displayed_entities = -1;

// Random number generator state
static unsigned int rand_seed = 12345;

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

// Get current time in milliseconds (approximation based on frame counter)
unsigned long get_time_ms(void) {
    return frame_counter * 16; // Assume 16ms per frame
}





// 修改FPS计数器函数 - 使用真实时间测量
void update_fps_counter(void) {
    static unsigned long last_frame_time = 0;
    static unsigned long fps_update_timer = 0;
    
    // 简单的时间测量（这里需要根据你的硬件调整）
    unsigned long current_time = frame_counter;
    
    fps_update_timer++;
    
    // 每30帧更新一次FPS显示（减少闪烁）
    if (fps_update_timer >= 30) {
        fps_update_timer = 0;
        
        // 计算实际FPS基于30帧的时间
        unsigned long elapsed_frames = current_time - last_frame_time;
        if (elapsed_frames > 0) {
            // 假设每帧约16ms，30帧 = 480ms
            displayed_fps = (30 * 1000) / (elapsed_frames * 16);
            if (displayed_fps > 99) displayed_fps = 99;
            if (displayed_fps < 1) displayed_fps = 1;
        }
        last_frame_time = current_time;
    }
}

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

// 修改实体计数器函数 - 减少更新频率
void update_entity_counter(void) {
    entity_update_counter++;
    if (entity_update_counter >= 10) {  // 每10帧更新一次，进一步减少闪烁
        entity_update_counter = 0;
        displayed_entities = count_active_entities();
        if (displayed_entities > 999) displayed_entities = 999;
    }
}

// 修改绘制函数 - 只在数值变化时重绘
void draw_performance_counters(void) {
    // 只在FPS变化时重绘FPS区域
    if (displayed_fps != last_displayed_fps) {
        // 清除FPS区域
        for (int x = SCREEN_WIDTH - 40; x < SCREEN_WIDTH; x++) {
            for (int y = 0; y < 25; y++) {
                LCD_DrawPoint(x, y, BLACK);
            }
        }
        
        // 重绘FPS
        LCD_ShowString(SCREEN_WIDTH + 5 , 5, (u8*)"FPS", WHITE);
        LCD_ShowNum(SCREEN_WIDTH + 5, 20, displayed_fps, 2, WHITE);
        
        last_displayed_fps = displayed_fps;
    }
    
    // 只在实体数变化时重绘实体区域
    if (displayed_entities != last_displayed_entities) {
        // 清除实体区域
        for (int x = SCREEN_WIDTH - 40; x < SCREEN_WIDTH; x++) {
            for (int y = 25; y < 50; y++) {
                LCD_DrawPoint(x, y, BLACK);
            }
        }
        
        // 重绘实体计数
        LCD_ShowString(SCREEN_WIDTH + 5, 30, (u8*)"ENT", WHITE);
        LCD_ShowNum(SCREEN_WIDTH +5 , 45, displayed_entities, 3, WHITE);
        
        last_displayed_entities = displayed_entities;
    }
}

// Safe LCD drawing function with bounds checking
void safe_draw_pixel(int x, int y, u16 color) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        LCD_DrawPoint(x, y, color);
    }
}

// Draw a filled rectangle with bounds checking
void draw_rect(int x, int y, int width, int height, u16 color) {
    for (int i = 0; i < width; i++) {
        for (int j = 0; j < height; j++) {
            safe_draw_pixel(x + i, y + j, color);
        }
    }
}

// Clear a rectangle (erase)
void clear_rect(int x, int y, int width, int height) {
    draw_rect(x, y, width, height, BLACK);
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
        player_bullets[i].dx = 0;
        player_bullets[i].dy = 0;
    }
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        enemy_bullets[i].active = 0;
        enemy_bullets[i].dx = 0;
        enemy_bullets[i].dy = 0;
    }
    
    // Initialize enemies - 初始生成一些敌人
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = 0;  // 开始时都不活跃，随机生成
    }
    
    // 生成初始敌人
    for (int i = 0; i < 10; i++) {  // 初始生成10个敌人
        spawn_random_enemy();
    }
    
    // Initialize performance counters
    for (int i = 0; i < FPS_SAMPLE_FRAMES; i++) {
        frame_times[i] = 16; // Assume 16ms initially
    }
    frame_counter = 0;
}

// Spawn a random enemy
void spawn_random_enemy(void) {
    // Find an inactive enemy slot
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            // Random spawn position at top of screen
            enemies[i].x = simple_rand() % (SCREEN_WIDTH - ENEMY_SIZE);
            enemies[i].y = (simple_rand() % 20 + 5);  // Spawn above screen
            enemies[i].active = 1;
            enemies[i].speed = -1 + simple_rand() % 3;  // Random speed 1-3
            enemies[i].shoot_timer = simple_rand() % 60;  // Random initial timer
            enemies[i].type = simple_rand() % 4;  // 四种类型的敌人 (0-3)
            break;
        }
    }
}

// Spawn enemy formation
void spawn_enemy_formation(void) {
    // Spawn a formation of 3-5 enemies
    int formation_size = 10 + simple_rand() % 3;  // 3-5 enemies
    int start_x = simple_rand() % (SCREEN_WIDTH - formation_size * 25);
    
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
    
    // 添加静态变量来防抖动
    static int button1_debounce = 0;
    static int button2_debounce = 0;
    static int debounce_counter = 0;
    
    // 防抖动计数器
    if (debounce_counter > 0) {
        debounce_counter--;
    }
    
    // Clear previous player position
    if (prev_x >= 0 && prev_y >= 0) {
        clear_rect(prev_x, prev_y, PLAYER_SIZE, PLAYER_SIZE);
    }
    
    // Handle movement - 只在没有防抖动时检查移动
    if (debounce_counter == 0) {
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
    }

    // Handle BUTTON_1 - 高频射击
    if (Get_Button(BUTTON_1)) {
        if (button1_debounce == 0) {
            button1_debounce = 3;  // 减少防抖动时间，增加射击频率
            debounce_counter = 1;   // 减少全局延迟
            
            // 发射三连发子弹
            for (int burst = 0; burst < 3; burst++) {
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (!player_bullets[i].active) {
                        player_bullets[i].x = player.x + PLAYER_SIZE / 2 + burst - 1;
                        player_bullets[i].y = player.y - burst * 2;
                        player_bullets[i].active = 1;
                        player_bullets[i].speed = 4;
                        player_bullets[i].dx = 0;
                        player_bullets[i].dy = -1;  // Move up
                        break;
                    }
                }
            }
        }
    } else {
        button1_debounce = 0;  // Reset when button is released
    }

    // Handle BUTTON_2 - 超级扩散射击
    if (Get_Button(BUTTON_2)) {
        if (button2_debounce == 0) {
            button2_debounce = 15;  // 15 frame debounce for spread shot
            debounce_counter = 5;   // 5 frame delay for all inputs
            
            // 发射16方向的子弹（更密集）
            int directions[16][2] = {
                {0, -1},   // Up
                {1, -2},   // Up-Right steep
                {1, -1},   // Up-Right
                {2, -1},   // Up-Right shallow
                {1, 0},    // Right
                {2, 1},    // Down-Right shallow
                {1, 1},    // Down-Right
                {1, 2},    // Down-Right steep
                {0, 1},    // Down
                {-1, 2},   // Down-Left steep
                {-1, 1},   // Down-Left
                {-2, 1},   // Down-Left shallow
                {-1, 0},   // Left
                {-2, -1},  // Up-Left shallow
                {-1, -1},  // Up-Left
                {-1, -2}   // Up-Left steep
            };
            
            int bullets_fired = 0;
            for (int dir = 0; dir < 16 && bullets_fired < 16; dir++) {
                for (int i = 0; i < MAX_BULLETS && bullets_fired < 16; i++) {
                    if (!player_bullets[i].active) {
                        player_bullets[i].x = player.x + PLAYER_SIZE / 2;
                        player_bullets[i].y = player.y;
                        player_bullets[i].active = 1;
                        player_bullets[i].speed = 3;
                        player_bullets[i].dx = directions[dir][0];
                        player_bullets[i].dy = directions[dir][1];
                        bullets_fired++;
                        break;
                    }
                }
            }
        }
    } else {
        button2_debounce = 0;
    }
    
    // Decrease debounce counters
    if (button1_debounce > 0) button1_debounce--;
    if (button2_debounce > 0) button2_debounce--;
    
    // Draw player
    draw_rect(player.x, player.y, PLAYER_SIZE, PLAYER_SIZE, BLUE);
    
    prev_x = player.x;
    prev_y = player.y;
}

// Update player bullets
void update_player_bullets(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (player_bullets[i].active) {
            // Clear old position
            clear_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
            
            // Move bullet
            player_bullets[i].x += player_bullets[i].dx * player_bullets[i].speed;
            player_bullets[i].y += player_bullets[i].dy * player_bullets[i].speed;
            
            // Check if bullet is off screen
            if (player_bullets[i].y < 0 || player_bullets[i].y > SCREEN_HEIGHT ||
                player_bullets[i].x < 0 || player_bullets[i].x > SCREEN_WIDTH) {
                player_bullets[i].active = 0;
            } else {
                // Draw bullet
                u16 bullet_color = (player_bullets[i].dy == -1 && player_bullets[i].dx == 0) ? WHITE : CYAN;
                draw_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE, bullet_color);
            }
        }
    }
}

// Update enemies with movement and random spawning
void update_enemies(void) {
    static int spawn_timer = 0;
    static int formation_timer = 0;
    
    spawn_timer++;
    formation_timer++;
    
    // Spawn individual enemies randomly
    if (spawn_timer >= 30) {  // Every 0.5 seconds at 60 FPS
        spawn_timer = 0;
        if (simple_rand() % 100 < 30) {  // 30% chance to spawn
            spawn_random_enemy();
        }
    }
    
    // Spawn enemy formations occasionally
    if (formation_timer >= 300) {  // Every 5 seconds
        formation_timer = 0;
        if (simple_rand() % 100 < 20) {  // 20% chance to spawn formation
            spawn_enemy_formation();
        }
    }
    
    // Update enemy positions
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            // Clear old position
            clear_rect(enemies[i].x, enemies[i].y, ENEMY_SIZE, ENEMY_SIZE);
            
            // Move enemy down
            enemies[i].y += enemies[i].speed;
            
            // Remove enemy if it goes off screen
            if (enemies[i].y > SCREEN_HEIGHT) {
                enemies[i].speed = - enemies[i].speed;  // Reverse direction
                enemies[i].y = SCREEN_HEIGHT;  // Reset to bottom
                continue;
            }

            if (enemies[i].y < 0) {
                enemies[i].speed = - enemies[i].speed;  // Reverse direction
                enemies[i].y = 0;  // Reset to top
                continue;
            }
            
            // Add some horizontal movement for variety
            if (simple_rand() % 100 < 5) {  // 5% chance to change direction
                int direction = (simple_rand() % 3) - 1;  // -1, 0, or 1
                enemies[i].x += direction * 2;
                
                // Keep enemies on screen
                if (enemies[i].x < 0) enemies[i].x = 0;
                if (enemies[i].x > SCREEN_WIDTH - ENEMY_SIZE) enemies[i].x = SCREEN_WIDTH - ENEMY_SIZE;
            }
        }
    }
    
    // Draw enemies with different colors based on type
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            u16 enemy_color;
            switch (enemies[i].type) {
                case 0: enemy_color = RED; break;
                case 1: enemy_color = MAGENTA; break;
                case 2: enemy_color = YELLOW; break;
                case 3: enemy_color = GREEN; break;  // 新增第四种类型
                default: enemy_color = RED; break;
            }
            draw_rect(enemies[i].x, enemies[i].y, ENEMY_SIZE, ENEMY_SIZE, enemy_color);
        }
    }
}

// Update enemy bullets system - 大幅增加射击频率
void update_enemy_bullets(void) {
    // Enemy shooting logic - 更频繁的射击
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            enemies[i].shoot_timer++;
            
            // 不同类型的敌人有不同的射击模式
            int shoot_interval;
            switch (enemies[i].type) {
                case 0: shoot_interval = 60; break;  // 1秒一发
                case 1: shoot_interval = 40; break;  // 0.67秒一发
                case 2: shoot_interval = 80; break;  // 1.33秒一发，但发射扩散弹
                case 3: shoot_interval = 30; break;  // 0.5秒一发 - 新类型：快速射击
                default: shoot_interval = 60; break;
            }
            
            if (enemies[i].shoot_timer >= shoot_interval) {
                enemies[i].shoot_timer = 0;
                
                if (enemies[i].type == 2) {
                    // 类型2敌人发射三发扩散弹
                    int spread_dirs[3][2] = {{-1, 1}, {0, 1}, {1, 1}};
                    for (int spread = 0; spread < 3; spread++) {
                        for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                            if (!enemy_bullets[j].active) {
                                enemy_bullets[j].x = enemies[i].x + ENEMY_SIZE / 2;
                                enemy_bullets[j].y = enemies[i].y + ENEMY_SIZE;
                                enemy_bullets[j].active = 1;
                                enemy_bullets[j].speed = 2;
                                enemy_bullets[j].dx = spread_dirs[spread][0];
                                enemy_bullets[j].dy = spread_dirs[spread][1];
                                break;
                            }
                        }
                    }
                } else if (enemies[i].type == 3) {
                    // 类型3敌人发射5方向扩散弹
                    int spread_dirs[5][2] = {{-2, 1}, {-1, 1}, {0, 1}, {1, 1}, {2, 1}};
                    for (int spread = 0; spread < 5; spread++) {
                        for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                            if (!enemy_bullets[j].active) {
                                enemy_bullets[j].x = enemies[i].x + ENEMY_SIZE / 2;
                                enemy_bullets[j].y = enemies[i].y + ENEMY_SIZE;
                                enemy_bullets[j].active = 1;
                                enemy_bullets[j].speed = 1;
                                enemy_bullets[j].dx = spread_dirs[spread][0];
                                enemy_bullets[j].dy = spread_dirs[spread][1];
                                break;
                            }
                        }
                    }
                } else {
                    // 普通敌人发射单发子弹
                    for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                        if (!enemy_bullets[j].active) {
                            enemy_bullets[j].x = enemies[i].x + ENEMY_SIZE / 2;
                            enemy_bullets[j].y = enemies[i].y + ENEMY_SIZE;
                            enemy_bullets[j].active = 1;
                            enemy_bullets[j].speed = 2;
                            enemy_bullets[j].dx = 0;
                            enemy_bullets[j].dy = 1;  // Move down
                            break;
                        }
                    }
                }
            }
        }
    }
    
    // Update enemy bullets
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active) {
            // Clear old position
            clear_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
            
            // Move bullet
            enemy_bullets[i].x += enemy_bullets[i].dx * enemy_bullets[i].speed;
            enemy_bullets[i].y += enemy_bullets[i].dy * enemy_bullets[i].speed;
            
            // Check if bullet is off screen
            if (enemy_bullets[i].y > SCREEN_HEIGHT || enemy_bullets[i].x < 0 || 
                enemy_bullets[i].x > SCREEN_WIDTH) {
                enemy_bullets[i].active = 0;
            } else {
                // Draw bullet
                draw_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE, YELLOW);
            }
        }
    }
}

// Check collisions between bullets and targets
void check_collisions(void) {
    // Check player bullets hitting enemies
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (player_bullets[i].active) {
            for (int j = 0; j < MAX_ENEMIES; j++) {
                if (enemies[j].active) {
                    // Simple collision detection
                    if (player_bullets[i].x >= enemies[j].x && 
                        player_bullets[i].x < enemies[j].x + ENEMY_SIZE &&
                        player_bullets[i].y >= enemies[j].y && 
                        player_bullets[i].y < enemies[j].y + ENEMY_SIZE) {
                        
                        // Hit! Clear both bullet and enemy positions first
                        clear_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
                        clear_rect(enemies[j].x, enemies[j].y, ENEMY_SIZE, ENEMY_SIZE);
                        
                        // Then deactivate them
                        player_bullets[i].active = 0;
                        enemies[j].active = 0;
                        
                        // 有概率生成新敌人作为奖励机制
                        if (simple_rand() % 100 < 10) {  // 10% chance
                            spawn_random_enemy();
                        }
                    }
                }
            }
        }
    }
    
    // Check enemy bullets hitting player
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active) {
            // Simple collision detection with player
            if (enemy_bullets[i].x >= player.x && 
                enemy_bullets[i].x < player.x + PLAYER_SIZE &&
                enemy_bullets[i].y >= player.y && 
                enemy_bullets[i].y < player.y + PLAYER_SIZE) {
                
                // Hit! Clear bullet position first, then deactivate
                clear_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
                enemy_bullets[i].active = 0;
            }
        }
    }
}

// Count active bullets and enemies
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

// Main game loop
void game_loop(int difficulty) {
    init_game();
    
    // Clear screen
    LCD_Clear(BLACK);
    
    while (1) {  // Infinite loop - no exit mechanism
        // Increment frame counter
        frame_counter++;
        
        // Update performance counters
        update_fps_counter();
        update_entity_counter();
        
        // Update game objects
        update_player();
        
        // Check collisions BEFORE updating bullets
        check_collisions();
        
        // Then update bullets (this will clear inactive bullets properly)
        update_player_bullets();
        update_enemies();
        update_enemy_bullets();
        
        // Draw performance counters (at the end to avoid being overwritten)
        draw_performance_counters();
        
        // Frame rate control - maintain 60 FPS
        delay_1ms(16); // Approximately 60 FPS
    }
}

int main(void) {
  IO_init();
  
  int difficulty = select();  // Call assembly function
  
  // Start game based on difficulty
  game_loop(difficulty);
  
  return 0;
}