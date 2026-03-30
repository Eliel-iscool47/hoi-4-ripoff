#ifndef NATIONS_H
#define NATIONS_H

#include <string.h>

typedef struct
{
	char name[100];
	int r, g, b; // Added for map color identification
	float stability; 
	// Array of provinces
} Nation;


#endif