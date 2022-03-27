#include <math.h>
#include <tice.h>
#include <keypadc.h>
#include <graphx.h>
#include <debug.h>
#include "gfx/gfx.h"

#define SCREEN_WIDTH 320
#define SCREEN_HEIGHT 240
#define SCREEN_MIDDLE_X (SCREEN_WIDTH/2)
#define SCREEN_MIDDLE_Y (SCREEN_HEIGHT/2)
#define WALL_SIZE 32
#define TANK_SIZE (WALL_SIZE/2)
#define TANK_RADIUS (TANK_SIZE/2)
#define ARM_RADIUS (int)(TANK_RADIUS * 0.7)
#define ARM_LENGTH (int)(TANK_RADIUS * 1.2)
#define ARM_WIDTH (int)(TANK_RADIUS * 0.4)
#define MOVEMENT_SPEED 1
#define DISTANCE(x,y,p,q) sqrt((p-x)*(p-x) + (q-y)*(q-y))
#define POINT_DISTANCE(a,b) DISTANCE(a.x,a.y,b.x,b.y)
#define MAX_BULLETS 5
#define BULLET_BOUNCES 1
#define PI 3.141592653689
#define BYTEANGLE_TO_RADIANS(angle) ((((256-angle) + 64)%256) * (PI / 180.0 * (360.0/256.0)))
#define RAY_LENGTH 500
#define BULLET_SPEED 2
#define BULLET_RADIUS 2
#define WALL_OFFSET_X (X_POS-SCREEN_MIDDLE_X)
#define WALL_OFFSET_Y (Y_POS-SCREEN_MIDDLE_Y)
#define FLIP_RADIAN_HORIZONTALLY(r) (r - (cos(r) * PI))
#define FLIP_RADIAN_VERTICALLY(r) (r - (sin(r) * PI))

/*
Tile IDs:
0 - Air
1 - Wall
2 - Fence
3 - Roof
4 - Player Spawn
5 - Player Roof Spawn
6 - Enemy Spawn
7 - Enemy Roof Spawn
8 - Weapon Spawn
*/

#define MAP_WIDTH 12
#define MAP_HEIGHT 8
static uint8_t MAP[MAP_HEIGHT][MAP_WIDTH] = {
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1},
    {1, 0, 0, 1, 0, 2, 2, 0, 1, 0, 0, 1},
    {1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1},
    {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1},
};

enum Direction {LEFT, RIGHT, TOP, BOTTOM};

struct Point {
    float x;
    float y;
};

struct Path {
    struct Point start;
    struct Point end;
    float totalDistance;
    float currentDistance;
};

struct Bullet {
    struct Point pos;
    struct Path paths[BULLET_BOUNCES];
    int pathIndex;
};

struct BounceLine {
    struct Point start;
    struct Point end;
    enum Direction direction;
};

struct Collision {
    struct BounceLine line;
    struct Point point;
};

void begin(void);
void end(void);
bool step(void);
void draw(void);
bool pointOnLine(struct Point l1, struct Point l2, struct Point p);
struct Point lineIntersection(struct Point *a, struct Point *b, struct Point *c, struct Point *d);
static int X_POS = 0;
static int Y_POS = 0;
static uint8_t ARM_ANGLE = 0; // The arm angle in degrees from [0, 255]
static struct Bullet bullets[MAX_BULLETS];
static bool FIRE_PRESSED = false;
void handleBulletFiring(void);
inline float floatMin(float a, float b);
inline float floatMax(float a, float b);
inline float floatAbs(float a);
void loadBounceLines(void);
static struct BounceLine *bounceLines;
static int bounceLinesCount;
inline int16_t getMapTile(int x, int y);
static struct Collision *collisions;
static int collisionIndex;
static struct Point currentPoint;

