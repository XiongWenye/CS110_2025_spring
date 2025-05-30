#include "lcd/lcd.h"
#include "utils.h"
#include "assembly/example.h"
#include "assembly/select.h"

// Game constants - 大幅增加数量
#define SCREEN_WIDTH 120
#define SCREEN_HEIGHT 80
#define PLAYER_SIZE 6
#define ENEMY_SIZE 8
#define BULLET_SIZE 2
#define MAX_BULLETS 300         // 增加到300个玩家子弹
#define MAX_ENEMY_BULLETS 300   // 增加到300个敌人子弹
#define MAX_ENEMIES 200         

// 追踪子弹相关常量
#define MAX_TRACKING_BULLETS 50    // 追踪子弹最大数量
#define TRACKING_BULLET_SIZE 4     // 追踪子弹尺寸（正方形）
#define TRACKING_BULLET_SPEED 2    // 追踪子弹速度

// 敌人和子弹形态常量
#define SHAPE_HOLLOW_CIRCLE 0     // 空心圆
#define SHAPE_HOLLOW_SQUARE 1     // 空心正方形
#define SHAPE_SOLID_SQUARE 2      // 实心小方块

// FPS and entity counting constants
#define FPS_SAMPLE_FRAMES 8     // Sample over 8 frames (0.133s at 60fps)
#define ENTITY_UPDATE_INTERVAL 3  // Update entity counter every 4 frames

// Player structure
typedef struct {
    int x, y;
    int prev_x, prev_y;  // 记录前一帧位置
    int speed;
    int visible;         // 是否需要重绘
} Player;

// Bullet structure
typedef struct {
    int x, y;
    int prev_x, prev_y;  // 记录前一帧位置
    int active;
    int speed;
    int dx, dy;  // Direction vectors
    int shape;   // 新增：子弹形态
    int visible; // 是否需要重绘
} Bullet;

// Enemy structure
typedef struct {
    int x, y;
    int prev_x, prev_y;  // 记录前一帧位置
    int active;
    int speed;
    int shoot_timer;
    int type;   // 敌人类型
    int shape;  // 新增：敌人形态
    int visible; // 是否需要重绘
} Enemy;

// 追踪子弹结构体
typedef struct {
    int x, y;
    int prev_x, prev_y;  // 记录前一帧位置
    int active;
    int target_enemy_index;  // 追踪的敌人索引
    int lifetime;           // 子弹生存时间
    int visible;           // 是否需要重绘
} TrackingBullet;

// Game state
Player player;
Bullet player_bullets[MAX_BULLETS];
Bullet enemy_bullets[MAX_ENEMY_BULLETS];
Enemy enemies[MAX_ENEMIES];
TrackingBullet tracking_bullets[MAX_TRACKING_BULLETS];

// Random number generator state
static unsigned int rand_seed = 12345;

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

// 优化的绘制函数 - 减少像素操作
void draw_rect_fast(int x, int y, int width, int height, u16 color) {
    // 边界检查一次，而不是每个像素都检查
    int start_x = (x < 0) ? 0 : x;
    int start_y = (y < 0) ? 0 : y;
    int end_x = (x + width > SCREEN_WIDTH) ? SCREEN_WIDTH : x + width;
    int end_y = (y + height > SCREEN_HEIGHT) ? SCREEN_HEIGHT : y + height;
    
    // 如果完全在屏幕外，直接返回
    if (start_x >= end_x || start_y >= end_y) return;
    
    // 使用LCD_Fill函数进行批量绘制，比逐像素绘制快很多
    if (end_x - start_x > 0 && end_y - start_y > 0) {
        LCD_Fill(start_x, start_y, end_x - 1, end_y - 1, color);
    }
}

// 清除敌人/子弹的函数 - 优化版本
void clear_shape_fast(int x, int y, int size, int shape) {
    if (x < -size || x > SCREEN_WIDTH || y < -size || y > SCREEN_HEIGHT) return;
    
    switch(shape) {
        case SHAPE_HOLLOW_CIRCLE:
            LCD_DrawCircle(x + size/2, y + size/2, size/2, BLACK);
            break;
        case SHAPE_HOLLOW_SQUARE:
            LCD_DrawRectangle(x, y, x + size - 1, y + size - 1, BLACK);
            break;
        case SHAPE_SOLID_SQUARE:
            draw_rect_fast(x, y, size, size, BLACK);
            break;
    }
}

