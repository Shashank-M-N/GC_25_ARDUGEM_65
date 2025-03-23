#include <TFT_22_ILI9225.h>

// Define TFT display pins
#define TFT_RST 8
#define TFT_RS 9
#define TFT_CS 10
#define TFT_LED 11

// Initialize TFT display
TFT_22_ILI9225 display(TFT_RST, TFT_RS, TFT_CS, TFT_LED);

// Rotary encoder pins
#define ENC1_CLK 2
#define ENC1_DT 4
#define ENC2_CLK 3
#define ENC2_DT 5
#define ENC1_SW 6
#define ENC2_SW 7

volatile int encoder1Pos = 0, encoder2Pos = 0;
volatile int encoder1Steps = 0, encoder2Steps = 0;
volatile int lastEncoder1Dir = 0, lastEncoder2Dir = 0;

// Snake direction variables
int dir1X = 1, dir1Y = 0;
int dir2X = -1, dir2Y = 0;

// New threshold variables for each encoder (default value 2)
int threshold1 = 2;
int threshold2 = 2;

// Game display constants
#define MAX_LENGTH 50
#define GRID_SIZE 5
#define SCREEN_WIDTH 176
#define SCREEN_HEIGHT 220

// Define padding areas
#define TOP_PADDING 20     // Top padding (for timer)
#define BOTTOM_PADDING 20  // Bottom padding
// Effective game grid dimensions (without paddings)
#define GRID_WIDTH (SCREEN_WIDTH / GRID_SIZE)
#define GRID_HEIGHT ((SCREEN_HEIGHT - TOP_PADDING - BOTTOM_PADDING) / GRID_SIZE)

// Colors (565 format)
#define COLOR_BROWN 0xA145
#define COLOR_BLACK 0x0000
#define COLOR_WHITE 0xFFFF
#define COLOR_BLUE 0x001F  // Snake 1 body
#define COLOR_RED 0xF800   // Snake 2 body
#define COLOR_GREEN 0x07E0
#define COLOR_GRAY 0x7BEF

// New head colors with better contrast:
#define COLOR_CYAN 0x07FF     // Bright cyan for snake 1 head
#define COLOR_MAGENTA 0xF81F  // Bright magenta for snake 2 head

// Snake data arrays
int snake1X[MAX_LENGTH], snake1Y[MAX_LENGTH];
int snake2X[MAX_LENGTH], snake2Y[MAX_LENGTH];
int snake1Length = 5, snake2Length = 5;
bool gameStarted = false;
bool snake1Wins = false;
bool snake2Wins = false;

// Previous snake positions for incremental updates
int prevSnake1X[MAX_LENGTH], prevSnake1Y[MAX_LENGTH];
int prevSnake2X[MAX_LENGTH], prevSnake2Y[MAX_LENGTH];
int prevSnake1Length = 0, prevSnake2Length = 0;

// Walls data arrays (5 vertical and 5 horizontal walls)
int wallhX[5], wallhY[5];  // Horizontal walls
int wallvX[5], wallvY[5];  // Vertical walls

// Flags for collision update
bool collisionOccurred = false;
unsigned long wallRespawnTimes[10] = { 0 };
const unsigned long wallRespawnDelay = 10000;

unsigned long gameStartTime;               // Stores when the game started
const unsigned long gameDuration = 30000;  // 30 seconds in milliseconds

// Food variables
#define FOOD_COUNT 10
int foodX[FOOD_COUNT], foodY[FOOD_COUNT];

// --- Rotary Encoder ISRs ---
void encoder1ISR() {
  int stateA = digitalRead(ENC1_CLK);
  int stateB = digitalRead(ENC1_DT);
  int direction = (stateA == stateB) ? -1 : 1;

  if (direction != lastEncoder1Dir)
    encoder1Steps = 0;
  encoder1Steps += direction;
  lastEncoder1Dir = direction;

  if (abs(encoder1Steps) >= threshold1) {  // use threshold1 here
    encoder1Pos += direction;
    encoder1Steps = 0;
  }
}

void encoder2ISR() {
  int stateA = digitalRead(ENC2_CLK);
  int stateB = digitalRead(ENC2_DT);
  int direction = (stateA == stateB) ? -1 : 1;

  if (direction != lastEncoder2Dir)
    encoder2Steps = 0;
  encoder2Steps += direction;
  lastEncoder2Dir = direction;

  if (abs(encoder2Steps) >= threshold2) {  // use threshold2 here
    encoder2Pos += direction;
    encoder2Steps = 0;
  }
}

