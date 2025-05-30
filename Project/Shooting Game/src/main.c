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
#define MAX_ENEMY_BULLETS 500   // 增加到500个敌人子弹
#define MAX_ENEMIES 200         

// FPS and entity counting constants
#define FPS_SAMPLE_FRAMES 8     // Sample over 8 frames (0.133s at 60fps)
#define ENTITY_UPDATE_INTERVAL 3  // Update entity counter every 4 frames

// Performance optimization constants
#define MAX_DIRTY_RECTS 200
#define COLLISION_CHECK_INTERVAL 2  // Check collisions every 2 frames
#define BULLET_UPDATE_INTERVAL 1    // Update bullets every frame, but optimize

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
    int last_x, last_y;  // 记录上一帧位置，用于脏矩形
} Bullet;

// Enemy structure
typedef struct {
    int x, y;
    int active;
    int speed;
    int shoot_timer;
    int type;  // 新增：敌人类型
    int last_x, last_y;  // 记录上一帧位置
} Enemy;

// Dirty rectangle for partial screen updates
typedef struct {
    int x, y, width, height;
    int active;
} DirtyRect;

// Game state
Player player;
Bullet player_bullets[MAX_BULLETS];
Bullet enemy_bullets[MAX_ENEMY_BULLETS];
Enemy enemies[MAX_ENEMIES];

// Dirty rectangle system
static DirtyRect dirty_rects[MAX_DIRTY_RECTS];
static int dirty_rect_count = 0;

// Random number generator state
static unsigned int rand_seed = 12345;

// Performance counters
static unsigned long frame_times[FPS_SAMPLE_FRAMES];
static int frame_index = 0;
static int displayed_fps = 60;
static int displayed_entities = 0;
static int entity_update_counter = 0;
static unsigned long frame_counter = 0;
static int last_displayed_fps = -1;
static int last_displayed_entities = -1;
static uint64_t fps_measurement_start_time = 0;
static uint32_t fps_frame_count = 0;

// Performance optimization variables
static int collision_check_counter = 0;
static int bullet_update_counter = 0;
static int performance_update_counter = 0;

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

// 新增：快速矩形填充函数 - 利用LCD_Fill硬件加速
void LCD_FillRect_Fast(u16 x, u16 y, u16 width, u16 height, u16 color) {
    // 边界检查
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT || width == 0 || height == 0) return;
    
    // 计算实际绘制区域
    u16 end_x = (x + width > SCREEN_WIDTH) ? SCREEN_WIDTH - 1 : x + width - 1;
    u16 end_y = (y + height > SCREEN_HEIGHT) ? SCREEN_HEIGHT - 1 : y + height - 1;
    
    if (x <= end_x && y <= end_y) {
        LCD_Fill(x, y, end_x, end_y, color);
    }
}

// 新增：批量绘制点的优化函数
void LCD_DrawPoint_Batch(u16 *x_coords, u16 *y_coords, u16 *colors, int count) {
    for (int i = 0; i < count; i++) {
        if (x_coords[i] < SCREEN_WIDTH && y_coords[i] < SCREEN_HEIGHT) {
            LCD_DrawPoint(x_coords[i], y_coords[i], colors[i]);
        }
    }
}

// Dirty rectangle management
void add_dirty_rect(int x, int y, int width, int height) {
    if (dirty_rect_count < MAX_DIRTY_RECTS) {
        dirty_rects[dirty_rect_count].x = x;
        dirty_rects[dirty_rect_count].y = y;
        dirty_rects[dirty_rect_count].width = width;
        dirty_rects[dirty_rect_count].height = height;
        dirty_rects[dirty_rect_count].active = 1;
        dirty_rect_count++;
    }
}

void clear_dirty_rects(void) {
    for (int i = 0; i < dirty_rect_count; i++) {
        if (dirty_rects[i].active) {
            // 使用硬件加速的LCD_Fill清除脏区域
            LCD_FillRect_Fast(dirty_rects[i].x, dirty_rects[i].y, 
                             dirty_rects[i].width, dirty_rects[i].height, BLACK);
        }
    }
    dirty_rect_count = 0;
}

// 真实FPS计数器 - 使用get_timer_value()测量
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
            if (displayed_fps < 1) displayed_fps = 1;
        }
        
        fps_measurement_start_time = current_time;
        fps_frame_count = 0;
    }
}