void begin(void) {
    X_POS = (MAP_WIDTH * WALL_SIZE)/2;
    Y_POS = (MAP_HEIGHT * WALL_SIZE)/2;
    loadBounceLines();
    collisions = malloc(bounceLinesCount * sizeof(struct Collision)); // Initialize collisions to the maximum number of possible collisions
    collisionIndex = 0;

    // Mark all bullet slots as empty
    for (int i=0; i<MAX_BULLETS; i++) {
        bullets[i].pathIndex = -1;
    }
}

bool step(void) {
    kb_Scan();
    if (kb_Data[1] & kb_Del) {
        // Exit the game
        dbg_sprintf(dbgout, "Exiting the game!\n");
        return false;
    }

    // Check move arm right
    if (kb_Data[1] & kb_2nd) {
        ARM_ANGLE += 2; // It's ok if this overflows
    }

    // Check move arm left
    if (kb_Data[2] & kb_Alpha) {
        ARM_ANGLE -= 2; // It's ok if this overflows
    }

    // Check move tank up
    if (kb_Data[7] & kb_Up) {
        Y_POS -= MOVEMENT_SPEED;
    }

    // Check move tank down
    if (kb_Data[7] & kb_Down) {
        Y_POS += MOVEMENT_SPEED;
    }

    // Check move tank left
    if (kb_Data[7] & kb_Left) {
        X_POS -= MOVEMENT_SPEED;
    }

    // Check move tank right
    if (kb_Data[7] & kb_Right) {
        X_POS += MOVEMENT_SPEED;
    }

    // Check for bullet firing
    if (kb_Data[6] & kb_Enter) {
        if (!FIRE_PRESSED) {
            FIRE_PRESSED = true;
            handleBulletFiring();
        }
    } else {
        FIRE_PRESSED = false;
    }

    // Update bullet positions
    for (int i=0; i<MAX_BULLETS; i++) {
        struct Bullet *bullet = &bullets[i];
        if (bullet->pathIndex == -1) continue; // Skip this bullet since it doesn't exist

        struct Path *path = &bullet->paths[bullet->pathIndex];
        path->currentDistance += BULLET_SPEED;

        if (path->currentDistance >= path->totalDistance) {
            bullet->pathIndex++;
            if (bullet->pathIndex == BULLET_BOUNCES) {
                // This is the end of this bullet! Mark this slot as empty
                bullet->pathIndex = -1;
                continue;
            }

            // Updated the path! Re-run this iteration
            i--;
            continue;
        }

        // Update the position of the bullet
        float progress = path->currentDistance / path->totalDistance;
        bullet->pos.x = path->start.x + ((path->end.x - path->start.x) * progress);
        bullet->pos.y = path->start.y + ((path->end.y - path->start.y) * progress);
    }

    // Do collision checks
    int tileX = X_POS / WALL_SIZE;
    int tileY = Y_POS / WALL_SIZE;
    int cornerX = X_POS - TANK_RADIUS;
    int cornerY = Y_POS - TANK_RADIUS;
    int wallRadius = WALL_SIZE / 2;
    for (int x=-1; x<2; x++) {
        for (int y=-1; y<2; y++) {
            int testTileX = tileX + x;
            int testTileY = tileY + y;
            int testTileCornerX = testTileX * WALL_SIZE;
            int testTileCornerY = testTileY * WALL_SIZE;
            if (MAP[testTileY][testTileX] == 1 || MAP[testTileY][testTileX] == 2) {
                // This is a wall! Check to see if we collide
                if (gfx_CheckRectangleHotspot(testTileCornerX, testTileCornerY, WALL_SIZE, WALL_SIZE, cornerX, cornerY, TANK_SIZE, TANK_SIZE)) {
                    // There is a collision! Figure out which side
                    float distances[4] = {
                        DISTANCE(X_POS, Y_POS, testTileCornerX + wallRadius, testTileCornerY), // Top
                        DISTANCE(X_POS, Y_POS, testTileCornerX + wallRadius, testTileCornerY + WALL_SIZE), // Bottom
                        DISTANCE(X_POS, Y_POS, testTileCornerX, testTileCornerY + wallRadius), // Left
                        DISTANCE(X_POS, Y_POS, testTileCornerX + WALL_SIZE, testTileCornerY + wallRadius) // Right
                    };

                    float shortestDistance = distances[0];
                    int shortestIndex = 0;
                    for (int i=1; i<4; i++) {
                        if (distances[i] < shortestDistance) {
                            shortestDistance = distances[i];
                            shortestIndex = i;
                        }
                    }

                    switch (shortestIndex) {
                        case 0:
                            // Top collision
                            Y_POS = testTileCornerY - TANK_RADIUS;
                            break;
                        case 1:
                            // Bottom collision
                            Y_POS = testTileCornerY + WALL_SIZE + TANK_RADIUS;
                            break;
                        case 2:
                            // Left collision
                            X_POS = testTileCornerX - TANK_RADIUS;
                            break;
                        case 3:
                            // Right collision
                            X_POS = testTileCornerX + WALL_SIZE + TANK_RADIUS;
                            break;
                    }
                }
            }
        }
    }

    return true;
}

