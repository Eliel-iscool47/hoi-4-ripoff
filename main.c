#include <SDL2/SDL.h>
#include <SDL2/SDL_ttf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include <stdbool.h>
#include "nations.h"
#include "provinces.h"

float attack_ratio = 0.6f;
int province_color_offset = 0; // Set to 0 as fill_provinces.py uses literal RGB values
int get_days_in_month(int month, int year)
{
	switch (month)
	{
	case 2:
		if ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0))
			return 29;
		return 28;
	case 4:
	case 6:
	case 9:
	case 11:
		return 30;
	default:
		return 31;
	}
}

void debug()
{
}

// Helper to create a texture from text
SDL_Texture *create_text_texture(SDL_Renderer *renderer, TTF_Font *font, const char *text, SDL_Color color)
{
	if (!text || strlen(text) == 0)
		return NULL;
	SDL_Surface *surface = TTF_RenderText_Blended(font, text, color);
	if (!surface)
		return NULL;
	SDL_Texture *texture = SDL_CreateTextureFromSurface(renderer, surface);
	SDL_FreeSurface(surface);
	return texture;
}

// Helper to resolve combat and update the map texture
int resolve_combat(Province *attacker, Province *defender, Nation *nations, SDL_Surface *mapSurface, SDL_Texture *mapTexture, int *province_id_map, int map_pixel_w)
{
	if (attacker->owner_nation_id == defender->owner_nation_id)
		return 0; // Same nation

	printf("COMBAT: %s attacking %s!\n", nations[attacker->owner_nation_id].name, defender->name);

	// Simple combat math: units * stability + random factor
	float attack_power = attacker->units * (nations[attacker->owner_nation_id].stability / 100.0f) * (0.8f + (rand() % 40) / 100.0f);
	float defense_power = defender->units * (nations[defender->owner_nation_id].stability / 100.0f) * (0.8f + (rand() % 40) / 100.0f);

	int defender_id = defender->map_r + (defender->map_g * 256) + (defender->map_b * 65536);

	if (attack_power > defense_power)
	{
		printf("Victory! %s has been conquered by %s.\n", defender->name, nations[attacker->owner_nation_id].name);
		defender->owner_nation_id = attacker->owner_nation_id;
		defender->units = (int)(attacker->units * attack_ratio); // 60% of attackers move in
		attacker->units = (int)(attacker->units * attack_ratio); // 40% stay behind

		// Only update map colors if the province is land.
		// This keeps the oceans blue even when occupied.
		if (!defender->is_water)
		{
			Nation *new_owner = &nations[defender->owner_nation_id];
			for (int y = defender->min_y; y <= defender->max_y; y++)
			{
				for (int x = defender->min_x; x <= defender->max_x; x++)
				{
					Uint32 *pixel = (Uint32 *)((Uint8 *)mapSurface->pixels + y * mapSurface->pitch + x * 4);
					if (province_id_map[y * map_pixel_w + x] == defender_id)
					{
						*pixel = SDL_MapRGBA(mapSurface->format, new_owner->r, new_owner->g, new_owner->b, 255);
					}
				}
			}
			SDL_UpdateTexture(mapTexture, NULL, mapSurface->pixels, mapSurface->pitch);
		}
	}
	attacker->units *= 0.7f; // Attacker loses 30% of troops
	defender->units *= 0.9f; // Defender loses 10%

	return 0;
}

// Helper to add neighbors to the Province struct (bidirectional)
void add_neighbor(Province *p1, Province *p2)
{
	if (!p1 || !p2 || p1 == p2)
		return;

	// Add p2 to p1's list if not already present
	for (int i = 0; i < 32; i++)
	{
		if (p1->neighbors[i] == p2)
			break;
		if (p1->neighbors[i] == NULL)
		{
			p1->neighbors[i] = p2;
			break;
		}
	}
	// Add p1 to p2's list if not already present
	for (int i = 0; i < 32; i++)
	{
		if (p2->neighbors[i] == p1)
			break;
		if (p2->neighbors[i] == NULL)
		{
			p2->neighbors[i] = p1;
			break;
		}
	}
}

