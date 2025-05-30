#include "lcd/lcd.h"
#include "utils.h"
#include "assembly/example.h"
#include "assembly/select.h"

// Game constants - 大幅增加数量
#define SCREEN_WIDTH 120
#define SCREEN_HEIGHT 80
#define PLAYER_SIZE 4
#define ENEMY_SIZE 3
#define BULLET_SIZE 2
#define MAX_BULLETS 300         // 增加到300个玩家子弹
#define MAX_ENEMY_BULLETS 300   // 增加到300个敌人子弹
#define MAX_ENEMIES 200         

// 追踪子弹相关常量
#define MAX_TRACKING_BULLETS 50    // 追踪子弹最大数量
#define TRACKING_BULLET_SIZE 4     // 追踪子弹尺寸（正方形）
#define TRACKING_BULLET_SPEED 2    // 追踪子弹速度

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

// 追踪子弹结构体
typedef struct {
    int x, y;
    int active;
    int target_enemy_index;  // 追踪的敌人索引
    int lifetime;           // 子弹生存时间
} TrackingBullet;

// Game state
Player player;
Bullet player_bullets[MAX_BULLETS];
Bullet enemy_bullets[MAX_ENEMY_BULLETS];
Enemy enemies[MAX_ENEMIES];
TrackingBullet tracking_bullets[MAX_TRACKING_BULLETS];

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

// 添加这些变量到性能计数器部分
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

// 添加真实时间测量变量
static uint64_t fps_measurement_start_time = 0;
static uint32_t fps_frame_count = 0;

// 真实FPS计数器 - 使用get_timer_value()测量
void update_fps_counter(void) {
    
    // 获取当前真实时间
    uint64_t current_time = get_timer_value();
    
    // 增加帧计数
    fps_frame_count++;
    
    // 如果这是第一次测量，记录开始时间
    if (fps_measurement_start_time == 0) {
        fps_measurement_start_time = current_time;
        fps_frame_count = 0;
        return;
    }
    
    // 每30帧更新一次FPS显示（约0.5秒）
    if (fps_frame_count >= 1) {
        uint64_t elapsed_time = current_time - fps_measurement_start_time;
        
        if (elapsed_time > 0) {
            // 计算真实FPS
            // elapsed_time 的单位是定时器滴答
            // SystemCoreClock/4 是每秒的滴答数（根据你的delay_1ms实现）
            uint64_t ticks_per_second = SystemCoreClock / 4;
            
            // FPS = 帧数 / 秒数 = 帧数 / (elapsed_ticks / ticks_per_second)
            displayed_fps = (fps_frame_count * ticks_per_second) / elapsed_time;
            
            // 限制显示范围
            if (displayed_fps > 99) displayed_fps = 99;
            if (displayed_fps < 1) displayed_fps = 1;
        }
        
        // 重置测量
        fps_measurement_start_time = current_time;
        fps_frame_count = 0;
    }
}

// 优化的绘制函数 - 减少像素操作
void draw_rect_optimized(int x, int y, int width, int height, u16 color) {
    // 边界检查一次，而不是每个像素都检查
    int start_x = (x < 0) ? 0 : x;
    int start_y = (y < 0) ? 0 : y;
    int end_x = (x + width > SCREEN_WIDTH) ? SCREEN_WIDTH : x + width;
    int end_y = (y + height > SCREEN_HEIGHT) ? SCREEN_HEIGHT : y + height;
    
    // 如果完全在屏幕外，直接返回
    if (start_x >= end_x || start_y >= end_y) return;
    
    // 批量绘制，减少函数调用
    for (int j = start_y; j < end_y; j++) {
        for (int i = start_x; i < end_x; i++) {
            LCD_DrawPoint(i, j, color);
        }
    }
}

// 替换所有draw_rect调用为draw_rect_optimized
#define draw_rect draw_rect_optimized

int count_active_entities(void) {
    int count = 1; // Player counts as 1
    
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (player_bullets[i].active) count++;
    }
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active) count++;
    }
    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) {
        if (tracking_bullets[i].active) count++;
    }
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) count++;
    }
    
    return count;
}

// 减少计数器更新频率
void update_entity_counter_optimized(void) {
    entity_update_counter++;
    if (entity_update_counter >= 1) {  // 每20帧更新一次
        entity_update_counter = 0;
        displayed_entities = count_active_entities();
        if (displayed_entities > 999) displayed_entities = 999;
    }
}

#define update_entity_counter update_entity_counter_optimized

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
        LCD_ShowString(SCREEN_WIDTH + 5, 35, (u8*)"ENT", WHITE);
        LCD_ShowNum(SCREEN_WIDTH +5 , 50, displayed_entities, 3, WHITE);
        
        last_displayed_entities = displayed_entities;
    }
}

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