// 绘制敌人/子弹的函数 - 优化版本
void draw_shape_fast(int x, int y, int size, int shape, u16 color) {
    if (x < -size || x > SCREEN_WIDTH || y < -size || y > SCREEN_HEIGHT) return;
    
    switch(shape) {
        case SHAPE_HOLLOW_CIRCLE:
            LCD_DrawCircle(x + size/2, y + size/2, size/2, color);
            break;
        case SHAPE_HOLLOW_SQUARE:
            LCD_DrawRectangle(x, y, x + size - 1, y + size - 1, color);
            break;
        case SHAPE_SOLID_SQUARE:
            draw_rect_fast(x, y, size, size, color);
            break;
    }
}

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
    
    // 每60帧更新一次FPS显示（约1秒）
    if (fps_frame_count >= 60) {
        uint64_t elapsed_time = current_time - fps_measurement_start_time;
        
        if (elapsed_time > 0) {
            // 计算真实FPS
            uint64_t ticks_per_second = SystemCoreClock / 4;
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
void update_entity_counter(void) {
    entity_update_counter++;
    if (entity_update_counter >= 30) {  // 每30帧更新一次
        entity_update_counter = 0;
        displayed_entities = count_active_entities();
        if (displayed_entities > 999) displayed_entities = 999;
    }
}

// 修改绘制函数 - 只在数值变化时重绘
void draw_performance_counters(void) {
    // 只在FPS变化时重绘FPS区域
    if (displayed_fps != last_displayed_fps) {
        // 清除并重绘FPS区域
        draw_rect_fast(SCREEN_WIDTH - 35, 5, 35, 15, BLACK);
        LCD_ShowString(SCREEN_WIDTH - 30, 5, (u8*)"FPS", WHITE);
        LCD_ShowNum(SCREEN_WIDTH - 30, 12, displayed_fps, 2, WHITE);
        last_displayed_fps = displayed_fps;
    }
    
    // 只在实体数变化时重绘实体区域
    if (displayed_entities != last_displayed_entities) {
        // 清除并重绘实体区域
        draw_rect_fast(SCREEN_WIDTH - 35, 25, 35, 15, BLACK);
        LCD_ShowString(SCREEN_WIDTH - 30, 25, (u8*)"ENT", WHITE);
        LCD_ShowNum(SCREEN_WIDTH - 30, 32, displayed_entities, 3, WHITE);
        last_displayed_entities = displayed_entities;
    }
}

// Safe LCD drawing function with bounds checking
void safe_draw_pixel(int x, int y, u16 color) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        LCD_DrawPoint(x, y, color);
    }
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
            tracking_bullets[i].prev_x = tracking_bullets[i].x;
            tracking_bullets[i].prev_y = tracking_bullets[i].y;
            tracking_bullets[i].active = 1;
            tracking_bullets[i].visible = 1;
            tracking_bullets[i].target_enemy_index = target_index;
            tracking_bullets[i].lifetime = 300; // 5秒生存时间（60FPS）
            break;
        }
    }
}

// Spawn a random enemy
void spawn_random_enemy(void) {
    // Find an inactive enemy slot
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            // Random spawn position at top of screen
            enemies[i].x = simple_rand() % (SCREEN_WIDTH - ENEMY_SIZE);
            enemies[i].y = -(simple_rand() % 20 + 5);  // Spawn above screen
            enemies[i].prev_x = enemies[i].x;
            enemies[i].prev_y = enemies[i].y;
            enemies[i].active = 1;
            enemies[i].visible = 1;
            enemies[i].speed = 1 + simple_rand() % 2;  // Speed 1-2
            enemies[i].shoot_timer = simple_rand() % 60 + 20;  // Random initial timer
            enemies[i].type = simple_rand() % 3;  // 三种类型的敌人 (0-2)
            enemies[i].shape = simple_rand() % 3; // 随机形态：0=空心圆，1=空心正方形，2=实心方块
            break;
        }
    }
}