void draw(void) {
    // Clear the screen
    gfx_ZeroScreen();

    // Draw walls
    gfx_SetColor(4); // Set color to grey
    gfx_FillRectangle(-WALL_OFFSET_X, -WALL_OFFSET_Y, (MAP_WIDTH * WALL_SIZE), WALL_SIZE);
    gfx_FillRectangle(-WALL_OFFSET_X, ((MAP_HEIGHT-1) * WALL_SIZE) - WALL_OFFSET_Y, (MAP_WIDTH * WALL_SIZE), WALL_SIZE);
    gfx_FillRectangle(-WALL_OFFSET_X, WALL_SIZE - WALL_OFFSET_Y, WALL_SIZE, (MAP_HEIGHT-2) * WALL_SIZE);
    gfx_FillRectangle(((MAP_WIDTH-1)*WALL_SIZE)-WALL_OFFSET_X, WALL_SIZE - WALL_OFFSET_Y, WALL_SIZE, (MAP_HEIGHT-2) * WALL_SIZE);
    for (uint8_t y=1; y<MAP_HEIGHT-1; y++) {
        for (uint8_t x=1; x<MAP_WIDTH-1; x++) {
            if (MAP[y][x] == 1) {
                gfx_FillRectangle((x * WALL_SIZE) - WALL_OFFSET_X, (y * WALL_SIZE) - WALL_OFFSET_Y, WALL_SIZE, WALL_SIZE);
            } else if (MAP[y][x] == 2) {
                gfx_TransparentSprite(wall, (x * WALL_SIZE) - WALL_OFFSET_X, (y * WALL_SIZE) - WALL_OFFSET_Y);
            }
        }
    }

    // Draw bullets
    gfx_SetColor(1); // Set color to black
    for (int i=0; i<MAX_BULLETS; i++) {
        if (bullets[i].pathIndex == -1) continue;

        gfx_FillCircle(bullets[i].pos.x - WALL_OFFSET_X, bullets[i].pos.y - WALL_OFFSET_Y, BULLET_RADIUS);
    }

    // Draw tank bodies
    gfx_SetColor(3); // Set color to blue
    gfx_FillRectangle_NoClip(SCREEN_MIDDLE_X - TANK_RADIUS, SCREEN_MIDDLE_Y - TANK_RADIUS, TANK_SIZE, TANK_SIZE);

    // Draw tank arms
    gfx_RotatedScaledTransparentSprite_NoClip(arm, SCREEN_MIDDLE_X-(arm_width/2), SCREEN_MIDDLE_Y-(arm_height/2), ARM_ANGLE, 64);

    /*
    // Draw text
    gfx_SetTextFGColor(1);
    gfx_SetTextXY(0, 0);
    gfx_PrintInt(X_POS, 1);
    gfx_SetTextXY(0, 10);
    gfx_PrintInt(Y_POS, 1);

    // Draw bounce lines
    gfx_SetColor(2);
    for (int i=0; i<bounceLinesCount; i++) {
        struct BounceLine *bounceLine = &bounceLines[i];
        gfx_Line(bounceLine->start.x - WALL_OFFSET_X, bounceLine->start.y - WALL_OFFSET_Y, bounceLine->end.x - WALL_OFFSET_X, bounceLine->end.y - WALL_OFFSET_Y);
    }
    */
}

