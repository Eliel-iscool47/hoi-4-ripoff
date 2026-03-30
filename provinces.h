#ifndef PROVINCES
#define PROVINCES

typedef struct Province
{
	char name[100];
	int map_r, map_g, map_b;		// The unique color of this province in map.bmp
	int owner_nation_id;			// Index of the nation in the nations array
	int min_x, max_x, min_y, max_y; // Bounding box for scanning optimization
	int units;
	int production;
	bool is_water;
	struct Province *neighbors[32];
} Province;

#endif