int main(int argc, char *argv[])
{
	// Initialize SDL
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) < 0)
	{
		printf("SDL could not initialize! SDL_Error: %s\n", SDL_GetError());
		return 1;
	}

	if (TTF_Init() == -1)
	{
		printf("SDL_ttf could not initialize! TTF_Error: %s\n", TTF_GetError());
		return 1;
	}

	srand(time(NULL));

	// Create Window
	SDL_Window *window = SDL_CreateWindow("Nations of Conflict",
										  SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 800, 600,
										  SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
	if (window == NULL)
	{
		printf("Uncaught SDL_Error: %s\n", SDL_GetError());
		return 1;
	}

	// --- Initialize Nations ---
	const int NUM_NATIONS = 256;
	Nation nations[NUM_NATIONS];
	memset(nations, 0, sizeof(nations)); // Zero initialize

	FILE *nFile = fopen("nations.txt", "r");
	if (nFile)
	{
		char line[256];
		while (fgets(line, sizeof(line), nFile))
		{
			int id, r, g, b;
			float stab;
			char name[100];
			// Parse: ID "Name" R G B Stability
			if (sscanf(line, "%d \"%[^\"]\" %d %d %d %f", &id, name, &r, &g, &b, &stab) == 6)
			{
				if (id >= 0 && id < NUM_NATIONS)
				{
					strcpy(nations[id].name, name);
					nations[id].r = r;
					nations[id].g = g;
					nations[id].b = b;
					nations[id].stability = stab;
				}
			}
		}
		fclose(nFile);
	}
	else
	{
		printf("Error: Could not open nations.txt\n");
	}
	// ------	--------------------

	// --- Initialize Provinces ---
	// This maps the color in 'map.bmp' to a logical province and an owner
	// NOTE: For this optimization to work, your map.bmp must use these specific colors.
	const int NUM_PROVINCES = 8192; // Increased limit to store more province data
	Province provinces[NUM_PROVINCES];
	memset(provinces, 0, sizeof(provinces));
	int loaded_provinces_count = 0;

	for (int i = 0; i < NUM_PROVINCES; i++)
	{
		provinces[i].min_x = 1000000; // Initialize with high/low values
		provinces[i].min_y = 1000000;
		provinces[i].max_x = -1;
		provinces[i].max_y = -1;
	}

	FILE *pFile = fopen("provinces.txt", "r");
	if (pFile)
	{
		char line[1024]; // A standard line buffer is sufficient and safer for the stack
		// We fill the array sequentially as we read from the file
		int pIndex = 0;
		while (fgets(line, sizeof(line), pFile) && pIndex < NUM_PROVINCES)
		{
			// Skip comments and empty lines.
			// sscanf naturally handles leading whitespace (spaces/tabs),
			if (line[0] == '#' || line[0] == ';' ||
				line[0] == '\n' || line[0] == '\r' ||
				line[0] == '\0')
				continue;
			int mapId, r, g, b, ownerId;
			char name[100];
			// Parse: ID;R;G;B;Name;OwnerID
			if (sscanf(line, "%d;%d;%d;%d;%[^;];%d", &mapId, &r, &g, &b, name, &ownerId) >= 6)
			{
				strcpy(provinces[pIndex].name, name);
				provinces[pIndex].map_r = r;
				provinces[pIndex].map_g = g;
				provinces[pIndex].map_b = b;
				provinces[pIndex].owner_nation_id = ownerId;
				provinces[pIndex].units = rand() % 50;
				provinces[pIndex].is_water = false; // Default to land
				provinces[pIndex].production = rand() % 2 + 1;

				pIndex++;
			}
		}
		loaded_provinces_count = pIndex;
		fclose(pFile);
	}
	else
		printf("Error: Could not open provinces.txt\n");
	// ----------------------------

	// Create Renderer
	// -1 initializes the first supported rendering driver
	// SDL_RENDERER_ACCELERATED uses hardware acceleration
	SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
	if (renderer == NULL)
	{
		printf("Renderer could not be created! SDL_Error: %s\n", SDL_GetError());
		return 1;
	}

	// Load Font - Ensure you have a .ttf file in a fonts/ folder
	// Suggestion: Use a bold weight for UI headers and a regular weight for details
	TTF_Font *uiFont = TTF_OpenFont("Fonts/Inter-VariableFont_opsz,wght.ttf", 18);
	if (uiFont == NULL)
	{
		printf("Failed to load font! TTF_Error: %s\n", TTF_GetError());
	}

	// Load Map Image (Must be a .bmp file)
	SDL_Surface *mapSurface = SDL_LoadBMP("maps/provinces_map.bmp");
	if (mapSurface == NULL)
	{
		printf("Unable to load image %s! SDL Error: %s\n", "maps/provinces_map.bmp", SDL_GetError());
		return 1;
	}

	// --- Create a fast lookup table for provinces ---
	// Supporting full 24-bit color space (16.7M entries)
	Province **province_lookup = (Province **)calloc(16777216, sizeof(Province *));
	if (!province_lookup)
	{
		printf("Failed to allocate memory for province_lookup!\n");
		return 1;
	}

	for (int i = 0; i < loaded_provinces_count; i++)
	{
		// Calculate ID based on the direct RGB color stored in the province data
		int province_id = provinces[i].map_r + (provinces[i].map_g * 256) + (provinces[i].map_b * 65536);
		if (province_id >= 0 && province_id < 16777216)
		{
			province_lookup[province_id] = &provinces[i];
		}
	}
	// ---------------------------------------------

	// 1. Convert to RGBA32 format
	SDL_Surface *formattedSurface = SDL_ConvertSurfaceFormat(mapSurface, SDL_PIXELFORMAT_RGBA32, 0);
	int map_pixel_w = mapSurface->w;
	int map_pixel_h = mapSurface->h;
	SDL_FreeSurface(mapSurface); // Free the old surface
	mapSurface = formattedSurface;

	// 2. Create a buffer to store Province IDs for mouse picking
	int *province_id_map = (int *)malloc(map_pixel_w * map_pixel_h * sizeof(int));
	if (province_id_map == NULL)
	{
		printf("Failed to allocate memory for province_id_map!\n");
		return 1;
	}
	memset(province_id_map, 0, map_pixel_w * map_pixel_h * sizeof(int));

	if (SDL_MUSTLOCK(mapSurface))
		SDL_LockSurface(mapSurface);

	for (int y = 0; y < mapSurface->h; y++)
	{
		for (int x = 0; x < mapSurface->w; x++)
		{
			// Calculate pointer to the specific pixel
			Uint32 *pixel = (Uint32 *)((Uint8 *)mapSurface->pixels + y * mapSurface->pitch + x * 4);

			Uint8 r, g, b, a;
			SDL_GetRGBA(*pixel, mapSurface->format, &r, &g, &b, &a);

			// Ignore pure black pixels (commonly used for map borders or voids)
			if (r == 0 && g == 0 && b == 0)
				continue;

			// O(1) lookup: Use the 24-bit color value as a direct index
			int lookup_id = (int)r + ((int)g * 256) + ((int)b * 65536);
			if (lookup_id >= 0 && lookup_id < 16777216) // Safety check for array bounds
			{
				Province *p = province_lookup[lookup_id];
				if (p != NULL)
				{
					// Only populate the click-map if the province is recognized in our data
					province_id_map[y * map_pixel_w + x] = lookup_id;

					// Update bounding box for faster scanning
					if (x < p->min_x)
						p->min_x = x;
					if (x > p->max_x)
						p->max_x = x;
					if (y < p->min_y)
						p->min_y = y;
					if (y > p->max_y)
						p->max_y = y;

					// Found the province, now find its owner to get the display color.
					int ownerId = p->owner_nation_id;
					if (ownerId < 0 || ownerId >= NUM_NATIONS)
					{
						// Skip if owner ID is invalid to prevent crash
						continue;
					}
					Nation *owner = &nations[ownerId];
					// Draw pixel using the OWNER NATION'S color
					*pixel = SDL_MapRGBA(mapSurface->format, owner->r, owner->g, owner->b, a);
				}
			}
		}
	}

	// Adjacency Scan: Populate neighbor arrays by looking across black borders
	const int border_jump = 4; // Search up to 4 pixels away to bridge border lines
	for (int y = 0; y < map_pixel_h; y++)
	{
		for (int x = 0; x < map_pixel_w; x++)
		{
			int id_current = province_id_map[y * map_pixel_w + x];
			if (id_current <= 0 || id_current >= 16777216)
				continue;

			// Look Right
			for (int r = 1; r <= border_jump && x + r < map_pixel_w; r++)
			{
				int id_other = province_id_map[y * map_pixel_w + (x + r)];
				if (id_other > 0 && id_other < 16777216)
				{
					if (id_other != id_current)
						add_neighbor(province_lookup[id_current], province_lookup[id_other]);
					break; // Found a province (or self), stop looking in this direction
				}
			}

			// Look Down
			for (int r = 1; r <= border_jump && y + r < map_pixel_h; r++)
			{
				int id_other = province_id_map[(y + r) * map_pixel_w + x];
				if (id_other > 0 && id_other < 16777216)
				{
					if (id_other != id_current)
						add_neighbor(province_lookup[id_current], province_lookup[id_other]);
					break;
				}
			}

			// Look Diagonal
			for (int r = 1; r <= border_jump && x + r < map_pixel_w && y + r < map_pixel_h; r++)
			{
				int id_other = province_id_map[(y + r) * map_pixel_w + (x + r)];
				if (id_other > 0 && id_other < 16777216)
				{
					if (id_other != id_current)
						add_neighbor(province_lookup[id_current], province_lookup[id_other]);
					break;
				}
			}
		}
	}

	if (SDL_MUSTLOCK(mapSurface))
		SDL_UnlockSurface(mapSurface);

	// Pre-allocate highlight buffer for efficiency
	SDL_Point *highlight_points = malloc(map_pixel_w * map_pixel_h * sizeof(SDL_Point));
	int highlight_count = 0;
	if (!highlight_points)
	{
		printf("Failed to allocate highlight points!\n");
		return 1;
	}

	// Create Texture from Surface (we now keep mapSurface for pixel updates)
	SDL_Texture *mapTexture = SDL_CreateTextureFromSurface(renderer, mapSurface);
	float mapScale = 1.0; // Set this to resize the map (e.g., 0.5 for half, 2.0 for double)
	int map_w = (int)(map_pixel_w * mapScale);
	int map_h = (int)(map_pixel_h * mapScale);

	int running = 1;
	SDL_Event e;

	/* Map Position (x, y)
	To center a specific point: (ScreenCenter) - (TargetCoordinate)
	Example: Centering the middle of the map */
	int map_x = (800 / 2) - (map_w / 2);
	int map_y = (600 / 2) - (map_h / 2);
	float speed = 5;
	int isDragging = 0;

	// Highlight System
	int selected_province_id = -1;

	// Text Textures
	SDL_Texture *dateTexture = NULL;
	SDL_Texture *infoTexture = NULL;
	SDL_Color textColor = {255, 255, 255, 255}; // White

	// Date System
	int year = 2026;
	int month = 1;
	int day = 1;
	Uint32 last_day_tick = SDL_GetTicks();
	const int MS_PER_DAY = 500; // Advance one day every 500ms

	// Main application loop
	while (running)
	{
		// Handle events (like clicking the X button)
		while (SDL_PollEvent(&e))
		{
			if (e.type == SDL_QUIT)
			{
				running = 0;
			}
			else if (e.type == SDL_MOUSEBUTTONDOWN)
			{
				if (e.button.button == SDL_BUTTON_LEFT)
				{
					isDragging = 1;

					// Province Selection Logic (Only on Left Click)
					int mouseX = e.button.x;
					int mouseY = e.button.y;

					int pixelX = (int)((mouseX - map_x) / mapScale);
					int pixelY = (int)((mouseY - map_y) / mapScale);

					if (pixelX >= 0 && pixelX < map_pixel_w && pixelY >= 0 && pixelY < map_pixel_h)
					{
						int id = province_id_map[pixelY * map_pixel_w + pixelX];
						if (id > 0 && province_lookup[id] != NULL)
						{
							Province *p = province_lookup[id];
							printf("Selected: %s (ID: %d) Owned by: %s\n", p->name, id, nations[p->owner_nation_id].name);

							// Update Info Text
							if (infoTexture)
								SDL_DestroyTexture(infoTexture);
							char infoStr[256];
							sprintf(infoStr, "%s | Owner: %s | Units: %d | Stability: %.1f%%",
									p->name, nations[p->owner_nation_id].name,
									p->units, nations[p->owner_nation_id].stability);
							infoTexture = create_text_texture(renderer, uiFont, infoStr, textColor);

							// Update Highlight Border
							if (id != selected_province_id)
							{
								selected_province_id = id;
								highlight_count = 0;

								// Scan for edges of the selected province
								for (int py = p->min_y; py <= p->max_y; py++)
								{
									for (int px = p->min_x; px <= p->max_x; px++)
									{
										if (province_id_map[py * map_pixel_w + px] == id)
										{
											bool is_edge = false;
											if (px == 0 || px == map_pixel_w - 1 || py == 0 || py == map_pixel_h - 1)
											{
												is_edge = true;
											}
											else
											{
												if (province_id_map[py * map_pixel_w + (px + 1)] != id ||
													province_id_map[py * map_pixel_w + (px - 1)] != id ||
													province_id_map[(py + 1) * map_pixel_w + px] != id ||
													province_id_map[(py - 1) * map_pixel_w + px] != id)
												{
													is_edge = true;
												}
											}

											if (is_edge)
											{
												highlight_points[highlight_count].x = px;
												highlight_points[highlight_count].y = py;
												highlight_count++;
											}
										}
									}
								}
							}
						}
					}
				}

				if (e.button.button == SDL_BUTTON_RIGHT)
				{
					// Combat Initiation Logic
					int pixelX = (int)((e.button.x - map_x) / mapScale);
					int pixelY = (int)((e.button.y - map_y) / mapScale);

					if (selected_province_id != -1 && pixelX >= 0 && pixelX < map_pixel_w && pixelY >= 0 && pixelY < map_pixel_h)
					{
						int target_id = province_id_map[pixelY * map_pixel_w + pixelX];
						if (target_id > 0 && target_id != selected_province_id)
						{
							Province *attacker = province_lookup[selected_province_id];
							Province *defender = province_lookup[target_id];

							if (attacker && defender)
							{
								// Check adjacency
								bool is_neighbor = false;
								for (int i = 0; i < 32; i++)
								{
									if (attacker->neighbors[i] == NULL)
										break;
									if (attacker->neighbors[i] == defender)
									{
										is_neighbor = true;
										break;
									}
								}

								if (is_neighbor)
								{
									if (attacker->owner_nation_id == defender->owner_nation_id)
									{
										// Friendly Province: Transfer 50% of units
										int move_amount = attacker->units / 2;
										attacker->units -= move_amount;
										defender->units += move_amount;
										printf("Moved %d units to %s\n", move_amount, defender->name);
									}
									else
									{
										// Hostile Province: Combat
										resolve_combat(attacker, defender, nations, mapSurface, mapTexture, province_id_map, map_pixel_w);
									}

									// Refresh UI text to show the result of the move/battle
									if (infoTexture)
										SDL_DestroyTexture(infoTexture);
									char infoStr[256];
									// Keep showing the selected province's updated stats
									sprintf(infoStr, "%s | Owner: %s | Units: %d | Stability: %.1f%%",
											attacker->name, nations[attacker->owner_nation_id].name,
											attacker->units, nations[attacker->owner_nation_id].stability);
									infoTexture = create_text_texture(renderer, uiFont, infoStr, textColor);
								}
								else
								{
									printf("Target is not a neighbor of %s!\n", attacker->name);
								}
							}
						}
						else
							debug();
					}
				}
			}
			else if (e.type == SDL_MOUSEBUTTONUP)
			{
				if (e.button.button == SDL_BUTTON_LEFT)
					isDragging = 0;
			}
			else if (e.type == SDL_MOUSEMOTION && isDragging)
			{
				map_x += e.motion.xrel;
				map_y += e.motion.yrel;
			}
			else if (e.type == SDL_MOUSEWHEEL)
			{
				int mx, my;
				SDL_GetMouseState(&mx, &my);

				float oldScale = mapScale;
				// Zoom in or out based on wheel direction
				if (e.wheel.y > 0)
					mapScale *= 1.1f;
				else if (e.wheel.y < 0)
					mapScale /= 1.1f;

				// Clamp zoom level to prevent the map from disappearing or becoming too large
				if (mapScale < 0.05f)
					mapScale = 0.05f;
				if (mapScale > 10.0f)
					mapScale = 10.0f;

				map_w = (int)(map_pixel_w * mapScale);
				map_h = (int)(map_pixel_h * mapScale);

				// Adjust map_x and map_y so the map point under the mouse remains stationary
				float scaleRatio = mapScale / oldScale;
				map_x = mx - (int)((mx - map_x) * scaleRatio);
				map_y = my - (int)((my - map_y) * scaleRatio);
			}
		}

		// Advance Date
		if (SDL_GetTicks() > last_day_tick + MS_PER_DAY)
		{
			day++;
			int days_in_month = get_days_in_month(month, year);
			if (day > days_in_month)
			{
				day = 1;
				month++;
				if (month > 12)
				{
					month = 1;
					year++;
				}
			}
			last_day_tick = SDL_GetTicks();
			// Unit regeneration
			for (int i = 0; i < NUM_PROVINCES; i++)
			{
				if (strlen(provinces[i].name) > 0 && !provinces[i].is_water)
				{
					provinces[i].units += provinces[i].production;
				}
			}

			// Refresh UI if a province is selected so the unit count updates visually
			if (selected_province_id != -1)
			{
				Province *p = province_lookup[selected_province_id];
				if (infoTexture)
					SDL_DestroyTexture(infoTexture);
				char infoStr[256];
				sprintf(infoStr, "%s | Owner: %s | Units: %d | Stability: %.1f%%",
						p->name, nations[p->owner_nation_id].name,
						p->units, nations[p->owner_nation_id].stability);
				infoTexture = create_text_texture(renderer, uiFont, infoStr, textColor);
			}

			// Update Date Texture
			if (dateTexture)
				SDL_DestroyTexture(dateTexture);
			char dateStr[64];
			sprintf(dateStr, "Date: %d-%d-%02d", day, month, year);
			dateTexture = create_text_texture(renderer, uiFont, dateStr, textColor);
		}

		// Handle Input
		const Uint8 *currentKeyStates = SDL_GetKeyboardState(NULL);
		if (currentKeyStates[SDL_SCANCODE_UP] || currentKeyStates[SDL_SCANCODE_W])
		{
			map_y += speed;
		}
		if (currentKeyStates[SDL_SCANCODE_DOWN] || currentKeyStates[SDL_SCANCODE_S])
		{
			map_y -= speed;
		}
		if (currentKeyStates[SDL_SCANCODE_LEFT] || currentKeyStates[SDL_SCANCODE_A])
		{
			map_x += speed;
		}
		if (currentKeyStates[SDL_SCANCODE_RIGHT] || currentKeyStates[SDL_SCANCODE_D])
		{
			map_x -= speed;
		}
		if (currentKeyStates[SDL_SCANCODE_R])
		{
			map_x = (800 / 2) - (map_w / 2);
			map_y = (600 / 2) - (map_h / 2);
		}
		if (currentKeyStates[SDL_SCANCODE_LEFTBRACKET])
		{
			float oldScale = mapScale;
			mapScale *= 0.98f; // Smoother multiplicative zoom
			if (mapScale < 0.05f)
				mapScale = 0.05f;

			map_w = (int)(map_pixel_w * mapScale);
			map_h = (int)(map_pixel_h * mapScale);

			// Zoom towards the center of the screen (400, 300)
			map_x = 400 - (int)((400 - map_x) * (mapScale / oldScale));
			map_y = 300 - (int)((300 - map_y) * (mapScale / oldScale));
		}
		if (currentKeyStates[SDL_SCANCODE_RIGHTBRACKET])
		{
			float oldScale = mapScale;
			mapScale *= 1.02f;
			if (mapScale > 10.0f)
				mapScale = 10.0f;

			map_w = (int)(map_pixel_w * mapScale);
			map_h = (int)(map_pixel_h * mapScale);

			map_x = 400 - (int)((400 - map_x) * (mapScale / oldScale));
			map_y = 300 - (int)((300 - map_y) * (mapScale / oldScale));
		}

		// Boundary Checks (Handles both large maps that need panning and small maps)
		int min_x, max_x, min_y, max_y;

		// X Axis Logic
		if (map_w > 800)
		{
			min_x = 800 - map_w;
			max_x = 0;
		}
		else
		{
			min_x = 0;
			max_x = 800 - map_w;
		}

		if (map_x < min_x)
			map_x = min_x;
		if (map_x > max_x)
			map_x = max_x;

		// Y Axis Logic
		if (map_h > 600)
		{
			min_y = 600 - map_h;
			max_y = 0;
		}
		else
		{
			min_y = 0;
			max_y = 600 - map_h;
		}

		if (map_y < min_y)
			map_y = min_y;
		if (map_y > max_y)
			map_y = max_y;

		// 1. Clear screen (set background color to black)
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255); // R, G, B, Alpha
		SDL_RenderClear(renderer);

		// 2. Draw Map
		SDL_Rect dstRect = {map_x, map_y, map_w, map_h};
		SDL_RenderCopy(renderer, mapTexture, NULL, &dstRect);

		// Debug: Draw adjacency lines between the selected province and its neighbors
		if (selected_province_id != -1)
		{
			Province *p = province_lookup[selected_province_id];
			if (p)
			{
				int startX = map_x + (int)(((p->min_x + p->max_x) / 2.0f) * mapScale);
				int startY = map_y + (int)(((p->min_y + p->max_y) / 2.0f) * mapScale);

				SDL_SetRenderDrawColor(renderer, 0, 255, 255, 255); // Cyan lines for debug
				for (int i = 0; i < 32; i++)
				{
					Province *nb = p->neighbors[i];
					if (nb == NULL)
						break;

					int endX = map_x + (int)(((nb->min_x + nb->max_x) / 2.0f) * mapScale);
					int endY = map_y + (int)(((nb->min_y + nb->max_y) / 2.0f) * mapScale);

					SDL_RenderDrawLine(renderer, startX, startY, endX, endY);
				}
			}
		}

		// 3. Draw UI Text
		if (dateTexture)
		{
			int tw, th;
			SDL_QueryTexture(dateTexture, NULL, NULL, &tw, &th);
			SDL_Rect dateRect = {10, 10, tw, th};
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150); // Semi-transparent bg
			SDL_RenderFillRect(renderer, &dateRect);
			SDL_RenderCopy(renderer, dateTexture, NULL, &dateRect);
		}

		if (infoTexture)
		{
			int tw, th;
			SDL_QueryTexture(infoTexture, NULL, NULL, &tw, &th);
			SDL_Rect infoRect = {10, 600 - th - 10, tw, th};
			SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
			SDL_RenderFillRect(renderer, &infoRect);
			SDL_RenderCopy(renderer, infoTexture, NULL, &infoRect);
		}

		// 3. Draw Highlight Border
		if (selected_province_id != -1 && highlight_points != NULL)
		{
			SDL_SetRenderDrawColor(renderer, 255, 255, 0, 255); // Yellow border
			for (int i = 0; i < highlight_count; i++)
			{
				SDL_Rect pRect = {map_x + (int)(highlight_points[i].x * mapScale),
								  map_y + (int)(highlight_points[i].y * mapScale),
								  (int)ceil(mapScale), (int)ceil(mapScale)};
				SDL_RenderFillRect(renderer, &pRect);
			}
		}

		// 4. Present the backbuffer to the screen
		SDL_RenderPresent(renderer);

		// Small delay to cap framerate (~60 FPS) so it doesn't move too fast
		SDL_Delay(16);
	}

	// Cleanup resources
	free(province_id_map);
	if (province_lookup)
		free(province_lookup);
	if (highlight_points)
		free(highlight_points);
	if (dateTexture)
		SDL_DestroyTexture(dateTexture);
	if (infoTexture)
		SDL_DestroyTexture(infoTexture);
	if (uiFont)
		TTF_CloseFont(uiFont);
	SDL_DestroyTexture(mapTexture);
	if (mapSurface)
		SDL_FreeSurface(mapSurface);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	TTF_Quit();
	SDL_Quit();

	return 0;
}

/*
   PROVINCE COLOR REFERENCE TABLE (RGB)
   Formula: Red = ID % 256, Green = 64 + (ID / 256), Blue = 0

   - Washington D.C. (1001):   RGB(233, 67, 0)
   - Northern California (1002): RGB(234, 67, 0)
   - North Texas (1003):       RGB(235, 67, 0)
   - Southern California (1023): RGB(255, 67, 0)
   - South Texas (1024):       RGB(0, 68, 0)
   - Florida (1025):           RGB(1, 68, 0)
   - West Texas (1026):        RGB(2, 68, 0)
   - East Texas (1027):        RGB(3, 68, 0)

   - Rio de Janeiro (1033):    RGB(9, 68, 0)
   - Quebec (1034):            RGB(10, 68, 0)
   - Western Australia (1035): RGB(11, 68, 0)
   - Munich (1036):            RGB(12, 68, 0)
*/