// 寻找最近敌人的函数
int find_nearest_enemy(int x, int y) {
    int nearest_index = -1;
    int min_distance_squared = 999999;
    
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            int dx = enemies[i].x - x;
            int dy = enemies[i].y - y;
            int distance_squared = dx * dx + dy * dy;
            
            if (distance_squared < min_distance_squared) {
                min_distance_squared = distance_squared;
                nearest_index = i;
            }
        }
    }
    
    return nearest_index;
}

// 发射追踪子弹的函数
void fire_tracking_bullet(void) {
    // 寻找最近的敌人
    int target_index = find_nearest_enemy(player.x, player.y);
    if (target_index == -1) return; // 没有敌人可追踪
    
    // 寻找空闲的追踪子弹槽
    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) {
        if (!tracking_bullets[i].active) {
            tracking_bullets[i].x = player.x + PLAYER_SIZE / 2 - TRACKING_BULLET_SIZE / 2;
            tracking_bullets[i].y = player.y;
            tracking_bullets[i].active = 1;
            tracking_bullets[i].target_enemy_index = target_index;
            tracking_bullets[i].lifetime = 300; // 5秒生存时间（60FPS）
            break;
        }
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
        player_bullets[i].dx = 0;
        player_bullets[i].dy = 0;
    }
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        enemy_bullets[i].active = 0;
        enemy_bullets[i].dx = 0;
        enemy_bullets[i].dy = 0;
    }
    
    // Initialize tracking bullets
    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) {
        tracking_bullets[i].active = 0;
        tracking_bullets[i].target_enemy_index = -1;
        tracking_bullets[i].lifetime = 0;
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
        frame_times[i] = 1; // Assume 16ms initially
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
            enemies[i].shoot_timer = simple_rand() % 60 + 20;  // Random initial timer
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
    static int center_button_debounce = 0;  // 新增center键防抖动
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
    
    // Handle CENTER button - 追踪子弹
    if (Get_Button(JOY_CTR)) {
        if (center_button_debounce == 0) {
            center_button_debounce = 30;  // 30帧防抖动，避免过于频繁发射
            debounce_counter = 5;
            
            // 发射追踪子弹
            fire_tracking_bullet();
        }
    } else {
        center_button_debounce = 0;
    }
    
    // Decrease debounce counters
    if (button1_debounce > 0) button1_debounce--;
    if (button2_debounce > 0) button2_debounce--;
    if (center_button_debounce > 0) center_button_debounce--;
    
    // Draw player
    draw_rect(player.x, player.y, PLAYER_SIZE, PLAYER_SIZE, BLUE);
    
    prev_x = player.x;
    prev_y = player.y;
}

// 优化的子弹更新 - 批量处理
void update_player_bullets_optimized(void) {
    // 第一遍：更新位置和标记无效子弹
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!player_bullets[i].active) continue;
        
        // 清除旧位置
        clear_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
        
        // 更新位置
        player_bullets[i].x += player_bullets[i].dx * player_bullets[i].speed;
        player_bullets[i].y += player_bullets[i].dy * player_bullets[i].speed;
        
        // 屏幕外检查
        if (player_bullets[i].y < -10 || player_bullets[i].y > SCREEN_HEIGHT + 10 ||
            player_bullets[i].x < -10 || player_bullets[i].x > SCREEN_WIDTH + 10) {
            player_bullets[i].active = 0;
        }
    }
    
    // 第二遍：只绘制有效子弹
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (player_bullets[i].active) {
            u16 bullet_color = (player_bullets[i].dy == -1 && player_bullets[i].dx == 0) ? WHITE : CYAN;
            draw_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE, bullet_color);
        }
    }
}

#define update_player_bullets update_player_bullets_optimized