void end(void) {
    // Exit graphics
}


void game(void) {
    begin(); // No rendering allowed!
    gfx_Begin();

    // Initial gfx setup
    gfx_SetPalette(global_palette, sizeof_global_palette, 0);

    gfx_SetDrawBuffer(); // Draw to the buffer to avoid rendering artifacts
    while (step()) { // No rendering allowed in step!
        draw(); // As little non-rendering logic as possible
        gfx_SwapDraw(); // Queue the buffered frame to be displayed
    }

    gfx_End();
    end();
}

void handleBulletFiring(void) {
    // Find an available bullet slot
    int bulletIndex = -1;
    for (uint8_t i=0; i<MAX_BULLETS; i++) {
        if (bullets[i].pathIndex == -1) {
            bulletIndex = i;
            break;
        }
    }
    if (bulletIndex == -1) return; // No bullet slot available!

    dbg_sprintf(dbgout, "Firing a bullet!\n");

    // Initialize
    currentPoint.x = X_POS;
    currentPoint.y = Y_POS;
    float currentAngle = BYTEANGLE_TO_RADIANS(ARM_ANGLE);
    for (uint8_t i=0; i<BULLET_BOUNCES; i++) {
        dbg_sprintf(dbgout, "Current angle in radians: %f\n", currentAngle);
        dbg_sprintf(dbgout, "Current point: (%f, %f)\n", currentPoint.x, currentPoint.y);
        collisionIndex = 0;
        struct Point rayPoint;
        rayPoint.x = currentPoint.x + (cos(currentAngle) * RAY_LENGTH);
        rayPoint.y = currentPoint.y - (sin(currentAngle) * RAY_LENGTH);
        gfx_SetColor(2);
        gfx_Line(currentPoint.x - WALL_OFFSET_X, currentPoint.y - WALL_OFFSET_Y, rayPoint.x - WALL_OFFSET_X, rayPoint.y - WALL_OFFSET_Y);

        // Find all collisions
        for (int j=0; j<bounceLinesCount; j++) {
            struct BounceLine *line = &bounceLines[j];
            struct Point intersection = lineIntersection(&currentPoint, &rayPoint, &line->start, &line->end);
            if (intersection.x != __FLT_MAX__) {
                // We have a valid intersection!
                dbg_sprintf(dbgout, "Start line: (%f, %f), (%f, %f)\n", currentPoint.x, currentPoint.y, rayPoint.x, rayPoint.y);
                dbg_sprintf(dbgout, "End line: (%f, %f), (%f, %f)\n", line->start.x, line->start.y, line->end.x, line->end.y);
                dbg_sprintf(dbgout, "Intersection: (%f, %f)\n", intersection.x, intersection.y);
                gfx_SetColor(7);
                gfx_FillCircle(intersection.x - WALL_OFFSET_X, intersection.y - WALL_OFFSET_Y, 5);
                collisions[collisionIndex].point.x = intersection.x;
                collisions[collisionIndex].point.y = intersection.y;
                collisions[collisionIndex].line = *line;
                collisionIndex++;
            }
        }

        dbg_sprintf(dbgout, "Found %d collisions\n", collisionIndex);

        if (collisionIndex == 0) return; // Somehow no collisions were found... don't shoot!

        // Find the closest collision
        float closestDistance = POINT_DISTANCE(currentPoint, collisions[0].point);
        int closestIndex = 0;
        for (int j=1; j<(MAP_WIDTH*MAP_HEIGHT*4); j++) {
            float distance = POINT_DISTANCE(currentPoint, collisions[j].point);
            if (distance < closestDistance) {
                closestDistance = distance;
                closestIndex = j;
            }
        }

        // Assign this path
        bullets[bulletIndex].paths[i].start = currentPoint;
        bullets[bulletIndex].paths[i].end = collisions[closestIndex].point;
        bullets[bulletIndex].paths[i].currentDistance = 0;
        bullets[bulletIndex].paths[i].totalDistance = closestDistance;
        dbg_sprintf(dbgout, "Path:\n");
        dbg_sprintf(dbgout, "Start point: (%f, %f)\n", currentPoint.x, currentPoint.y);
        dbg_sprintf(dbgout, "Closest intersection: (%f, %f)\n", collisions[closestIndex].point.x, collisions[closestIndex].point.y);

        currentPoint = collisions[closestIndex].point;
        dbg_sprintf(dbgout, "Before angle: %f\n", currentAngle);
        switch (collisions[closestIndex].line.direction) {
            case TOP:
            case BOTTOM:
                currentAngle = FLIP_RADIAN_VERTICALLY(currentAngle);
                dbg_sprintf(dbgout, "Flipped vertically\n");
                break;
            case LEFT:
            case RIGHT:
                currentAngle = FLIP_RADIAN_HORIZONTALLY(currentAngle);
                dbg_sprintf(dbgout, "Flipped horizontally\n");
                break;
        }
        dbg_sprintf(dbgout, "After angle: %f\n", currentAngle);
    }

    // Assign this bullet
    bullets[bulletIndex].pathIndex = 0;
}

