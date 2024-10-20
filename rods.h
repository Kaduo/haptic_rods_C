#include <raylib.h>

#define NB_RODS_MENU 10
extern const int UNIT_ROD_LENGTH;
extern const int ROD_HEIGHT;


extern const Color COLORS[NB_RODS_MENU];



typedef struct Rod
{
  Rectangle rect;
  int numericLength;
} Rod;


typedef struct RodGroup
{
  int nbRods;
  Rod rods[];
} RodGroup;



Rod NewRod(int numericLength, float x, float y);
Vector2 GetTopLeft(Rod rod);
Vector2 GetBottomRight(Rod rod);

Vector2 GetBottomLeft(Rod rod);
Vector2 GetTopRight(Rod rod);

float GetTop(Rod rod);
void SetTop(Rod *rod, float top);
float GetBottom(Rod rod);
void SetBottom(Rod *rod, float bottom);
float GetLeft(Rod rod);

void SetLeft(Rod *rod, float left);

float GetRight(Rod rod);
void SetRight(Rod *rod, float right);

void SetTopLeft(Rod *rod, Vector2 newPos);
Color GetRodColor(Rod rod);
RodGroup *NewRodGroup(const char *spec_name);