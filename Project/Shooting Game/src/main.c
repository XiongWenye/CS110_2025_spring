#include "lcd/lcd.h"
#include "utils.h"
#include "assembly/example.h"
#include "assembly/select.h"

// Game constants - 优化的数量
#define SCREEN_WIDTH 160
#define SCREEN_HEIGHT 80
#define PLAYER_SIZE 4
#define ENEMY_SIZE 3
#define BULLET_SIZE 2
#define MAX_BULLETS 200         // 200个玩家子弹
#define MAX_ENEMY_BULLETS 200   // 200个敌人子弹
#define MAX_ENEMIES 15          // 15个敌人

// FPS and entity counting
#define FPS_SAMPLE_FRAMES 8     // Sample over 8 frames (0.133s at 60fps)
#define ENTITY_UPDATE_INTERVAL 4  // Update entity counter every 4 frames

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
    int type;
} Enemy;

// Game state
Player player;
Bullet player_bullets[MAX_BULLETS];
Bullet enemy_bullets[MAX_ENEMY_BULLETS];
Enemy enemies[MAX_ENEMIES];

// Performance counters
static unsigned long frame_times[FPS_SAMPLE_FRAMES];
static int frame_index = 0;
static int entity_count = 0;
static int displayed_fps = 60;
static int displayed_entities = 0;
static int entity_update_counter = 0;

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

// Get current time in milliseconds (simple approximation)
unsigned long get_time_ms(void) {
    static unsigned long time_counter = 0;
    time_counter += 16; // Assume 16ms per frame
    return time_counter;
}

// Update FPS counter
void update_fps_counter(void) {
    static unsigned long last_time = 0;
    unsigned long current_time = get_time_ms();
    
    // Store frame time
    frame_times[frame_index] = current_time - last_time;
    frame_index = (frame_index + 1) % FPS_SAMPLE_FRAMES;
    last_time = current_time;
    
    // Calculate average FPS every FPS_SAMPLE_FRAMES
    if (frame_index == 0) {
        unsigned long total_time = 0;
        for (int i = 0; i < FPS_SAMPLE_FRAMES; i++) {
            total_time += frame_times[i];
        }
        
        if (total_time > 0) {
            displayed_fps = (FPS_SAMPLE_FRAMES * 1000) / total_time;
        }
    }
}

// Count active entities
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

// Update entity counter (less frequently for performance)
void update_entity_counter(void) {
    entity_update_counter++;
    if (entity_update_counter >= ENTITY_UPDATE_INTERVAL) {
        entity_update_counter = 0;
        displayed_entities = count_active_entities();
    }
}

// Draw performance counters
void draw_performance_counters(void) {
    // Clear counter area (top-right corner)
    for (int x = 120; x < SCREEN_WIDTH; x++) {
        for (int y = 0; y < 25; y++) {
            LCD_DrawPoint(x, y, BLACK);
        }
    }
    
    // Draw FPS counter
    LCD_ShowString(125, 5, (u8*)"FPS:", WHITE);
    LCD_ShowNum(125, 15, displayed_fps, 2, WHITE);
    
    // Draw entity counter
    LCD_ShowString(125, 35, (u8*)"ENT:", WHITE);
    LCD_ShowNum(125, 45, displayed_entities, 3, WHITE);
}

// Safe LCD drawing function with bounds checking
void safe_draw_pixel(int x, int y, u16 color) {
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        LCD_DrawPoint(x, y, color);
    }
}

// Optimized rectangle drawing - only draw outline for better performance
void draw_rect_optimized(int x, int y, int width, int height, u16 color) {
    // For bullets (small objects), draw solid
    if (width <= 2 && height <= 2) {
        for (int i = 0; i < width; i++) {
            for (int j = 0; j < height; j++) {
                safe_draw_pixel(x + i, y + j, color);
            }
        }
    } else {
        // For larger objects, draw outline only
        for (int i = 0; i < width; i++) {
            safe_draw_pixel(x + i, y, color);
            safe_draw_pixel(x + i, y + height - 1, color);
        }
        for (int j = 0; j < height; j++) {
            safe_draw_pixel(x, y + j, color);
            safe_draw_pixel(x + width - 1, y + j, color);
        }
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
    
    // Initialize enemies
    for (int i = 0; i < MAX_ENEMIES; i++) {
        enemies[i].active = 0;
    }
    
    // Generate initial enemies
    for (int i = 0; i < 8; i++) {
        spawn_random_enemy();
    }
    
    // Initialize performance counters
    for (int i = 0; i < FPS_SAMPLE_FRAMES; i++) {
        frame_times[i] = 16; // Assume 16ms initially
    }
}

// Spawn a random enemy
void spawn_random_enemy(void) {
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (!enemies[i].active) {
            enemies[i].x = simple_rand() % (SCREEN_WIDTH - ENEMY_SIZE);
            enemies[i].y = -(simple_rand() % 20 + 5);
            enemies[i].active = 1;
            enemies[i].speed = 1 + simple_rand() % 2;  // Speed 1-2
            enemies[i].shoot_timer = simple_rand() % 60;
            enemies[i].type = simple_rand() % 3;  // 3 types (0-2)
            break;
        }
    }
}