// 超级优化的绘制函数 - 使用硬件加速
void draw_rect_ultra_fast(int x, int y, int width, int height, u16 color) {
    LCD_FillRect_Fast(x, y, width, height, color);
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

void update_entity_counter_optimized(void) {
    entity_update_counter++;
    if (entity_update_counter >= 30) {  // 每30帧更新一次，减少频率
        entity_update_counter = 0;
        displayed_entities = count_active_entities();
        if (displayed_entities > 999) displayed_entities = 999;
    }
}

void draw_performance_counters(void) {
    if (displayed_fps != last_displayed_fps) {
        // 使用硬件加速清除FPS区域
        LCD_FillRect_Fast(SCREEN_WIDTH - 40, 0, 40, 25, BLACK);
        
        LCD_ShowString(SCREEN_WIDTH - 35, 5, (u8*)"FPS", WHITE);
        LCD_ShowNum(SCREEN_WIDTH - 20, 15, displayed_fps, 2, WHITE);
        last_displayed_fps = displayed_fps;
    }
    
    if (displayed_entities != last_displayed_entities) {
        // 使用硬件加速清除实体计数区域
        LCD_FillRect_Fast(SCREEN_WIDTH - 40, 25, 40, 25, BLACK);
        
        LCD_ShowString(SCREEN_WIDTH - 35, 30, (u8*)"ENT", WHITE);
        LCD_ShowNum(SCREEN_WIDTH - 25, 40, displayed_entities, 3, WHITE);
        last_displayed_entities = displayed_entities;
    }
}

void safe_draw_pixel(int x, int y, u16 color) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        LCD_DrawPoint(x, y, color);
    }
}

void clear_rect(int x, int y, int width, int height) {
    draw_rect_ultra_fast(x, y, width, height, BLACK);
}

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
        player_bullets[i].last_x = -1;
        player_bullets[i].last_y = -1;
    }
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        enemy_bullets[i].active = 0;
        enemy_bullets[i].dx = 0;
        enemy_bullets[i].dy = 0;
        enemy_bullets[i].last_x = -1;
        enemy_bullets[i].last_y = -1;
    }
    
    // Initialize enemies
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = 0;
        enemies[i].last_x = -1;
        enemies[i].last_y = -1;
    }
    
    // 生成初始敌人
    for (int i = 0; i < 5; i++) {  // 减少初始敌人数量
        spawn_random_enemy();
    }
    
    // Initialize performance counters
    for (int i = 0; i < FPS_SAMPLE_FRAMES; i++) {
        frame_times[i] = 16;
    }
    frame_counter = 0;
    dirty_rect_count = 0;
}

void spawn_random_enemy(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            enemies[i].x = simple_rand() % (SCREEN_WIDTH - ENEMY_SIZE);
            enemies[i].y = -(simple_rand() % 10 + 5);
            enemies[i].active = 1;
            enemies[i].speed = 1 + simple_rand() % 2;  // 减少速度范围
            enemies[i].shoot_timer = simple_rand() % 80 + 40;
            enemies[i].type = simple_rand() % 4;
            enemies[i].last_x = -1;
            enemies[i].last_y = -1;
            break;
        }
    }
}

void spawn_enemy_formation(void) {
    int formation_size = 3 + simple_rand() % 3;  // 减少formation大小
    int start_x = simple_rand() % (SCREEN_WIDTH - formation_size * 15);
    
    for (int i = 0; i < formation_size; i++) {
        for (int j = 0; j < MAX_ENEMIES; j++) {
            if (!enemies[j].active) {
                enemies[j].x = start_x + i * 15;
                enemies[j].y = -(simple_rand() % 10 + 5);
                enemies[j].active = 1;
                enemies[j].speed = 1;
                enemies[j].shoot_timer = simple_rand() % 60;
                enemies[j].type = simple_rand() % 4;
                enemies[j].last_x = -1;
                enemies[j].last_y = -1;
                break;
            }
        }
    }
}