// Initialize game objects 
void init_game(void) {
    // Initialize player
    player.x = SCREEN_WIDTH / 2;
    player.y = SCREEN_HEIGHT - 20;
    player.prev_x = player.x;
    player.prev_y = player.y;
    player.speed = 2;
    player.visible = 1;
    
    // Initialize bullets
    for (int i = 0; i < MAX_BULLETS; i++) {
        player_bullets[i].active = 0;
        player_bullets[i].visible = 0;
        player_bullets[i].dx = 0;
        player_bullets[i].dy = 0;
        player_bullets[i].shape = SHAPE_SOLID_SQUARE; // 玩家子弹默认实心方块
    }
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        enemy_bullets[i].active = 0;
        enemy_bullets[i].visible = 0;
        enemy_bullets[i].dx = 0;
        enemy_bullets[i].dy = 0;
        enemy_bullets[i].shape = SHAPE_SOLID_SQUARE; // 默认形态
    }
    
    // Initialize tracking bullets
    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) {
        tracking_bullets[i].active = 0;
        tracking_bullets[i].visible = 0;
        tracking_bullets[i].target_enemy_index = -1;
        tracking_bullets[i].lifetime = 0;
    }
    
    // Initialize enemies - 初始生成一些敌人
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = 0;  // 开始时都不活跃，随机生成
        enemies[i].visible = 0;
        enemies[i].shape = SHAPE_SOLID_SQUARE; // 默认形态
    }
    
    // 生成初始敌人
    for (int i = 0; i < 5; i++) {  // 初始生成5个敌人
        spawn_random_enemy();
    }
    
    // Initialize performance counters
    for (int i = 0; i < FPS_SAMPLE_FRAMES; i++) {
        frame_times[i] = 15; // Assume 16ms initially
    }
    frame_counter = 0;
}

// Spawn enemy formation
void spawn_enemy_formation(void) {
    // Spawn a formation of 3-5 enemies
    int formation_size = 3 + simple_rand() % 3;  // 3-5 enemies
    int start_x = simple_rand() % (SCREEN_WIDTH - formation_size * 20);
    
    for (int i = 0; i < formation_size; i++) {
        for (int j = 0; j < MAX_ENEMIES; j++) {
            if (!enemies[j].active) {
                enemies[j].x = start_x + i * 20;
                enemies[j].y = -(simple_rand() % 15 + 5);
                enemies[j].prev_x = enemies[j].x;
                enemies[j].prev_y = enemies[j].y;
                enemies[j].active = 1;
                enemies[j].visible = 1;
                enemies[j].speed = 1;
                enemies[j].shoot_timer = simple_rand() % 40;
                enemies[j].type = simple_rand() % 3;
                enemies[j].shape = simple_rand() % 3; // 随机形态
                break;
            }
        }
    }
}