// --- Update Directions Based on Encoder Input ---
void updateDirection() {
  Serial.print("Encoder1: ");
  Serial.println(encoder1Pos);
  Serial.print("Encoder2: ");
  Serial.println(encoder2Pos);

  if (encoder2Pos > 0) {
    int temp = dir2X;
    dir2X = -dir2Y;
    dir2Y = temp;
    encoder2Pos = 0;
  } else if (encoder2Pos < 0) {
    int temp = dir2X;
    dir2X = dir2Y;
    dir2Y = -temp;
    encoder2Pos = 0;
  }

  if (encoder1Pos > 0) {
    int temp = dir1X;
    dir1X = -dir1Y;
    dir1Y = temp;
    encoder1Pos = 0;
  } else if (encoder1Pos < 0) {
    int temp = dir1X;
    dir1X = dir1Y;
    dir1Y = -temp;
    encoder1Pos = 0;
  }
}

// --- Drawing Helper ---
void drawSegment(int gridX, int gridY, uint16_t color) {
  display.fillRectangle(gridX * GRID_SIZE,
                        gridY * GRID_SIZE + TOP_PADDING,
                        (gridX * GRID_SIZE) + (GRID_SIZE - 1),
                        (gridY * GRID_SIZE) + (GRID_SIZE - 1) + TOP_PADDING,
                        color);
}

// Checks if a given position is occupied by a snake, food, or wall.
bool isPositionOccupied(int x, int y) {
  for (int i = 0; i < snake1Length; i++) {
    if (snake1X[i] == x && snake1Y[i] == y)
      return true;
  }
  for (int i = 0; i < snake2Length; i++) {
    if (snake2X[i] == x && snake2Y[i] == y)
      return true;
  }
  for (int i = 0; i < FOOD_COUNT; i++) {
    if (foodX[i] == x && foodY[i] == y)
      return true;
  }
  // Check vertical walls (each covers 3 grid cells)
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 3; j++) {
      if (x == wallvX[i] && y == wallvY[i] + j)
        return true;
    }
  }
  // Check horizontal walls (each covers 3 grid cells)
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 3; j++) {
      if (x == wallhX[i] + j && y == wallhY[i])
        return true;
    }
  }
  return false;
}

// --- Food Functions ---
void spawnFood() {
  for (int i = 0; i < FOOD_COUNT; i++) {
    int x, y;
    bool validPosition;
    do {
      validPosition = true;
      x = random(0, GRID_WIDTH);
      y = random(0, GRID_HEIGHT);

      // Check if the position is occupied by any walls
      for (int j = 0; j < 5; j++) {
        for (int k = 0; k < 3; k++) {
          if ((x == wallvX[j] && y == wallvY[j] + k) || (x == wallhX[j] + k && y == wallhY[j])) {
            validPosition = false;
            break;
          }
        }
        if (!validPosition)
          break;
      }

      // Check if the position is occupied by a snake
      if (validPosition && isPositionOccupied(x, y)) {
        validPosition = false;
      }
    } while (!validPosition);

    foodX[i] = x;
    foodY[i] = y;
  }
}

void checkFoodCollision(int &length, int snakeX[], int snakeY[]) {
  for (int i = 0; i < FOOD_COUNT; i++) {
    if (snakeX[0] == foodX[i] && snakeY[0] == foodY[i]) {
      if (length < MAX_LENGTH)
        length++;
      int newX, newY;
      collisionOccurred = true;
      do {
        newX = random(0, GRID_WIDTH);
        newY = random(0, GRID_HEIGHT);
      } while (isPositionOccupied(newX, newY));
      foodX[i] = newX;
      foodY[i] = newY;
    }
  }
}

void drawFood() {
  for (int i = 0; i < FOOD_COUNT; i++) {
    drawSegment(foodX[i], foodY[i], COLOR_GREEN);
  }
}