void update_player(void) {
    static int prev_x = -1, prev_y = -1;
    static int button1_debounce = 0;
    static int button2_debounce = 0;
    static int debounce_counter = 0;
    
    if (debounce_counter > 0) {
        debounce_counter--;
    }
    
    // Add previous position to dirty rects
    if (prev_x >= 0 && prev_y >= 0) {
        add_dirty_rect(prev_x, prev_y, PLAYER_SIZE, PLAYER_SIZE);
    }
    
    // Handle movement
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

    // Handle BUTTON_1 - 减少射击频率
    if (Get_Button(BUTTON_1)) {
        if (button1_debounce == 0) {
            button1_debounce = 8;  // 增加防抖动时间，降低射击频率
            debounce_counter = 3;
            
            // 发射单发子弹
            for (int i = 0; i < MAX_BULLETS; i++) {
                if (!player_bullets[i].active) {
                    player_bullets[i].x = player.x + PLAYER_SIZE / 2;
                    player_bullets[i].y = player.y;
                    player_bullets[i].active = 1;
                    player_bullets[i].speed = 4;
                    player_bullets[i].dx = 0;
                    player_bullets[i].dy = -1;
                    player_bullets[i].last_x = -1;
                    player_bullets[i].last_y = -1;
                    break;
                }
            }
        }
    } else {
        button1_debounce = 0;
    }

    // Handle BUTTON_2 - 减少扩散射击数量和频率
    if (Get_Button(BUTTON_2)) {
        if (button2_debounce == 0) {
            button2_debounce = 25;  // 大幅增加防抖动时间
            debounce_counter = 10;
            
            // 减少到5方向，降低复杂度
            int directions[5][2] = {
                {0, -1}, {1, -1}, {-1, -1}, {1, 0}, {-1, 0}
            };
            
            int bullets_fired = 0;
            for (int dir = 0; dir < 5 && bullets_fired < 5; dir++) {
                for (int i = 0; i < MAX_BULLETS && bullets_fired < 5; i++) {
                    if (!player_bullets[i].active) {
                        player_bullets[i].x = player.x + PLAYER_SIZE / 2;
                        player_bullets[i].y = player.y;
                        player_bullets[i].active = 1;
                        player_bullets[i].speed = 3;
                        player_bullets[i].dx = directions[dir][0];
                        player_bullets[i].dy = directions[dir][1];
                        player_bullets[i].last_x = -1;
                        player_bullets[i].last_y = -1;
                        bullets_fired++;
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
    
    // Draw player using hardware acceleration
    draw_rect_ultra_fast(player.x, player.y, PLAYER_SIZE, PLAYER_SIZE, BLUE);
    
    prev_x = player.x;
    prev_y = player.y;
}

// 极度优化的玩家子弹更新 - 使用批量绘制
void update_player_bullets_ultra_optimized(void) {
    static int skip_counter = 0;
    skip_counter++;
    
    // 准备批量绘制数据
    static u16 bullet_x[MAX_BULLETS], bullet_y[MAX_BULLETS], bullet_colors[MAX_BULLETS];
    int active_bullet_count = 0;
    
    // 每2帧更新一次位置
    if (skip_counter % 2 == 0) {
        for (int i = 0; i < MAX_BULLETS; i++) {
            if (player_bullets[i].active) {
                // 添加旧位置到脏矩形
                add_dirty_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
                
                // 更新位置
                player_bullets[i].x += player_bullets[i].dx * player_bullets[i].speed;
                player_bullets[i].y += player_bullets[i].dy * player_bullets[i].speed;
                
                // 边界检查
                if (player_bullets[i].y < -5 || player_bullets[i].y > SCREEN_HEIGHT + 5 ||
                    player_bullets[i].x < -5 || player_bullets[i].x > SCREEN_WIDTH + 5) {
                    player_bullets[i].active = 0;
                    continue;
                }
            }
        }
    }
    
    // 收集活跃子弹进行批量绘制
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (player_bullets[i].active) {
            if (active_bullet_count < MAX_BULLETS) {
                bullet_x[active_bullet_count] = player_bullets[i].x;
                bullet_y[active_bullet_count] = player_bullets[i].y;
                bullet_colors[active_bullet_count] = (player_bullets[i].dy == -1 && player_bullets[i].dx == 0) ? WHITE : CYAN;
                active_bullet_count++;
            }
        }
    }
    
    // 批量绘制子弹 - 使用硬件加速
    for (int i = 0; i < active_bullet_count; i++) {
        draw_rect_ultra_fast(bullet_x[i], bullet_y[i], BULLET_SIZE, BULLET_SIZE, bullet_colors[i]);
    }
}

void update_enemies(void) {
    static int spawn_timer = 0;
    static int formation_timer = 0;
    
    spawn_timer++;
    formation_timer++;
    
    // 减少敌人生成频率
    if (spawn_timer >= 80) {  // 每1.3秒生成一次
        spawn_timer = 0;
        if (simple_rand() % 100 < 20) {  // 20% 概率
            spawn_random_enemy();
        }
    }
    
    // 减少编队生成频率
    if (formation_timer >= 800) {  // 每13秒
        formation_timer = 0;
        if (simple_rand() % 100 < 10) {  // 10% 概率
            spawn_enemy_formation();
        }
    }
    
    // 批量处理敌人位置更新
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            // 添加脏矩形
            add_dirty_rect(enemies[i].x, enemies[i].y, ENEMY_SIZE, ENEMY_SIZE);
            
            // 移动敌人
            enemies[i].y += enemies[i].speed;
            
            // 边界处理
            if (enemies[i].y > SCREEN_HEIGHT) {
                enemies[i].speed = -enemies[i].speed;
                enemies[i].y = SCREEN_HEIGHT;
            } else if (enemies[i].y < 0) {
                enemies[i].speed = -enemies[i].speed;
                enemies[i].y = 0;
            }
            
            // 减少横向移动频率
            if (simple_rand() % 300 < 1) {  // 0.33% 概率
                int direction = (simple_rand() % 3) - 1;
                enemies[i].x += direction;
                
                if (enemies[i].x < 0) enemies[i].x = 0;
                if (enemies[i].x > SCREEN_WIDTH - ENEMY_SIZE) enemies[i].x = SCREEN_WIDTH - ENEMY_SIZE;
            }
        }
    }
    
    // 批量绘制敌人 - 使用硬件加速
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
            draw_rect_ultra_fast(enemies[i].x, enemies[i].y, ENEMY_SIZE, ENEMY_SIZE, enemy_color);
        }
    }
}

