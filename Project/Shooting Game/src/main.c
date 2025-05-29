#include "lcd/lcd.h"
#include "utils.h"
#include "assembly/example.h"
#include "assembly/select.h"

// Game constants - 大幅增加数量
#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 80
#define PLAYER_SIZE 4
#define ENEMY_SIZE 3
#define BULLET_SIZE 2
#define MAX_BULLETS 300         // 增加到300个玩家子弹
#define MAX_ENEMY_BULLETS 300   // 增加到300个敌人子弹
#define MAX_ENEMIES 20          // 增加到20个敌人

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
}

// Spawn a random enemy
void spawn_random_enemy(void) {
    // Find an inactive enemy slot
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            // Random spawn position at top of screen
            enemies[i].x = simple_rand() % (SCREEN_WIDTH - ENEMY_SIZE);
            enemies[i].y = -(simple_rand() % 20 + 5);  // Spawn above screen
            enemies[i].active = 1;
            enemies[i].speed = 1 + simple_rand() % 3;  // Random speed 1-3
            enemies[i].shoot_timer = simple_rand() % 60;  // Random initial timer
            enemies[i].type = simple_rand() % 4;  // 四种类型的敌人 (0-3)
            break;
        }
    }
}

// Spawn enemy formation
void spawn_enemy_formation(void) {
    // Spawn a formation of 3-5 enemies
    int formation_size = 3 + simple_rand() % 3;  // 3-5 enemies
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
                enemies[i].active = 0;
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
                        
                        // Hit! Remove bullet and enemy
                        player_bullets[i].active = 0;
                        enemies[j].active = 0;
                        
                        // Clear enemy from screen
                        clear_rect(enemies[j].x, enemies[j].y, ENEMY_SIZE, ENEMY_SIZE);
                        
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
                
                // Hit! Remove bullet
                enemy_bullets[i].active = 0;
                clear_rect(enemy_bullets[i].x, enemy_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
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
        // Update game objects
        update_player();
        update_player_bullets();
        update_enemies();
        update_enemy_bullets();
        check_collisions();
        
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