// 更新追踪子弹的函数
void update_tracking_bullets(void) {
    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) {
        if (!tracking_bullets[i].active) continue;
        
        // 清除旧位置
        LCD_DrawRectangle(tracking_bullets[i].x, tracking_bullets[i].y, 
                         tracking_bullets[i].x + TRACKING_BULLET_SIZE - 1, 
                         tracking_bullets[i].y + TRACKING_BULLET_SIZE - 1, BLACK);
        
        // 减少生存时间
        tracking_bullets[i].lifetime--;
        if (tracking_bullets[i].lifetime <= 0) {
            tracking_bullets[i].active = 0;
            continue;
        }
        
        // 检查目标敌人是否还存在
        int target_index = tracking_bullets[i].target_enemy_index;
        if (target_index < 0 || target_index >= MAX_ENEMIES || !enemies[target_index].active) {
            // 重新寻找最近的敌人
            target_index = find_nearest_enemy(tracking_bullets[i].x, tracking_bullets[i].y);
            tracking_bullets[i].target_enemy_index = target_index;
            
            if (target_index == -1) {
                // 没有敌人可追踪，子弹向上飞行
                tracking_bullets[i].y -= TRACKING_BULLET_SPEED;
            }
        }
        
        if (target_index >= 0) {
            // 计算朝向目标的方向
            int target_x = enemies[target_index].x + ENEMY_SIZE / 2;
            int target_y = enemies[target_index].y + ENEMY_SIZE / 2;
            
            int dx = target_x - tracking_bullets[i].x;
            int dy = target_y - tracking_bullets[i].y;
            
            // 简单的追踪算法：朝目标方向移动
            if (dx != 0 || dy != 0) {
                // 计算移动方向（简化版本，不使用浮点数）
                int move_x = 0, move_y = 0;
                
                if (dx > TRACKING_BULLET_SPEED) move_x = TRACKING_BULLET_SPEED;
                else if (dx < -TRACKING_BULLET_SPEED) move_x = -TRACKING_BULLET_SPEED;
                else move_x = dx;
                
                if (dy > TRACKING_BULLET_SPEED) move_y = TRACKING_BULLET_SPEED;
                else if (dy < -TRACKING_BULLET_SPEED) move_y = -TRACKING_BULLET_SPEED;
                else move_y = dy;
                
                tracking_bullets[i].x += move_x;
                tracking_bullets[i].y += move_y;
            }
        }
        
        // 屏幕边界检查
        if (tracking_bullets[i].x < 0 || tracking_bullets[i].x > SCREEN_WIDTH - TRACKING_BULLET_SIZE ||
            tracking_bullets[i].y < 0 || tracking_bullets[i].y > SCREEN_HEIGHT - TRACKING_BULLET_SIZE) {
            tracking_bullets[i].active = 0;
            continue;
        }
        
        // 绘制追踪子弹（正方形边框，用绿色表示追踪子弹）
        LCD_DrawRectangle(tracking_bullets[i].x, tracking_bullets[i].y, 
                         tracking_bullets[i].x + TRACKING_BULLET_SIZE - 1, 
                         tracking_bullets[i].y + TRACKING_BULLET_SIZE - 1, GREEN);
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

// 优化的敌人子弹系统
void update_enemy_bullets_optimized(void) {
    // 计算当前子弹数量，控制最大数量
    int active_enemy_bullets = 0;
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active) active_enemy_bullets++;
    }
    
    if (active_enemy_bullets < 1000) { // 限制敌人子弹数量
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (!enemies[i].active) continue;
            
            enemies[i].shoot_timer++;
            
            int shoot_interval;
            switch (enemies[i].type) {
                case 0: shoot_interval = 120; break; // 降低射击频率
                case 1: shoot_interval = 80; break;
                case 2: shoot_interval = 160; break;
                case 3: shoot_interval = 60; break;
                default: shoot_interval = 120; break;
            }
            
            if (enemies[i].shoot_timer >= shoot_interval) {
                enemies[i].shoot_timer = 0;
                
                // 简化射击模式，减少计算
                for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                    if (!enemy_bullets[j].active) {
                        enemy_bullets[j].x = enemies[i].x + ENEMY_SIZE / 2;
                        enemy_bullets[j].y = enemies[i].y + ENEMY_SIZE;
                        enemy_bullets[j].active = 1;
                        enemy_bullets[j].speed = 2;
                        enemy_bullets[j].dx = 0;
                        enemy_bullets[j].dy = 1;
                        break; // 只发射一发，简化
                    }
                }
            }
        }
    }
    
    // 批量更新敌人子弹（与玩家子弹类似的优化）
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (!enemy_bullets[i].active) continue;
        
        clear_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
        
        enemy_bullets[i].x += enemy_bullets[i].dx * enemy_bullets[i].speed;
        enemy_bullets[i].y += enemy_bullets[i].dy * enemy_bullets[i].speed;
        
        if (enemy_bullets[i].y > SCREEN_HEIGHT + 10 || enemy_bullets[i].x < -10 || 
            enemy_bullets[i].x > SCREEN_WIDTH + 10 || enemy_bullets[i].y < -10) {
            enemy_bullets[i].active = 0;
        }
    }
    
    // 批量绘制
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active) {
            draw_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE, YELLOW);
        }
    }
}

#define update_enemy_bullets update_enemy_bullets_optimized