// 极度优化的敌人子弹系统 - 使用批量绘制和更激进的优化
void update_enemy_bullets_ultra_optimized(void) {
    static int update_skip = 0;
    update_skip++;
    
    // 计算当前子弹数量
    int active_count = 0;
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active) active_count++;
    }
    
    // 动态调整更新频率 - 更激进的优化
    int skip_frames = (active_count > 150) ? 4 : (active_count > 75) ? 3 : 2;
    
    // 生成新子弹 - 大幅降低频率
    if (active_count < 1000 && update_skip % 6 == 0) { // 每6帧检查一次
        for (int i = 0; i < MAX_ENEMIES; i++) {
            if (!enemies[i].active) continue;
            
            enemies[i].shoot_timer++;
            
            int shoot_interval = 300; // 大幅增加射击间隔
            
            if (enemies[i].shoot_timer >= shoot_interval) {
                enemies[i].shoot_timer = 0;
                
                // 只发射一发子弹
                for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                    if (!enemy_bullets[j].active) {
                        enemy_bullets[j].x = enemies[i].x + ENEMY_SIZE / 2;
                        enemy_bullets[j].y = enemies[i].y + ENEMY_SIZE;
                        enemy_bullets[j].active = 1;
                        enemy_bullets[j].speed = 2;
                        enemy_bullets[j].dx = 0;
                        enemy_bullets[j].dy = 1;
                        enemy_bullets[j].last_x = -1;
                        enemy_bullets[j].last_y = -1;
                        break;
                    }
                }
            }
        }
    }
    
    // 更新子弹位置 - 使用跳帧
    if (update_skip % skip_frames == 0) {
        for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
            if (!enemy_bullets[i].active) continue;
            
            add_dirty_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
            
            enemy_bullets[i].x += enemy_bullets[i].dx * enemy_bullets[i].speed;
            enemy_bullets[i].y += enemy_bullets[i].dy * enemy_bullets[i].speed;
            
            if (enemy_bullets[i].y > SCREEN_HEIGHT + 5 || enemy_bullets[i].x < -5 || 
                enemy_bullets[i].x > SCREEN_WIDTH + 5 || enemy_bullets[i].y < -5) {
                enemy_bullets[i].active = 0;
            }
        }
    }
    
    // 批量绘制敌人子弹 - 使用硬件加速
    for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
        if (enemy_bullets[i].active) {
            draw_rect_ultra_fast(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE, YELLOW);
        }
    }
}