inline float floatMin(float a, float b) {
    if (a < b) return a;
    else return b;
}

inline float floatMax(float a, float b) {
    if (a > b) return a;
    else return b;
}

inline float floatAbs(float a) {
    if (a < 0) return a * -1;
    return a;
}

// Adapted from: https://www.geeksforgeeks.org/program-for-point-of-intersection-of-two-lines/
bool pointOnLine(struct Point l1, struct Point l2, struct Point p) {
    return (floatMin(l1.x, l2.x) <= p.x && p.x <= floatMax(l1.x, l2.x)) && (floatMin(l1.y, l2.y) <= p.y && p.y <= floatMax(l1.y, l2.y));
}

// Adapted from: https://www.geeksforgeeks.org/program-for-point-of-intersection-of-two-lines/
struct Point lineIntersection(struct Point *a, struct Point *b, struct Point *c, struct Point *d) {
    /* Will either return a Point containing the location
    of the intersection, or a Point containing __FLT_MAX__ meaning
    that the two lines do not intersect.
    */

    //dbg_sprintf(dbgout, "Test start line: (%f, %f) to (%f, %f)\n", a->x, a->y, b->x, b->y);
    //dbg_sprintf(dbgout, "Test end line: (%f, %f) to (%f, %f)\n", c->x, c->y, d->x, d->y);

    float a1 = b->y - a->y;
    float b1 = a->x - b->x;
    float c1 = (a1 * a->x) + (b1 * a->y);

    float a2 = d->y - c->y;
    float b2 = c->x - d->x;
    float c2 = (a2 * c->x) + (b2 * c->y);

    float determinant = (a1 * b2) - (a2 * b1);

    if (determinant == 0) {
        struct Point result;
        result.x = __FLT_MAX__;
        result.y = __FLT_MAX__;
        //dbg_sprintf(dbgout, "0 Determinant\n");
        return result;
    } else {
        float x = (b2 * c1 - b1 * c2) / determinant;
        float y = (a1 * c2 - a2 * c1) / determinant;

        struct Point result;
        result.x = x;
        result.y = y;

        if (pointOnLine(*a, *b, result) && pointOnLine(*c, *d, result)) {
            return result;
        }
        // The lines do not touch
        //dbg_sprintf(dbgout, "Do not touch\n");
        result.x = __FLT_MAX__;
        result.y = __FLT_MAX__;
        return result;
    }
}

inline int16_t getMapTile(int x, int y) {
    if (x < 0 || x >= MAP_WIDTH || y < 0 || y > MAP_HEIGHT) return -1;
    return MAP[y][x];
}