// --- Wall Functions ---
void spawnWalls() {
  // Spawn vertical walls
  for (int i = 0; i < 5; i++) {
    bool validPosition;
    do {
      validPosition = true;
      wallvX[i] = random(1, GRID_WIDTH - 1);
      wallvY[i] = random(1, GRID_HEIGHT - 2);
      for (int j = 0; j < i; j++) {
        if (wallvX[i] == wallvX[j] && abs(wallvY[i] - wallvY[j]) <= 2) {
          validPosition = false;
          break;
        }
      }
    } while (!validPosition);
  }
  // Spawn horizontal walls
  for (int i = 0; i < 5; i++) {
    bool validPosition;
    do {
      validPosition = true;
      wallhX[i] = random(1, GRID_WIDTH - 2);
      wallhY[i] = random(1, GRID_HEIGHT - 1);
      for (int j = 0; j < i; j++) {
        if (wallhY[i] == wallhY[j] && abs(wallhX[i] - wallhX[j]) <= 2) {
          validPosition = false;
          break;
        }
      }
    } while (!validPosition);
  }
}

void checkWallCollision(int &length, int snakeX[], int snakeY[]) {
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 3; j++) {
      if (snakeX[0] == wallvX[i] && snakeY[0] == wallvY[i] + j) {
        length = 5;
        collisionOccurred = true;
        return;
      }
    }
  }
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 3; j++) {
      if (snakeX[0] == wallhX[i] + j && snakeY[0] == wallhY[i]) {
        length = 5;
        collisionOccurred = true;
        return;
      }
    }
  }
}

void drawWalls() {
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 3; j++) {
      drawSegment(wallvX[i], wallvY[i] + j, COLOR_GRAY);
    }
  }
  for (int i = 0; i < 5; i++) {
    for (int j = 0; j < 3; j++) {
      drawSegment(wallhX[i] + j, wallhY[i], COLOR_GRAY);
    }
  }
}

// --- Test Display Function ---
void testDisplay() {
  display.clear();
  display.setBackgroundColor(COLOR_BLACK);
  display.setFont(Terminal6x8);
  display.drawText(10, TOP_PADDING + 10, "Display Test", COLOR_WHITE);
  display.drawText(10, TOP_PADDING + 30, "Working!", COLOR_GREEN);
  delay(2000);
}

// --- Move Snake (with grid wrapping) ---
void moveSnake(int snakeX[], int snakeY[], int &length, int dirX, int dirY) {
  int oldTailX = snakeX[length - 1];
  int oldTailY = snakeY[length - 1];

  for (int i = length - 1; i > 0; i--) {
    snakeX[i] = snakeX[i - 1];
    snakeY[i] = snakeY[i - 1];
  }
  snakeX[0] += dirX;
  snakeY[0] += dirY;

  if (snakeX[0] < 0)
    snakeX[0] = GRID_WIDTH - 1;
  if (snakeX[0] >= GRID_WIDTH)
    snakeX[0] = 0;
  if (snakeY[0] < 0)
    snakeY[0] = GRID_HEIGHT - 1;
  if (snakeY[0] >= GRID_HEIGHT)
    snakeY[0] = 0;
}

// --- Check for Snake-to-Snake Collision ---
void checkSnakeCollision() {
  for (int i = 0; i < snake2Length; i++) {
    if (snake1X[0] == snake2X[i] && snake1Y[0] == snake2Y[i]) {
      if (snake1Length > 5)
        snake1Length--;
      collisionOccurred = true;
    }
  }
  for (int i = 0; i < snake1Length; i++) {
    if (snake2X[0] == snake1X[i] && snake2Y[0] == snake1Y[i]) {
      if (snake2Length > 5)
        snake2Length--;
      collisionOccurred = true;
    }
  }
}

// --- Display Timer (in seconds) ---
void displayTime(int secondsRemaining) {
  display.fillRectangle(0, 0, SCREEN_WIDTH, TOP_PADDING, COLOR_BROWN);
  display.setFont(Terminal6x8);
  display.drawText(50, 2, "Time: " + String(secondsRemaining), COLOR_WHITE);
}

// --- Draw Bottom Padding ---
void displayBottomPadding() {
  display.fillRectangle(0, SCREEN_HEIGHT - BOTTOM_PADDING, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BROWN);
}

// --- Incremental Update Function ---
void updateSnakeDisplay(int prevLength, int prevX[], int prevY[],
                        int newLength, int snakeX[], int snakeY[],
                        uint16_t headColor, uint16_t bodyColor) {
  // Erase cells that are no longer occupied
  for (int i = 0; i < prevLength; i++) {
    bool stillPresent = false;
    for (int j = 0; j < newLength; j++) {
      if (prevX[i] == snakeX[j] && prevY[i] == snakeY[j]) {
        stillPresent = true;
        break;
      }
    }
    if (!stillPresent) {
      drawSegment(prevX[i], prevY[i], COLOR_BLACK);
    }
  }
  // Draw snake using new positions
  for (int i = 0; i < newLength; i++) {
    if (i == 0)
      drawSegment(snakeX[i], snakeY[i], headColor);
    else
      drawSegment(snakeX[i], snakeY[i], bodyColor);
  }
}