// Handle player input and movement
void update_player(void) {
    static int prev_x = -1, prev_y = -1;
    static int button1_debounce = 0;
    static int button2_debounce = 0;
    static int debounce_counter = 0;
    
    if (debounce_counter > 0) {
        debounce_counter--;
    }
    
    // Clear previous player position
    if (prev_x >= 0 && prev_y >= 0) {
        clear_rect(prev_x, prev_y, PLAYER_SIZE, PLAYER_SIZE);
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

    // Handle BUTTON_1 - rapid fire
    if (Get_Button(BUTTON_1)) {
        if (button1_debounce == 0) {
            button1_debounce = 4;  // Reduced for higher fire rate
            debounce_counter = 1;
            
            // Fire 2 bullets for better performance
            for (int burst = 0; burst < 2; burst++) {
                for (int i = 0; i < MAX_BULLETS; i++) {
                    if (!player_bullets[i].active) {
                        player_bullets[i].x = player.x + PLAYER_SIZE / 2 + burst - 1;
                        player_bullets[i].y = player.y - burst;
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

    // Handle BUTTON_2 - spread shot (reduced spread for performance)
    if (Get_Button(BUTTON_2)) {
        if (button2_debounce == 0) {
            button2_debounce = 20;
            debounce_counter = 5;
            
            // 8 directions instead of 16 for better performance
            int directions[8][2] = {
                {0, -1}, {1, -1}, {1, 0}, {1, 1},
                {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}
            };
            
            int bullets_fired = 0;
            for (int dir = 0; dir < 8 && bullets_fired < 8; dir++) {
                for (int i = 0; i < MAX_BULLETS && bullets_fired < 8; i++) {
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

// Update enemies with optimized spawning
void update_enemies(void) {
    static int spawn_timer = 0;
    
    spawn_timer++;
    
    // Spawn enemies less frequently for better performance
    if (spawn_timer >= 45) {  // Every 0.75 seconds
        spawn_timer = 0;
        if (simple_rand() % 100 < 25) {  // 25% chance
            spawn_random_enemy();
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
            
            // Simple horizontal movement
            if (simple_rand() % 100 < 3) {  // Reduced probability
                int direction = (simple_rand() % 3) - 1;
                enemies[i].x += direction * 2;
                
                if (enemies[i].x < 0) enemies[i].x = 0;
                if (enemies[i].x > SCREEN_WIDTH - ENEMY_SIZE) enemies[i].x = SCREEN_WIDTH - ENEMY_SIZE;
            }
        }
    }
    
    // Draw enemies
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            u16 enemy_color;
            switch (enemies[i].type) {
                case 0: enemy_color = RED; break;
                case 1: enemy_color = MAGENTA; break;
                case 2: enemy_color = YELLOW; break;
                default: enemy_color = RED; break;
            }
            draw_rect_optimized(enemies[i].x, enemies[i].y, ENEMY_SIZE, ENEMY_SIZE, enemy_color);
        }
    }
}

// Update enemy bullets with optimized shooting
void update_enemy_bullets(void) {
    // Enemy shooting logic
    for (int i = 0; i < MAX_ENEMIES; i++) {
        if (enemies[i].active) {
            enemies[i].shoot_timer++;
            
            int shoot_interval;
            switch (enemies[i].type) {
                case 0: shoot_interval = 90; break;  // Slower shooting for performance
                case 1: shoot_interval = 70; break;
                case 2: shoot_interval = 110; break;
                default: shoot_interval = 90; break;
            }
            
            if (enemies[i].shoot_timer >= shoot_interval) {
                enemies[i].shoot_timer = 0;
                
                if (enemies[i].type == 2) {
                    // Type 2: 3-way spread
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
                } else {
                    // Normal single shot
                    for (int j = 0; j < MAX_ENEMY_BULLETS; j++) {
                        if (!enemy_bullets[j].active) {
                            enemy_bullets[j].x = enemies[i].x + ENEMY_SIZE / 2;
                            enemy_bullets[j].y = enemies[i].y + ENEMY_SIZE;
                            enemy_bullets[j].active = 1;
                            enemy_bullets[j].speed = 2;
                            enemy_bullets[j].dx = 0;
                            enemy_bullets[j].dy = 1;
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

// Optimized collision detection
void check_collisions(void) {
    // Check player bullets hitting enemies
    for (int i = 0; i < MAX_BULLETS; i++) {
        if (player_bullets[i].active) {
            for (int j = 0; j < MAX_ENEMIES; j++) {
                if (enemies[j].active) {
                    if (player_bullets[i].x >= enemies[j].x && 
                        player_bullets[i].x < enemies[j].x + ENEMY_SIZE &&
                        player_bullets[i].y >= enemies[j].y && 
                        player_bullets[i].y < enemies[j].y + ENEMY_SIZE) {
                        
                        // Clear both positions first
                        clear_rect(player_bullets[i].x, player_bullets[i].y, BULLET_SIZE, BULLET_SIZE);
                        clear_rect(enemies[j].x, enemies[j].y, ENEMY_SIZE, ENEMY_SIZE);
                        
                        // Deactivate
                        player_bullets[i].active = 0;
                        enemies[j].active = 0;
                        
                        // Chance to spawn new enemy
                        if (simple_rand() % 100 < 15) {
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

// Main game loop
void game_loop(int difficulty) {
    init_game();
    
    // Clear screen
    LCD_Clear(BLACK);
    
    while (1) {
        // Update performance counters
        update_fps_counter();
        update_entity_counter();
        
        // Check collisions BEFORE updating bullets
        check_collisions();
        
        // Update game objects
        update_player();
        update_player_bullets();
        update_enemies();
        update_enemy_bullets();
        
        // Draw performance counters
        draw_performance_counters();
        
        // Frame rate control - target 30+ FPS
        delay_1ms(20); // 50 FPS target, should maintain >30 FPS
    }
}

int main(void) {
  IO_init();
  
  int difficulty = select();
  
  game_loop(difficulty);
  
  return 0;
}