// Handle player input and movement - 优化版本
void update_player(void) {
    // 添加静态变量来防抖动
    static int button1_debounce = 0;
    static int button2_debounce = 0;
    static int center_button_debounce = 0;  // 新增center键防抖动
    static int debounce_counter = 0;
    
    // 防抖动计数器
    if (debounce_counter > 0) {
        debounce_counter--;
    }
    
    // 记录前一帧位置
    player.prev_x = player.x;
    player.prev_y = player.y;
    
    // Handle movement - 只在没有防抖动时检查移动
    if (debounce_counter == 0) {
        if (Get_Button(JOY_LEFT) && player.x > 0) {
            player.x -= player.speed;
            player.visible = 1;
        }
        if (Get_Button(JOY_RIGHT) && player.x < SCREEN_WIDTH - PLAYER_SIZE) {
            player.x += player.speed;
            player.visible = 1;
        }
        if (Get_Button(JOY_UP) && player.y > 0) {
            player.y -= player.speed;
            player.visible = 1;
        }
        if (Get_Button(JOY_DOWN) && player.y < SCREEN_HEIGHT - PLAYER_SIZE) {
            player.y += player.speed;
            player.visible = 1;
        }
    }

    // Handle BUTTON_1 - 高频射击
    if (Get_Button(BUTTON_1)) {
        if (button1_debounce == 0) {
            button1_debounce = 5;  // 减少防抖动时间，增加射击频率
            debounce_counter = 1;   // 减少全局延迟
            
            // 发射三连发子弹
            for (int burst = 0; burst < 3; burst++) {
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (!player_bullets[i].active) {
                        player_bullets[i].x = player.x + PLAYER_SIZE / 2 + burst - 1;
                        player_bullets[i].y = player.y - burst * 2;
                        player_bullets[i].prev_x = player_bullets[i].x;
                        player_bullets[i].prev_y = player_bullets[i].y;
                        player_bullets[i].active = 1;
                        player_bullets[i].visible = 1;
                        player_bullets[i].speed = 4;
                        player_bullets[i].dx = 0;
                        player_bullets[i].dy = -1;  // Move up
                        player_bullets[i].shape = SHAPE_SOLID_SQUARE; // 实心方块
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
            button2_debounce = 20;  // 20 frame debounce for spread shot
            debounce_counter = 5;   // 5 frame delay for all inputs
            
            // 发射8方向的子弹（减少数量提高性能）
            int directions[8][2] = {
                {0, -1},   // Up
                {1, -1},   // Up-Right
                {1, 0},    // Right
                {1, 1},    // Down-Right
                {0, 1},    // Down
                {-1, 1},   // Down-Left
                {-1, 0},   // Left
                {-1, -1}   // Up-Left
            };
            
            int bullets_fired = 0;
            for (int dir = 0; dir < 8 && bullets_fired < 8; dir++) {
                for (int i = 0; i < MAX_BULLETS && bullets_fired < 8; i++) {
                    if (!player_bullets[i].active) {
                        player_bullets[i].x = player.x + PLAYER_SIZE / 2;
                        player_bullets[i].y = player.y;
                        player_bullets[i].prev_x = player_bullets[i].x;
                        player_bullets[i].prev_y = player_bullets[i].y;
                        player_bullets[i].active = 1;
                        player_bullets[i].visible = 1;
                        player_bullets[i].speed = 3;
                        player_bullets[i].dx = directions[dir][0];
                        player_bullets[i].dy = directions[dir][1];
                        player_bullets[i].shape = SHAPE_SOLID_SQUARE; // 实心方块
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
}

// 优化的子弹更新 - 批量处理且减少闪烁
void update_player_bullets(void) {
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!player_bullets[i].active) continue;
        
        // 记录前一帧位置
        player_bullets[i].prev_x = player_bullets[i].x;
        player_bullets[i].prev_y = player_bullets[i].y;
        
        // 更新位置
        player_bullets[i].x += player_bullets[i].dx * player_bullets[i].speed;
        player_bullets[i].y += player_bullets[i].dy * player_bullets[i].speed;
        
        // 屏幕外检查
        if (player_bullets[i].y < -10 || player_bullets[i].y > SCREEN_HEIGHT + 10 ||
            player_bullets[i].x < -10 || player_bullets[i].x > SCREEN_WIDTH + 10) {
            player_bullets[i].active = 0;
            player_bullets[i].visible = 0;
        } else {
            player_bullets[i].visible = 1; // 标记需要重绘
        }
    }
}

// 更新追踪子弹的函数 - 优化版本
void update_tracking_bullets(void) {
    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) {
        if (!tracking_bullets[i].active) continue;
        
        // 记录前一帧位置
        tracking_bullets[i].prev_x = tracking_bullets[i].x;
        tracking_bullets[i].prev_y = tracking_bullets[i].y;
        
        // 减少生存时间
        tracking_bullets[i].lifetime--;
        if (tracking_bullets[i].lifetime <= 0) {
            tracking_bullets[i].active = 0;
            tracking_bullets[i].visible = 0;
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
        if (tracking_bullets[i].x < -10 || tracking_bullets[i].x > SCREEN_WIDTH + 10 ||
            tracking_bullets[i].y < -10 || tracking_bullets[i].y > SCREEN_HEIGHT + 10) {
            tracking_bullets[i].active = 0;
            tracking_bullets[i].visible = 0;
        } else {
            tracking_bullets[i].visible = 1;
        }
    }
}

// Update enemies with movement and random spawning - 优化版本
void update_enemies(void) {
    static int spawn_timer = 0;
    static int formation_timer = 0;
    
    spawn_timer++;
    formation_timer++;
    
    // Spawn individual enemies randomly
    if (spawn_timer >= 60) {  // Every 1 second at 60 FPS
        spawn_timer = 0;
        if (simple_rand() % 100 < 40) {  // 40% chance to spawn
            spawn_random_enemy();
        }
    }
    
    // Spawn enemy formations occasionally
    if (formation_timer >= 300) {  // Every 5 seconds
        formation_timer = 0;
        if (simple_rand() % 100 < 30) {  // 30% chance to spawn formation
            spawn_enemy_formation();
        }
    }
    
    // Update enemy positions
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) continue;
        
        // 记录前一帧位置
        enemies[i].prev_x = enemies[i].x;
        enemies[i].prev_y = enemies[i].y;
        
        // Move enemy
        enemies[i].y += enemies[i].speed;
        
        // Remove enemy if it goes off screen
        if (enemies[i].y > SCREEN_HEIGHT + 10) {
            enemies[i].active = 0;
            enemies[i].visible = 0;
            continue;
        }
        
        if (enemies[i].y < -20) {
            enemies[i].active = 0;
            enemies[i].visible = 0;
            continue;
        }
        
        // Add some horizontal movement for variety
        if (simple_rand() % 100 < 3) {  // 3% chance to change direction
            int direction = (simple_rand() % 3) - 1;  // -1, 0, or 1
            enemies[i].x += direction;
            
            // Keep enemies on screen
            if (enemies[i].x < 0) enemies[i].x = 0;
            if (enemies[i].x > SCREEN_WIDTH - ENEMY_SIZE) enemies[i].x = SCREEN_WIDTH - ENEMY_SIZE;
        }
        
        enemies[i].visible = 1; // 标记需要重绘
    }
}

// 优化的敌人子弹系统
void update_enemy_bullets(void) {
    // 计算当前子弹数量，控制最大数量
    int active_enemy_bullets = 0;
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active) active_enemy_bullets++;
    }
    
    if (active_enemy_bullets < 100) { // 限制敌人子弹数量，提高性能
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (!enemies[i].active) continue;
            
            enemies[i].shoot_timer++;
            
            int shoot_interval;
            switch (enemies[i].type) {
                case 0: shoot_interval = 180; break; // 降低射击频率
                case 1: shoot_interval = 120; break;
                case 2: shoot_interval = 240; break;
                default: shoot_interval = 180; break;
            }
            
            if (enemies[i].shoot_timer >= shoot_interval) {
                enemies[i].shoot_timer = 0;
                
                // 发射子弹，形态与敌人相同
                for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                    if (!enemy_bullets[j].active) {
                        enemy_bullets[j].x = enemies[i].x + ENEMY_SIZE / 2;
                        enemy_bullets[j].y = enemies[i].y + ENEMY_SIZE;
                        enemy_bullets[j].prev_x = enemy_bullets[j].x;
                        enemy_bullets[j].prev_y = enemy_bullets[j].y;
                        enemy_bullets[j].active = 1;
                        enemy_bullets[j].visible = 1;
                        enemy_bullets[j].speed = 2;
                        enemy_bullets[j].dx = 0;
                        enemy_bullets[j].dy = 1;
                        enemy_bullets[j].shape = enemies[i].shape; // 子弹形态与敌人相同
                        break; // 只发射一发
                    }
                }
            }
        }
    }
    
    // 批量更新敌人子弹
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (!enemy_bullets[i].active) continue;
        
        // 记录前一帧位置
        enemy_bullets[i].prev_x = enemy_bullets[i].x;
        enemy_bullets[i].prev_y = enemy_bullets[i].y;
        
        enemy_bullets[i].x += enemy_bullets[i].dx * enemy_bullets[i].speed;
        enemy_bullets[i].y += enemy_bullets[i].dy * enemy_bullets[i].speed;
        
        if (enemy_bullets[i].y > SCREEN_HEIGHT + 10 || enemy_bullets[i].x < -10 || 
            enemy_bullets[i].x > SCREEN_WIDTH + 10 || enemy_bullets[i].y < -10) {
            enemy_bullets[i].active = 0;
            enemy_bullets[i].visible = 0;
        } else {
            enemy_bullets[i].visible = 1;
        }
    }
}

