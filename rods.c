#include "rods.h"
#include <stdio.h>
#include <stdlib.h>

const int UNIT_ROD_LENGTH = 40;
const int ROD_HEIGHT = 40;


const Color COLORS[] = {LIGHTGRAY, RED, GREEN, PURPLE, YELLOW,
                        DARKGREEN, BLACK, BROWN, BLUE, ORANGE};




Rod NewRod(int numericLength, float x, float y)
{
  return (Rod){rect : (Rectangle){x, y, numericLength * UNIT_ROD_LENGTH, ROD_HEIGHT}, numericLength};
}

Vector2 GetTopLeft(Rod rod)
{
  return (Vector2){rod.rect.x, rod.rect.y};
}

Vector2 GetBottomRight(Rod rod)
{
  return (Vector2){rod.rect.x + rod.rect.width, rod.rect.y + rod.rect.height};
}

Vector2 GetBottomLeft(Rod rod)
{
  return (Vector2){rod.rect.x, rod.rect.y + rod.rect.height};
}

Vector2 GetTopRight(Rod rod)
{
  return (Vector2){rod.rect.x + rod.rect.width, rod.rect.y};
}

float GetTop(Rod rod)
{
  return rod.rect.y;
}

void SetTop(Rod *rod, float top)
{
  rod->rect.y = top;
}

float GetBottom(Rod rod)
{
  return rod.rect.y + rod.rect.height;
}

void SetBottom(Rod *rod, float bottom)
{
  rod->rect.y = bottom - rod->rect.height;
}

float GetLeft(Rod rod)
{
  return rod.rect.x;
}

void SetLeft(Rod *rod, float left)
{
  rod->rect.x = left;
}

float GetRight(Rod rod)
{
  return rod.rect.x + rod.rect.width;
}

void SetRight(Rod *rod, float right)
{
  rod->rect.x = right - rod->rect.width;
}

void SetTopLeft(Rod *rod, Vector2 newPos)
{
  rod->rect.x = newPos.x;
  rod->rect.y = newPos.y;
}

Color GetRodColor(Rod rod)
{
  return COLORS[rod.numericLength - 1];
}

RodGroup *NewRodGroup(const char *spec_name)
{
  int i;
  int nbRods;
  FILE *f;
  f = fopen(spec_name, "r");
  if (f == NULL)
  {
    perror("Couldn't open the spec.\n");
    abort();
  }
  fscanf(f, "%d ", &nbRods);
  RodGroup *rod_group = malloc(sizeof(RodGroup) + nbRods * sizeof(Rod));
  rod_group->nbRods = nbRods;
  for (i = 0; i < nbRods; i++)
  {
    int l;
    float x;
    float y;
    fscanf(f, "%d %f %f ", &l, &x, &y);
    rod_group->rods[i] = NewRod(l, x, y);
  }
  fclose(f);
  return rod_group;
}


void SaveRodGroup(RodGroup rodGroup, FILE *file)
{
  fprintf(file, "%d ", rodGroup.nbRods);
  for (int i = 0; i < rodGroup.nbRods; i++)
  {
    Rod rod = rodGroup.rods[i];
    fprintf(file, "%d %f %f ", rod.numericLength, rod.rect.x, rod.rect.y);
  }
}