// 优化的碰撞检测 - 使用空间分割
void check_collisions_optimized(void) {
    // 只检查屏幕内的物体
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!player_bullets[i].active) continue;
        
        // 屏幕外的子弹不参与碰撞检测
        if (player_bullets[i].x < 0 || player_bullets[i].x > SCREEN_WIDTH ||
            player_bullets[i].y < 0 || player_bullets[i].y > SCREEN_HEIGHT) {
            continue;
        }
        
        for (int j = 0; j < MAX_ENEMIES; j++) {
            if (!enemies[j].active) continue;
            
            // 快速距离检查 - 避免复杂计算
            int dx = player_bullets[i].x - enemies[j].x;
            int dy = player_bullets[i].y - enemies[j].y;
            
            // 使用平方距离避免sqrt
            if (dx * dx + dy * dy < 25) { // 约5像素半径
                // 精确碰撞检测
                if (player_bullets[i].x >= enemies[j].x && 
                    player_bullets[i].x < enemies[j].x + ENEMY_SIZE &&
                    player_bullets[i].y >= enemies[j].y && 
                    player_bullets[i].y < enemies[j].y + ENEMY_SIZE) {
                    
                    // 碰撞处理
                    clear_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
                    clear_rect(enemies[j].x, enemies[j].y, ENEMY_SIZE, ENEMY_SIZE);
                    
                    player_bullets[i].active = 0;
                    enemies[j].active = 0;
                    
                    if (simple_rand() % 100 < 10) {
                        spawn_random_enemy();
                    }
                    break; // 子弹已销毁，跳出内层循环
                }
            }
        }
    }
    
    // 追踪子弹与敌人的碰撞检测
    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) {
        if (!tracking_bullets[i].active) continue;
        
        for (int j = 0; j < MAX_ENEMIES; j++) {
            if (!enemies[j].active) continue;
            
            // 快速距离检查
            int dx = tracking_bullets[i].x + TRACKING_BULLET_SIZE/2 - (enemies[j].x + ENEMY_SIZE/2);
            int dy = tracking_bullets[i].y + TRACKING_BULLET_SIZE/2 - (enemies[j].y + ENEMY_SIZE/2);
            
            if (dx * dx + dy * dy < 36) { // 约6像素半径
                // 精确碰撞检测
                if (tracking_bullets[i].x < enemies[j].x + ENEMY_SIZE &&
                    tracking_bullets[i].x + TRACKING_BULLET_SIZE > enemies[j].x &&
                    tracking_bullets[i].y < enemies[j].y + ENEMY_SIZE &&
                    tracking_bullets[i].y + TRACKING_BULLET_SIZE > enemies[j].y) {
                    
                    // 碰撞处理
                    LCD_DrawRectangle(tracking_bullets[i].x, tracking_bullets[i].y, 
                                     tracking_bullets[i].x + TRACKING_BULLET_SIZE - 1, 
                                     tracking_bullets[i].y + TRACKING_BULLET_SIZE - 1, BLACK);
                    clear_rect(enemies[j].x, enemies[j].y, ENEMY_SIZE, ENEMY_SIZE);
                    
                    tracking_bullets[i].active = 0;
                    enemies[j].active = 0;
                    
                    // 随机生成新敌人
                    if (simple_rand() % 100 < 15) {
                        spawn_random_enemy();
                    }
                    break;
                }
            }
        }
    }
    
    // 敌人子弹碰撞检测（类似优化）
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (!enemy_bullets[i].active) continue;
        
        // 与玩家的距离检查
        int dx = enemy_bullets[i].x - player.x;
        int dy = enemy_bullets[i].y - player.y;
        
        if (dx * dx + dy * dy < 16) { // 约4像素半径
            if (enemy_bullets[i].x >= player.x && 
                enemy_bullets[i].x < player.x + PLAYER_SIZE &&
                enemy_bullets[i].y >= player.y && 
                enemy_bullets[i].y < player.y + PLAYER_SIZE) {
                
                clear_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
                enemy_bullets[i].active = 0;
            }
        }
    }
}

// 替换原函数
#define check_collisions check_collisions_optimized

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
    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) {
        if (tracking_bullets[i].active) bullet_count++;
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
        update_tracking_bullets();  // 新增：更新追踪子弹
        update_enemies();
        update_enemy_bullets();
        
        // Draw performance counters (at the end to avoid being overwritten)
        draw_performance_counters();
        
        // Frame rate control - maintain 60 FPS
        delay_1ms(1); // Approximately 60 FPS
    }
}

int main(void) {
  IO_init();
  
  int difficulty = select();  // Call assembly function
  
  // Start game based on difficulty
  game_loop(difficulty);
  
  return 0;
}