// 优化的碰撞检测
void check_collisions_ultra_optimized(void) {
    collision_check_counter++;
    
    // 每4帧检查一次碰撞，进一步减少频率
    if (collision_check_counter % 4 != 0) return;
    
    // 玩家子弹 vs 敌人 - 限制检查数量
    int bullet_checks = 0;
    for (int i = 0; i < MAX_BULLETS && bullet_checks < 50; i++) {
        if (!player_bullets[i].active) continue;
        bullet_checks++;
        
        // 简单边界检查
        if (player_bullets[i].x < 0 || player_bullets[i].x > SCREEN_WIDTH ||
            player_bullets[i].y < 0 || player_bullets[i].y > SCREEN_HEIGHT) {
            continue;
        }
        
        int enemy_checks = 0;
        for (int j = 0; j < MAX_ENEMIES && enemy_checks < 20; j++) {
            if (!enemies[j].active) continue;
            enemy_checks++;
            
            // 简化的碰撞检测
            if (player_bullets[i].x < enemies[j].x + ENEMY_SIZE && 
                player_bullets[i].x + BULLET_SIZE > enemies[j].x &&
                player_bullets[i].y < enemies[j].y + ENEMY_SIZE && 
                player_bullets[i].y + BULLET_SIZE > enemies[j].y) {
                
                add_dirty_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
                add_dirty_rect(enemies[j].x, enemies[j].y, ENEMY_SIZE, ENEMY_SIZE);
                
                player_bullets[i].active = 0;
                enemies[j].active = 0;
                
                // 大幅减少新敌人生成概率
                if (simple_rand() % 100 < 3) {
                    spawn_random_enemy();
                }
                break;
            }
        }
    }
    
    // 敌人子弹 vs 玩家 - 限制检查数量
    int enemy_bullet_checks = 0;
    for (int i = 0; i < MAX_ENEMY_BULLETS && enemy_bullet_checks < 30; i++) {
        if (!enemy_bullets[i].active) continue;
        enemy_bullet_checks++;
        
        if (enemy_bullets[i].x < player.x + PLAYER_SIZE && 
            enemy_bullets[i].x + BULLET_SIZE > player.x &&
            enemy_bullets[i].y < player.y + PLAYER_SIZE && 
            enemy_bullets[i].y + BULLET_SIZE > player.y) {
            
            add_dirty_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
            enemy_bullets[i].active = 0;
        }
    }
}

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

// 优化的主游戏循环
void game_loop(int difficulty) {
    init_game();
    LCD_Clear(BLACK);
    
    while (1) {
        frame_counter++;
        performance_update_counter++;
        
        // 减少性能计数器更新频率
        if (performance_update_counter % 20 == 0) {
            update_fps_counter();
            update_entity_counter_optimized();
        }
        
        // 清除脏矩形
        clear_dirty_rects();
        
        // 更新游戏对象
        update_player();
        check_collisions_ultra_optimized();
        update_player_bullets_ultra_optimized();
        update_enemies();
        update_enemy_bullets_ultra_optimized();
        
        // 减少性能显示更新频率
        if (performance_update_counter % 40 == 0) {
            draw_performance_counters();
        }
        
        // 动态帧率控制 - 更激进的优化
        int active_bullets = 0;
        for (int i = 0; i < MAX_ENEMY_BULLETS; i++) {
            if (enemy_bullets[i].active) active_bullets++;
        }
        
        // 根据子弹数量调整延迟
        int frame_delay;
        if (active_bullets > 100) {
            frame_delay = 25;  // ~40 FPS
        } else if (active_bullets > 50) {
            frame_delay = 22;  // ~45 FPS
        } else {
            frame_delay = 20;  // ~50 FPS
        }
        
        delay_1ms(frame_delay);
    }
}

int main(void) {
  IO_init();
  
  int difficulty = select();  // Call assembly function
  
  // Start game based on difficulty
  game_loop(difficulty);
  
  return 0;
}