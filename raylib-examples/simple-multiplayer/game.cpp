#include <raylib.h>
#include <vector>
#include <cstdlib>
#include <ctime>

struct Player
{
	float x;
	float y;
	float size;
	int score;
	Color color;
};

struct Coin
{
	float x;
	float y;
	float size;
	bool collected;
	Color color;
};

const int SCREEN_WIDTH = 800;
const int SCREEN_HEIGHT = 600;
const float PLAYER_SPEED = 200.0f;
const float PLAYER_SIZE = 20.0f;
const float COIN_SIZE = 10.0f;
const int MAX_COINS = 20;

void SpawnCoin(Coin& coin)
{
	coin.x = static_cast<float>(rand() % (SCREEN_WIDTH - 100) + 50);
	coin.y = static_cast<float>(rand() % (SCREEN_HEIGHT - 100) + 50);
	coin.size = COIN_SIZE;
	coin.collected = false;
	coin.color = GOLD;
}

bool CheckCollision(float x1, float y1, float r1, float x2, float y2, float r2)
{
	float dx = x1 - x2;
	float dy = y1 - y2;
	float distance = dx * dx + dy * dy;
	float radiusSum = r1 + r2;
	return distance < radiusSum * radiusSum;
}

int main() {
	srand(static_cast<unsigned int>(time(nullptr)));

	InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "Coin Collector - Raylib Game");
	SetTargetFPS(60);

	Player player;
	player.x = SCREEN_WIDTH / 2.0f;
	player.y = SCREEN_HEIGHT / 2.0f;
	player.size = PLAYER_SIZE;
	player.score = 0;
	player.color = BLUE;

	std::vector<Coin> coins(MAX_COINS);
	for (int i = 0; i < MAX_COINS; i++) {
		SpawnCoin(coins[i]);
	}

	while (!WindowShouldClose()) {
		float dt = GetFrameTime();

		// Handle player movement
		if (IsKeyDown(KEY_LEFT) || IsKeyDown(KEY_A))
		{
			player.x -= PLAYER_SPEED * dt;
		}
		if (IsKeyDown(KEY_RIGHT) || IsKeyDown(KEY_D))
		{
			player.x += PLAYER_SPEED * dt;
		}
		if (IsKeyDown(KEY_UP) || IsKeyDown(KEY_W))
		{
			player.y -= PLAYER_SPEED * dt;
		}
		if (IsKeyDown(KEY_DOWN) || IsKeyDown(KEY_S))
		{
			player.y += PLAYER_SPEED * dt;
		}

		// Keep player in bounds
		if (player.x < player.size) player.x = player.size;
		if (player.x > SCREEN_WIDTH - player.size) player.x = SCREEN_WIDTH - player.size;
		if (player.y < player.size) player.y = player.size;
		if (player.y > SCREEN_HEIGHT - player.size) player.y = SCREEN_HEIGHT - player.size;

		// Check coin collection
		for (int i = 0; i < MAX_COINS; i++)
		{
			if (!coins[i].collected)
			{
				if (CheckCollision(player.x, player.y, player.size,
														coins[i].x, coins[i].y, coins[i].size))
				{
					coins[i].collected = true;
					player.score += 10;
					// Respawn coin at new location
					SpawnCoin(coins[i]);
				}
			}
		}

		// Render
		BeginDrawing();
		ClearBackground(Color{30, 30, 40, 255});

		// Draw coins
		for (int i = 0; i < MAX_COINS; i++) {
			if (!coins[i].collected) {
					DrawCircle(static_cast<int>(coins[i].x),
										static_cast<int>(coins[i].y),
										coins[i].size,
										coins[i].color);
					DrawCircleLines(static_cast<int>(coins[i].x),
													static_cast<int>(coins[i].y),
													coins[i].size,
													YELLOW);
			}
		}

		// Draw player
		DrawCircle(static_cast<int>(player.x),
							static_cast<int>(player.y),
							player.size,
							player.color);
		DrawCircleLines(static_cast<int>(player.x),
										static_cast<int>(player.y),
										player.size,
										SKYBLUE);

		// Draw score
		DrawText(TextFormat("Score: %d", player.score), 10, 10, 30, WHITE);

		// Draw instructions
		DrawText("Use WASD or Arrow Keys to move", 10, SCREEN_HEIGHT - 30, 20, LIGHTGRAY);

		EndDrawing();
	}

	CloseWindow();
	return 0;
}