void loadBounceLines(void) {
    free(bounceLines);
    bounceLinesCount = 0;
    bounceLines = malloc(0);

    for (int y=0; y<MAP_HEIGHT; y++) {
        for (int x=0; x<MAP_WIDTH; x++) {
            uint8_t value = MAP[y][x];

            int leftX = x * WALL_SIZE;
            int rightX = (x+1) * WALL_SIZE;
            int topY = y * WALL_SIZE;
            int bottomY = (y+1) * WALL_SIZE;

            if (value == 0 || value == 2) {
                if (getMapTile(x-1, y) == 1) {
                    // Tile to the left is solid
                    bounceLinesCount++;
                    bounceLines = realloc(bounceLines, bounceLinesCount * sizeof(struct BounceLine));
                    struct BounceLine *bounceLine = &bounceLines[bounceLinesCount-1];
                    bounceLine->start.x = leftX;
                    bounceLine->start.y = topY;
                    bounceLine->end.x = leftX;
                    bounceLine->end.y = bottomY;
                    bounceLine->direction = RIGHT;
                }
                if (getMapTile(x+1, y) == 1) {
                    // Tile to the right is solid
                    bounceLinesCount++;
                    bounceLines = realloc(bounceLines, bounceLinesCount * sizeof(struct BounceLine));
                    struct BounceLine *bounceLine = &bounceLines[bounceLinesCount-1];
                    bounceLine->start.x = rightX;
                    bounceLine->start.y = topY;
                    bounceLine->end.x = rightX;
                    bounceLine->end.y = bottomY;
                    bounceLine->direction = LEFT;
                }
                if (getMapTile(x, y-1) == 1) {
                    // Tile above is solid
                    bounceLinesCount++;
                    bounceLines = realloc(bounceLines, bounceLinesCount * sizeof(struct BounceLine));
                    struct BounceLine *bounceLine = &bounceLines[bounceLinesCount-1];
                    bounceLine->start.x = leftX;
                    bounceLine->start.y = topY;
                    bounceLine->end.x = rightX;
                    bounceLine->end.y = topY;
                    bounceLine->direction = BOTTOM;
                }
                if (getMapTile(x, y+1) == 1) {
                    // Tile above is solid
                    bounceLinesCount++;
                    bounceLines = realloc(bounceLines, bounceLinesCount * sizeof(struct BounceLine));
                    struct BounceLine *bounceLine = &bounceLines[bounceLinesCount-1];
                    bounceLine->start.x = leftX;
                    bounceLine->start.y = bottomY;
                    bounceLine->end.x = rightX;
                    bounceLine->end.y = bottomY;
                    bounceLine->direction = TOP;
                }
            }
        }
    }

    // Merge lines of same direction
    double mergeTolerance = 5;
    for (int x=0; x<bounceLinesCount; x++) {
        // Check if the current line's end is another's start
        for (int y=0; y<bounceLinesCount; y++) {
            if (y == x) continue;
            if (
                floatAbs(bounceLines[x].end.x - bounceLines[y].start.x) < mergeTolerance &&
                floatAbs(bounceLines[x].end.y - bounceLines[y].start.y) < mergeTolerance &&
                bounceLines[x].direction == bounceLines[y].direction
            ) {
                // The lines are connected. Link them
                bounceLines[x].end.x = bounceLines[y].end.x;
                bounceLines[x].end.y = bounceLines[y].end.y;

                // Copy out the touching line
                struct BounceLine *newBounceLines = malloc((bounceLinesCount-1) * sizeof(struct BounceLine));
                for (int z=0; z<y; z++) newBounceLines[z] = bounceLines[z];
                for (int z=y+1; z<bounceLinesCount; z++) newBounceLines[z-1] = bounceLines[z];
                free(bounceLines);
                bounceLines = newBounceLines;
                bounceLinesCount--;

                // Reset the for loop
                x = -1;
                break;
            }
        }
    }
}

/* Main function, called first */
int main(void)
{
    game();
    return 0;
}