// 优化的碰撞检测 - 使用空间分割
void check_collisions(void) {
    // 玩家子弹与敌人的碰撞检测
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (!player_bullets[i].active) continue;
        
        // 屏幕外的子弹不参与碰撞检测
        if (player_bullets[i].x < 0 || player_bullets[i].x > SCREEN_WIDTH ||
            player_bullets[i].y < 0 || player_bullets[i].y > SCREEN_HEIGHT) {
            continue;
        }
        
        for (int j = 0; j < MAX_ENEMIES; j++) {
            if (!enemies[j].active) continue;
            
            // 简化的碰撞检测
            if (player_bullets[i].x >= enemies[j].x && 
                player_bullets[i].x < enemies[j].x + ENEMY_SIZE &&
                player_bullets[i].y >= enemies[j].y && 
                player_bullets[i].y < enemies[j].y + ENEMY_SIZE) {
                
                // 碰撞处理
                player_bullets[i].active = 0;
                player_bullets[i].visible = 0;
                enemies[j].active = 0;
                enemies[j].visible = 0;
                
                // 10% 概率生成新敌人
                if (simple_rand() % 100 < 10) {
                    spawn_random_enemy();
                }
                break; // 子弹已销毁，跳出内层循环
            }
        }
    }
    
    // 追踪子弹与敌人的碰撞检测
    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) {
        if (!tracking_bullets[i].active) continue;
        
        for (int j = 0; j < MAX_ENEMIES; j++) {
            if (!enemies[j].active) continue;
            
            // 简化的碰撞检测
            if (tracking_bullets[i].x < enemies[j].x + ENEMY_SIZE &&
                tracking_bullets[i].x + TRACKING_BULLET_SIZE > enemies[j].x &&
                tracking_bullets[i].y < enemies[j].y + ENEMY_SIZE &&
                tracking_bullets[i].y + TRACKING_BULLET_SIZE > enemies[j].y) {
                
                // 碰撞处理
                tracking_bullets[i].active = 0;
                tracking_bullets[i].visible = 0;
                enemies[j].active = 0;
                enemies[j].visible = 0;
                
                // 15% 概率生成新敌人
                if (simple_rand() % 100 < 15) {
                    spawn_random_enemy();
                }
                break;
            }
        }
    }
    
    // 敌人子弹与玩家的碰撞检测
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (!enemy_bullets[i].active) continue;
        
        // 简化的碰撞检测
        if (enemy_bullets[i].x >= player.x && 
            enemy_bullets[i].x < player.x + PLAYER_SIZE &&
            enemy_bullets[i].y >= player.y && 
            enemy_bullets[i].y < player.y + PLAYER_SIZE) {
            
            enemy_bullets[i].active = 0;
            enemy_bullets[i].visible = 0;
            // 这里可以添加玩家受伤逻辑
        }
    }
}