// --- End Screen ---
void showEndScreen() {
  if (snake1Length > snake2Length)
    snake1Wins = true;
  else if (snake2Length > snake1Length)
    snake2Wins = true;

  gameStarted = false;

  // Clear the screen and set a white border
  display.fillRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
  for (int i = 1; i < 6; i++)
    display.drawRectangle(0, 0, SCREEN_WIDTH - i, SCREEN_HEIGHT - i, COLOR_WHITE);

  // Draw the "Game Over" text
  display.setFont(Terminal6x8);
  display.drawText(40, 100, "###########", COLOR_WHITE);
  display.drawText(40, 110, " Game Over ", COLOR_WHITE);
  display.drawText(40, 120, "###########", COLOR_WHITE);

  // Display who won
  if (snake1Wins) {
    display.drawText(40, 150, "Snake 1 Wins!", COLOR_WHITE);
  } else if (snake2Wins) {
    display.drawText(40, 150, "Snake 2 Wins!", COLOR_WHITE);
  } else {
    display.drawText(40, 150, "It was a Draw", COLOR_WHITE);
  }

  // Draw a more natural grass effect at the bottom of the screen
  for (int x = 0; x < SCREEN_WIDTH; x++) {
    for (int y = 0; y < 50; y++) {
      // Use a random chance (e.g., 30% chance) to draw a pixel
      if (random(0, 100) < 30) {
        // Generate a green color with slight variation in red and blue components.
        int greenVal = random(200, 256);  // Keep green high for lush grass
        int redVal = random(0, 50);
        int blueVal = random(0, 50);
        uint16_t grassColor = display.setColor(redVal, greenVal, blueVal);
        // Draw the pixel at (x, SCREEN_HEIGHT - y)
        display.drawPixel(x, SCREEN_HEIGHT - y, grassColor);
      }
    }
  }

  display.drawText(20, 50, "Want to Play again?", COLOR_WHITE);
  display.drawText(20, 60, "Press both the buttons", COLOR_WHITE);
  display.drawText(20, 70, "to start again", COLOR_WHITE);

  // Reset variables to initial values
  snake1Length = 5;
  snake2Length = 5;
  dir1X = 1;
  dir1Y = 0;
  dir2X = -1;
  dir2Y = 0;
  encoder1Pos = 0;
  encoder2Pos = 0;
  encoder1Steps = 0;
  encoder2Steps = 0;
  lastEncoder1Dir = 0;
  lastEncoder2Dir = 0;
  snake1Wins = false;
  snake2Wins = false;
  collisionOccurred = false;
  for (int i = 0; i < MAX_LENGTH; i++) {
    snake1X[i] = 0;
    snake1Y[i] = 0;
    snake2X[i] = 0;
    snake2Y[i] = 0;
    prevSnake1X[i] = 0;
    prevSnake1Y[i] = 0;
    prevSnake2X[i] = 0;
    prevSnake2Y[i] = 0;
  }
  for (int i = 0; i < 5; i++) {
    wallhX[i] = 0;
    wallhY[i] = 0;
    wallvX[i] = 0;
    wallvY[i] = 0;
  }
  for (int i = 0; i < FOOD_COUNT; i++) {
    foodX[i] = 0;
    foodY[i] = 0;
  }
}