// 统一的渲染函数 - 减少闪烁
void render_all_objects(void) {
    // 1. 清除所有需要清除的对象
    
    // 清除玩家旧位置（如果位置改变）
    if (player.prev_x != player.x || player.prev_y != player.y) {
        draw_rect_fast(player.prev_x, player.prev_y, PLAYER_SIZE, PLAYER_SIZE, BLACK);
    }
    
    // 清除玩家子弹旧位置
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (player_bullets[i].visible && 
            (player_bullets[i].prev_x != player_bullets[i].x || player_bullets[i].prev_y != player_bullets[i].y || !player_bullets[i].active)) {
            clear_shape_fast(player_bullets[i].prev_x, player_bullets[i].prev_y, BULLET_SIZE, player_bullets[i].shape);
        }
    }
    
    // 清除敌人子弹旧位置
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].visible && 
            (enemy_bullets[i].prev_x != enemy_bullets[i].x || enemy_bullets[i].prev_y != enemy_bullets[i].y || !enemy_bullets[i].active)) {
            clear_shape_fast(enemy_bullets[i].prev_x, enemy_bullets[i].prev_y, BULLET_SIZE, enemy_bullets[i].shape);
        }
    }
    
    // 清除追踪子弹旧位置
    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) {
        if (tracking_bullets[i].visible && 
            (tracking_bullets[i].prev_x != tracking_bullets[i].x || tracking_bullets[i].prev_y != tracking_bullets[i].y || !tracking_bullets[i].active)) {
            LCD_DrawRectangle(tracking_bullets[i].prev_x, tracking_bullets[i].prev_y, 
                             tracking_bullets[i].prev_x + TRACKING_BULLET_SIZE - 1, 
                             tracking_bullets[i].prev_y + TRACKING_BULLET_SIZE - 1, BLACK);
        }
    }
    
    // 清除敌人旧位置
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].visible && 
            (enemies[i].prev_x != enemies[i].x || enemies[i].prev_y != enemies[i].y || !enemies[i].active)) {
            clear_shape_fast(enemies[i].prev_x, enemies[i].prev_y, ENEMY_SIZE, enemies[i].shape);
        }
    }
    
    // 2. 绘制所有活跃对象
    
    // 绘制玩家
    if (player.visible) {
        draw_rect_fast(player.x, player.y, PLAYER_SIZE, PLAYER_SIZE, BLUE);
        player.visible = 0; // 重绘后清除标记
    }
    
    // 绘制玩家子弹
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (player_bullets[i].active && player_bullets[i].visible) {
            u16 bullet_color = (player_bullets[i].dy == -1 && player_bullets[i].dx == 0) ? WHITE : CYAN;
            draw_shape_fast(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, player_bullets[i].shape, bullet_color);
            player_bullets[i].visible = 0; // 重绘后清除标记
        }
    }
    
    // 绘制敌人子弹
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active && enemy_bullets[i].visible) {
            draw_shape_fast(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, enemy_bullets[i].shape, YELLOW);
            enemy_bullets[i].visible = 0; // 重绘后清除标记
        }
    }
    
    // 绘制追踪子弹
    for (int i = 0; i < MAX_TRACKING_BULLETS; i++) {
        if (tracking_bullets[i].active && tracking_bullets[i].visible) {
            LCD_DrawRectangle(tracking_bullets[i].x, tracking_bullets[i].y, 
                             tracking_bullets[i].x + TRACKING_BULLET_SIZE - 1, 
                             tracking_bullets[i].y + TRACKING_BULLET_SIZE - 1, GREEN);
            tracking_bullets[i].visible = 0; // 重绘后清除标记
        }
    }
    
    // 绘制敌人
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active && enemies[i].visible) {
            u16 enemy_color;
            switch (enemies[i].type) {
                case 0: enemy_color = RED; break;
                case 1: enemy_color = MAGENTA; break;
                case 2: enemy_color = YELLOW; break;
                default: enemy_color = RED; break;
            }
            draw_shape_fast(enemies[i].x, enemies[i].y, ENEMY_SIZE, enemies[i].shape, enemy_color);
            enemies[i].visible = 0; // 重绘后清除标记
        }
    }
}

// Main game loop - 优化版本
void game_loop(int difficulty) {
    init_game();
    
    // Clear screen once
    LCD_Clear(BLACK);
    
    while (1) {  // Infinite loop - no exit mechanism
        // Increment frame counter
        frame_counter++;
        
        // Update performance counters (less frequent)
        if (frame_counter % 2 == 0) {  // Every other frame
            update_fps_counter();
            update_entity_counter();
        }
        
        // Update game logic
        update_player();
        update_player_bullets();
        update_tracking_bullets();
        update_enemies();
        update_enemy_bullets();
        
        // Check collisions
        check_collisions();
        
        // Render everything in one pass
        render_all_objects();
        
        // Draw performance counters (less frequently)
        if (frame_counter % 10 == 0) {  // Every 10 frames
            draw_performance_counters();
        }
        
        // Frame rate control - stable 60 FPS
        delay_1ms(16); // 60 FPS target
    }
}

int main(void) {
  IO_init();
  
  int difficulty = select();  // Call assembly function
  
  // Start game based on difficulty
  game_loop(difficulty);
  
  return 0;
}