// Start screen
void showStartScreen() {
  display.fillRectangle(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
  for (int i = 1; i < 6; i++)
    display.drawRectangle(0, 0, SCREEN_WIDTH - i, SCREEN_HEIGHT - i, COLOR_WHITE);

  // Draw a more natural grass effect at the bottom of the screen
  for (int x = 0; x < SCREEN_WIDTH; x++) {
    for (int y = 0; y < 50; y++) {
      // Use a random chance (e.g., 30% chance) to draw a pixel
      if (random(0, 100) < 30) {
        // Generate a green color with slight variation in red and blue components.
        int greenVal = random(200, 256);  // Keep green high for lush grass
        int redVal = random(0, 50);
        int blueVal = random(0, 50);
        uint16_t grassColor = display.setColor(redVal, greenVal, blueVal);
        // Draw the pixel at (x, SCREEN_HEIGHT - y)
        display.drawPixel(x, SCREEN_HEIGHT - y, grassColor);
      }
    }
  }

  // Title
  display.setFont(Terminal6x8);
  display.drawText((SCREEN_WIDTH - (12 * 6)) / 2, 50, "Double Bite", COLOR_YELLOW);

  // Instruction for Encoder1 threshold selection
  display.drawText(10, 100, "Rotate Encoder1", COLOR_WHITE);
  display.drawText(10, 110, "to set threshold", COLOR_WHITE);

  // Threshold Selection for Encoder1
  int lastThreshold1 = threshold1;
  display.drawText(15, 125, "Encoder1 Threshold: " + String(threshold1), COLOR_WHITE);
  while (true) {
    static int lastThreshold1 = threshold1;
    if (threshold1 != lastThreshold1) {
      display.fillRectangle(15, 125, SCREEN_WIDTH - 15, 135, COLOR_BLACK);
      display.drawText(15, 125, "Encoder1 Threshold: " + String(threshold1), COLOR_WHITE);
      lastThreshold1 = threshold1;
    }
    int newValue = readEncoder1Threshold();
    if (newValue != 0) {
      threshold1 += newValue;
      // Limit threshold2 between 2 and 15 to ensure reasonable sensitivity for the rotary encoder.
      threshold1 = constrain(threshold1, 2, 15);
    }
    // Confirm selection with Encoder1 switch
    if (digitalRead(ENC1_SW) == LOW) {
      delay(500);  // Debounce
      break;
    }
  }

  delay(500);  // Separate the two selections

  // Instruction for Encoder2 threshold selection
  display.drawText(10, 100, "Rotate Encoder2", COLOR_WHITE);
  display.drawText(10, 110, "to set threshold", COLOR_WHITE);

  // Threshold Selection for Encoder2
  int lastThreshold2 = threshold2;
  display.drawText(15, 125, "Encoder2 Threshold: " + String(threshold2), COLOR_WHITE);
  while (true) {
    if (threshold2 != lastThreshold2) {
      display.fillRectangle(15, 125, SCREEN_WIDTH - 15, 135, COLOR_BLACK);
      display.drawText(15, 125, "Encoder2 Threshold: " + String(threshold2), COLOR_WHITE);
      lastThreshold2 = threshold2;
    }
    int newValue = readEncoder2Threshold();
    if (newValue != 0) {
      threshold2 += newValue;
      // Limit threshold2 between 2 and 15 to ensure reasonable sensitivity for the rotary encoder.
      threshold2 = constrain(threshold2, 2, 15);
    }
    // Confirm selection with Encoder2 switch
    if (digitalRead(ENC2_SW) == LOW) {
      delay(500);  // Debounce
      break;
    }
  }

  // Final "Press to Start" message
  display.drawText(50, 145, "Press to Start", COLOR_WHITE);

  // Wait for confirmation
  while (digitalRead(ENC1_SW) == HIGH && digitalRead(ENC2_SW) == HIGH)
    ;
  delay(500);
}

int readEncoder1Threshold() {
  // Use a static variable to hold the last state of ENC1_CLK.
  static int lastClkState = HIGH;
  int change = 0;

  int currentClkState = digitalRead(ENC1_CLK);
  // Detect a falling edge.
  if (lastClkState == HIGH && currentClkState == LOW) {
    // On a falling edge, decide the direction by reading ENC1_DT.
    if (digitalRead(ENC1_DT) == HIGH) {
      change = 1;
    } else {
      change = -1;
    }
    // Small delay for debouncing.
    delay(50);
  }
  lastClkState = currentClkState;
  return change;
}

int readEncoder2Threshold() {
  static int lastClkState = HIGH;
  int change = 0;

  int currentClkState = digitalRead(ENC2_CLK);
  if (lastClkState == HIGH && currentClkState == LOW) {
    if (digitalRead(ENC2_DT) == HIGH) {
      change = 1;
    } else {
      change = -1;
    }
    delay(50);
  }
  lastClkState = currentClkState;
  return change;
}

// --- Check if Start Button is Pressed ---
bool checkStartPressed() {
  return (digitalRead(ENC1_SW) == LOW && digitalRead(ENC2_SW) == LOW);
}

// --- Main Game Loop ---
void gameLoop() {
  gameStartTime = millis();
  collisionOccurred = false;

  // Initialize snake positions
  for (int i = 0; i < snake1Length; i++) {
    snake1X[i] = GRID_WIDTH / 4 + i;
    snake1Y[i] = (3 * GRID_HEIGHT) / 4;
  }
  for (int i = 0; i < snake2Length; i++) {
    snake2X[i] = (3 * GRID_WIDTH) / 4 - i;
    snake2Y[i] = GRID_HEIGHT / 4;
  }

  // Initialize previous snake arrays
  prevSnake1Length = snake1Length;
  for (int i = 0; i < snake1Length; i++) {
    prevSnake1X[i] = snake1X[i];
    prevSnake1Y[i] = snake1Y[i];
  }
  prevSnake2Length = snake2Length;
  for (int i = 0; i < snake2Length; i++) {
    prevSnake2X[i] = snake2X[i];
    prevSnake2Y[i] = snake2Y[i];
  }

  spawnWalls();
  spawnFood();
  drawWalls();
  drawFood();

  Serial.println("Game started");

  // Draw initial snakes
  for (int i = 0; i < snake1Length; i++) {
    if (i == 0)
      drawSegment(snake1X[i], snake1Y[i], COLOR_CYAN);
    else
      drawSegment(snake1X[i], snake1Y[i], COLOR_BLUE);
  }
  for (int i = 0; i < snake2Length; i++) {
    if (i == 0)
      drawSegment(snake2X[i], snake2Y[i], COLOR_MAGENTA);
    else
      drawSegment(snake2X[i], snake2Y[i], COLOR_RED);
  }

  displayBottomPadding();

  while (true) {
    unsigned long elapsed = millis() - gameStartTime;
    if (elapsed >= gameDuration)
      break;
    int secondsRemaining = (gameDuration - elapsed) / 1000;
    displayTime(secondsRemaining);

    // Save current snake positions before moving
    prevSnake1Length = snake1Length;
    for (int i = 0; i < snake1Length; i++) {
      prevSnake1X[i] = snake1X[i];
      prevSnake1Y[i] = snake1Y[i];
    }
    prevSnake2Length = snake2Length;
    for (int i = 0; i < snake2Length; i++) {
      prevSnake2X[i] = snake2X[i];
      prevSnake2Y[i] = snake2Y[i];
    }

    updateDirection();

    // Move snakes
    moveSnake(snake1X, snake1Y, snake1Length, dir1X, dir1Y);
    moveSnake(snake2X, snake2Y, snake2Length, dir2X, dir2Y);

    // Extra move if button pressed
    if (digitalRead(ENC1_SW) == LOW) {
      moveSnake(snake1X, snake1Y, snake1Length, dir1X, dir1Y);
    }
    if (digitalRead(ENC2_SW) == LOW) {
      moveSnake(snake2X, snake2Y, snake2Length, dir2X, dir2Y);
    }

    // Check collisions
    checkFoodCollision(snake1Length, snake1X, snake1Y);
    checkFoodCollision(snake2Length, snake2X, snake2Y);
    checkWallCollision(snake1Length, snake1X, snake1Y);
    checkWallCollision(snake2Length, snake2X, snake2Y);
    checkSnakeCollision();

    // Incremental update: update only changed cells
    updateSnakeDisplay(prevSnake1Length, prevSnake1X, prevSnake1Y,
                       snake1Length, snake1X, snake1Y,
                       COLOR_CYAN, COLOR_BLUE);
    updateSnakeDisplay(prevSnake2Length, prevSnake2X, prevSnake2Y,
                       snake2Length, snake2X, snake2Y,
                       COLOR_MAGENTA, COLOR_RED);

    // Redraw food and bottom padding
    drawFood();
    displayBottomPadding();
  }
  showEndScreen();
}

void setup() {
  Serial.begin(9600);
  display.begin();
  // testDisplay();
  display.setBackgroundColor(COLOR_BLACK);
  display.clear();
  display.setFont(Terminal6x8);

  pinMode(ENC1_CLK, INPUT);
  pinMode(ENC1_DT, INPUT);
  pinMode(ENC2_CLK, INPUT);
  pinMode(ENC2_DT, INPUT);
  pinMode(ENC1_SW, INPUT_PULLUP);
  pinMode(ENC2_SW, INPUT_PULLUP);

  attachInterrupt(digitalPinToInterrupt(ENC1_CLK), encoder1ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC2_CLK), encoder2ISR, CHANGE);

  showStartScreen();
}

void loop() {
  if (!gameStarted && checkStartPressed()) {
    gameStarted = true;
    display.clear();
    gameLoop();
  }
}