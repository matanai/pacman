/******************************************************************************
MIT License
Copyright (c) 2020 matanai
Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*******************************************************************************/

#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>

#define WINDOW_TITLE "Pacman"
#define SIZE_TILE 20
#define FPS 15

#define SCREEN_WIDTH 560
#define SCREEN_HEIGHT 660

typedef enum { noFood = 0, smallBall = 1, largeBall = 2 } foodName;
typedef enum { north = 0, south = 1, west = 2, east = 3 } neighbourName;
typedef enum { up = 1, down = 2, left = 3, right = 4, idle = 5 } headingName; 
typedef enum { scatter = 1, frightened = 2, eaten = 3, chase = 4, home = 5 } stateName;
typedef enum { blinky = 0, pinky = 1, inky = 2, clyde = 3 } ghostName;

typedef struct {
    float gridX, gridY;
    foodName food;
    SDL_bool isWall;
} gridClass;

gridClass grid[33][30] = {{{ 0 }}};

typedef struct nodeClass {
    gridClass *gridPtr;
    float nodeX, nodeY, g, f;
    SDL_bool isWall, isVisited;
    struct nodeClass *nodeParent, *allNeighbours[4];
} nodeClass;

nodeClass *nodeStart = NULL;
nodeClass *nodeEnd = NULL;
nodeClass nodes[33][30] = {{{ 0 }}};

typedef struct {
    float speed, posX, posY;
    short vector[2];
    headingName curHeading, newHeading;
    SDL_bool isAlive, isMoving;
    gridClass *curGridPos, *newGridPos;
    SDL_Rect pacmanTextureCrop, killTextureCrop;
    unsigned int timeFrame;
} playerClass;

typedef struct {
    float speed, posX, posY; 
    short vector[2];
    headingName heading;
    stateName state;
    gridClass *target, *curGridPos, *newGridPos, *scatterPointOne, *scatterPointTwo;
    SDL_bool isMoving, isRandLocationSet, isTimeAlmostEnd;
    SDL_Rect ghostTextureCrop;
    unsigned int timeEnd;
} enemyClass;

typedef struct listClass {
    nodeClass *nodePtr;
    struct listClass *next;
} listClass;

listClass *listHead = NULL;

typedef struct {
    SDL_bool gameOver;
    unsigned short playerLives;
    unsigned int ballsLeft, timeDelay, currentScore, highestScore;
} gameClass;

// PATHFINDING ROUTINES

float doDistance(const gridClass *a, const gridClass *b)
{
    return fabsf(a -> gridX - b -> gridX) + fabsf(a -> gridY - b -> gridY);
}

void doListPush(nodeClass* node)
{
    listClass *tmp = (listClass*)malloc(sizeof(listClass));

    tmp -> nodePtr = node; 
    tmp -> next = listHead;
    listHead = tmp;
}

void doListSort(void)
{
    listClass *tmp1 = NULL, *tmp2 = NULL;
    nodeClass *node = NULL;

    if (!listHead || listHead -> next == NULL)
    {
        return;
    }

    for (tmp1 = listHead; tmp1 != NULL; tmp1 = tmp1 -> next)
    {
        for (tmp2 = tmp1 -> next; tmp2 != NULL; tmp2 = tmp2 -> next)
        {
            if (tmp2 -> nodePtr -> f < tmp1 -> nodePtr -> f)
            {
                node = tmp1 -> nodePtr;
                tmp1 -> nodePtr = tmp2 ->nodePtr;
                tmp2 -> nodePtr = node;
            }
        }
    }
}

void doListPopVisited(void)
{
    listClass **tmp1 = &listHead;
    
    while (*tmp1)
    {
        if ((*tmp1) -> nodePtr -> isVisited)
        {
            listClass *tmp2 = *tmp1;
            *tmp1 = (*tmp1) -> next;
            free(tmp2);
        } 
        else
        {
            tmp1 = &(*tmp1) -> next;
        }
    }
}

void doListDelete(void)
{
    listClass *tmp = listHead;

    while (tmp)
    {
        listHead = listHead -> next;
        free(tmp);
        tmp = listHead;
    }
}

void doInitNodes(const enemyClass* enemy)
{
    // initiate nodes
    for (int y = 0; y < 31; y++)
    {
        for (int x = 0; x < 30; x++)
        {
            nodes[y][x].gridPtr = &grid[y][x];
            nodes[y][x].nodeX = grid[y][x].gridX;
            nodes[y][x].nodeY = grid[y][x].gridY;
            nodes[y][x].g = nodes[y][x].f = INFINITY;
            nodes[y][x].isWall = grid[y][x].isWall;
            nodes[y][x].isVisited = SDL_FALSE;
            nodes[y][x].nodeParent = NULL;
            
            if (nodes[y][x].gridPtr == enemy -> curGridPos)
            {
                nodeStart = &nodes[y][x];
                nodeStart -> f = nodeStart -> g = 0.0f;
                doListPush(nodeStart);
            }

            if (nodes[y][x].gridPtr == enemy -> target)
            {
                nodeEnd = &nodes[y][x];
            }
        }
    }

    // establish connections
    for (int y = 0; y < 31; y++) 
    {
        for (int x = 0; x < 30; x++)
        {
            if (y > 0)
            {
                nodes[y][x].allNeighbours[north] = &nodes[y - 1][x];
            }
            if (y < 33)
            {
                nodes[y][x].allNeighbours[south] = &nodes[y + 1][x];
            }
            if (x > 0)
            {
                nodes[y][x].allNeighbours[west] = &nodes[y][x - 1];
            }
            if (x < 30)
            {
                nodes[y][x].allNeighbours[east] = &nodes[y][x + 1];
            }
        }
    }

    // specifically establish connections between portals
    nodes[14][0].allNeighbours[west] = &nodes[14][29];
    nodes[14][29].allNeighbours[east] = &nodes[14][0];
}

void doRestrictMovingBack(nodeClass* nodeStart, const enemyClass* enemy)
{
    if (enemy -> state == scatter || enemy -> state == chase)
    {
        switch (enemy -> heading)
        {
            case up: 
                nodeStart -> allNeighbours[south] = NULL; 
            break;

            case down: 
                nodeStart -> allNeighbours[north] = NULL; 
            break;
            
            case left: 
                nodeStart -> allNeighbours[east] = NULL; 
            break;
            
            case right: 
                nodeStart -> allNeighbours[west] = NULL; 
            break;
            
            case idle: 
            break;
        }
    }
}

void doPathFinding(const enemyClass* enemy)
{
    nodeClass *nodeCurrent = NULL;
    nodeClass *nodeNeighbour = NULL;

    // clear list
    if (listHead)
    {
        doListDelete();
    }

    doInitNodes(enemy);
    doRestrictMovingBack(nodeStart, enemy);
    
    while (listHead)
    {
        doListSort();
        doListPopVisited();

        nodeCurrent = listHead -> nodePtr;
        nodeCurrent -> isVisited = SDL_TRUE;

        if (nodeCurrent == nodeEnd)
        {
            return;
        }

        for (int i = 0; i < 4; i++)
        {
            nodeNeighbour = nodeCurrent -> allNeighbours[i];
        
            if (nodeNeighbour && !nodeNeighbour -> isWall && !nodeNeighbour -> isVisited)
            {
                // variable to check if this path to neighbour is shorter
                float tmp = nodeCurrent -> g + doDistance(nodeNeighbour -> gridPtr, nodeCurrent -> gridPtr);

                if (tmp < nodeNeighbour -> g)
                {
                    if (nodeNeighbour -> g == INFINITY)
                    {
                        doListPush(nodeNeighbour);
                    }

                    nodeNeighbour -> nodeParent = nodeCurrent;
                    nodeNeighbour -> g = tmp;
                    nodeNeighbour -> f = nodeNeighbour -> g + doDistance(nodeNeighbour -> gridPtr, nodeEnd -> gridPtr);
                }
            }
        }
    }
}

// TELEPORT

void doTeleport(float* posX, float* posY, gridClass** curGridPos, gridClass** newGridPos, const headingName heading)
{
    // we can't take both enemy and player type, but we can take individual fields from each type

    if (*curGridPos == &grid[14][0] && heading == left)
    {
        *curGridPos = &grid[14][29];
        *newGridPos = &grid[14][29];
        *posX = grid[14][29].gridX;
        *posY = grid[14][29].gridY;
    }
    else if (*curGridPos == &grid[14][29] && heading == right)
    {
        *curGridPos = &grid[14][0];
        *newGridPos = &grid[14][0];
        *posX = grid[14][0].gridX;
        *posY = grid[14][0].gridY;
    }
}

// PLAYER ROUTINES

SDL_bool doGetPlayerComand(SDL_Window* window, SDL_Event* event, gameClass* game, playerClass* player)
{
    while(SDL_PollEvent(event)) 
    {
        switch(event -> type) 
        {
            case SDL_WINDOWEVENT_CLOSE: case SDL_QUIT: 
                return SDL_TRUE; 
            break;
            
            case SDL_KEYDOWN:
                switch(event -> key.keysym.sym) 
                {
                    case SDLK_ESCAPE: 
                        return SDL_TRUE; 
                    break;
                    
                    case SDLK_SPACE: 
                        game -> gameOver = SDL_FALSE; 
                    break;
                    
                    case SDLK_UP: 
                        player -> newHeading = up; 
                    break;
                    
                    case SDLK_DOWN: 
                        player -> newHeading = down; 
                    break;                    
                    
                    case SDLK_LEFT: 
                        player -> newHeading = left; 
                    break;                
                    
                    case SDLK_RIGHT: 
                        player -> newHeading = right; 
                    break;                    
                    
                    default: 
                        player -> newHeading = idle; 
                    break;
                }
            break;
        }
    }
    return SDL_FALSE;
}

void doUpdatePlayerHeading(playerClass* player)
{
    switch (player -> newHeading) 
    {
        case up: 
            if ( !(player -> newGridPos - 30) -> isWall)
            {
                player -> curHeading = up;
            }
        break;

        case down:
            if ( !(player -> newGridPos + 30) -> isWall)
            {
                player -> curHeading = down;
            }
        break;
        
        case left:
            if ( !(player -> newGridPos - 1) -> isWall)
            {
                player -> curHeading = left; 
            }
        break;
        
        case right:
            if (!(player -> newGridPos + 1) -> isWall)
            {
                player -> curHeading = right;
            }
        break;

        case idle: 
        break;
    }

    switch (player -> curHeading) 
    {
        case up: 
            if ( !(player -> newGridPos - 30) -> isWall)
            {
                player -> newGridPos -= 30;
            }
        break;
        
        case down:
            if ( !(player -> newGridPos + 30) -> isWall)
            {
                player -> newGridPos += 30;
            }
        break;
        
        case left:
            if ( !(player -> newGridPos - 1) -> isWall)
            {
                player -> newGridPos -= 1;
            }
        break; 
        
        case right:
            if ( !(player -> newGridPos + 1) -> isWall)
            {
                player -> newGridPos += 1;
            }
        break;

        case idle:
        break;
    }
}

SDL_bool doPlayerMove(SDL_Window* window, SDL_Event* event, gameClass* game, playerClass* player)
{  
    SDL_bool tmp = SDL_FALSE;
    
    if (player -> isMoving == SDL_TRUE)
    {
        if (player -> posX == player -> newGridPos -> gridX && player -> posY == player -> newGridPos -> gridY)
        {
            player -> curGridPos = player -> newGridPos;
            player -> vector[0] = player -> vector[1] = 0;
            tmp = doGetPlayerComand(window, event, game, player);
            doUpdatePlayerHeading(player);
        } 
        else
        {
            player -> vector[0] = (int)(player -> newGridPos -> gridX - player -> curGridPos -> gridX) / SIZE_TILE ; // 1, -1, 0
            player -> vector[1] = (int)(player -> newGridPos -> gridY - player -> curGridPos -> gridY) / SIZE_TILE ; // 1, -1, 0
            player -> posX += player -> speed * (float)player -> vector[0];
            player -> posY += player -> speed * (float)player -> vector[1];
        }
        doTeleport(&player -> posX, &player -> posY, &player -> curGridPos, &player -> newGridPos, player -> curHeading);
    } 
    else
    {
        player -> newHeading = idle;
        tmp = doGetPlayerComand(window, event, game, player);
    }

    return tmp;
}

// ENEMY ROUTINES

SDL_bool doSetTimer(const unsigned int time, enemyClass* enemy)
{
    if (!enemy -> timeEnd)
    {
        enemy -> timeEnd = SDL_GetTicks() / 1000 + time;
    }

    if (enemy -> timeEnd && enemy -> timeEnd - SDL_GetTicks() / 1000 < 3)
    {
        enemy -> isTimeAlmostEnd = SDL_TRUE;
    }

    if (enemy -> timeEnd && enemy -> timeEnd == SDL_GetTicks() / 1000)
    {
        enemy -> timeEnd = enemy -> isTimeAlmostEnd = 0;
        return SDL_TRUE;
    }

    return SDL_FALSE; 
}

gridClass* doGetRandomLocation(enemyClass* enemy)
{
    gridClass *tmp = NULL;
    int rowRand, colRand;

    if (enemy -> state != home)
    {
        do {
            rowRand = rand() % (29 + 1 - 1) + 1;
            colRand = rand() % (27 + 1 - 2) + 2;
            tmp = &grid[rowRand][colRand];
        } while (tmp -> isWall);
    } 
    else
    {
        rowRand = rand() % (15 + 1 - 13) + 13;
        colRand = rand() % (17 + 1 - 12) + 12;
        tmp = &grid[rowRand][colRand];
    }

    return tmp;
} 

void doEnemyScatter(enemyClass* enemy)
{
    if (enemy -> curGridPos != enemy -> scatterPointOne)
    {
        enemy -> target = enemy -> scatterPointOne;
    }

    if (enemy -> curGridPos != enemy -> scatterPointTwo)
    {
        enemy -> target = enemy -> scatterPointTwo;
    }
}

void doPinkySearch(const playerClass* player, enemyClass* enemy)
{
    gridClass *tmp = player -> curGridPos; 

    switch (player -> curHeading)
    {
        case up: 
            tmp -= 90; 
        break;
        
        case down: 
            tmp += 90; 
        break;
        
        case left: 
            tmp -= 3; 
        break;
        
        case right: 
            tmp += 3; 
        break;
        
        case idle: 
        break;
    }

    !tmp -> isWall ? (enemy -> target = tmp) : (enemy -> target = player -> curGridPos);

    if (enemy -> curGridPos == enemy -> target)
    {
        doEnemyScatter(enemy);
    }
}

void doInkySearch(const playerClass* player, enemyClass* enemy)
{
    float tmp = doDistance(player -> curGridPos, enemy -> curGridPos) / SIZE_TILE;

    tmp <= 8 ? doEnemyScatter(enemy) : (enemy -> target = player -> curGridPos);
}

void doClydeSearch(const playerClass* player, const enemyClass* blinky, enemyClass* clyde)
{
    float coordX, coordY, dist;
    gridClass* tmp1 = player -> curGridPos; 

    switch (player -> curHeading)
    {
        case up: 
            tmp1 -= 60; 
        break;
        
        case down: 
            tmp1 += 60; 
        break;
        
        case left: 
            tmp1 -= 2; 
        break;
        
        case right: 
            tmp1 += 2; 
        break;
        
        case idle: 
        break;
    }

    dist = doDistance(blinky -> curGridPos, tmp1);
    coordX = tmp1 -> gridX - 2 * dist * (tmp1 -> gridX - blinky -> curGridPos -> gridX) / dist;
    coordY = tmp1 -> gridY - 2 * dist * (tmp1 -> gridY - blinky -> curGridPos -> gridY) / dist;

    if ( (coordX < SCREEN_WIDTH && coordX >= SIZE_TILE) && (coordY < SCREEN_HEIGHT && coordY >= SIZE_TILE) )
    {
        gridClass* tmp2 = NULL;

        for (int y = 2; y < 33; y++)
        {
            for (int x = 0; x < 30; x++)
            {
                if (grid[y][x].gridX == coordX && grid[y][x].gridY == coordY) 
                    tmp2 = &grid[y][x];
            }
        }

        if (tmp2 && !tmp2 -> isWall)
        {
            clyde -> target = tmp2;
        }

        if (clyde -> curGridPos == tmp2)
        {
            doEnemyScatter(clyde);
        }

    } else
    {
        clyde -> target = player -> curGridPos;
    }
}

void doUpdateEnemyState(const gameClass* game, const playerClass* player, enemyClass* enemy)
{
    int time = 0;
    game -> ballsLeft < 100 ? (time = 3) : (time = 7); 

    for (int i = 0; i < 4; i++)
    {
        switch (enemy[i].state) 
        {
            case home:

                if (game -> ballsLeft < 215)
                {
                    enemy[i].state = scatter;
                }
                else 
                {
                    if (!enemy[i].isRandLocationSet)
                    {
                        enemy[i].target = doGetRandomLocation(&enemy[i]);
                        enemy[i].isRandLocationSet = SDL_TRUE;
                    }

                    if (enemy[i].isRandLocationSet == SDL_TRUE && enemy[i].curGridPos == enemy[i].target)
                    {
                        enemy[i].isRandLocationSet = SDL_FALSE;
                    }
                }
            break;

            case scatter:   
                doEnemyScatter(&enemy[i]); 
                if (doSetTimer(time, &enemy[i])) 
                {
                    enemy[i].state = chase;
                }
            break;
        
            case chase:
                switch (i)
                {
                    case blinky: 
                        enemy[blinky].target = player -> curGridPos; 
                    break;
                    
                    case pinky: 
                        doPinkySearch(player, &enemy[pinky]); 
                    break;
                    
                    case inky: 
                        doInkySearch(player, &enemy[inky]); 
                    break;
                    
                    case clyde: 
                        doClydeSearch(player, &enemy[blinky], &enemy[clyde]); 
                    break;
                }

                enemy[i].isRandLocationSet = SDL_FALSE;

                if (doSetTimer(20, &enemy[i])) 
                {
                    enemy[i].state = scatter;
                }
            break;

            case frightened:
                if (!enemy[i].isRandLocationSet) 
                {
                    enemy[i].target = doGetRandomLocation(&enemy[i]);
                    enemy[i].isRandLocationSet = SDL_TRUE;
                }
        
                if (enemy[i].isRandLocationSet && enemy[i].curGridPos == enemy[i].target) 
                {
                    enemy[i].isRandLocationSet = SDL_FALSE;
                }

                if (doSetTimer(time, &enemy[i])) 
                {
                    enemy[i].state = chase;
                }
            break;

            case eaten:
                enemy[i].target = &grid[14][14];
                enemy[i].isRandLocationSet = SDL_FALSE; 

                if (enemy[i].curGridPos == &grid[14][14]) 
                {
                    enemy[i].state = chase;
                }
            break;
        }
    }
}

void doUpdateEnemySpeed(gameClass* game, enemyClass* enemy)
{
    switch (enemy -> state) 
    {
        case chase: case scatter: case home: 
            game -> ballsLeft < 50 ? (enemy -> speed = 2.5f) : (enemy -> speed = 2.0f); 
        break;
        
        case frightened: 
            enemy -> speed = 0.5f; 
        break;
        
        case eaten:
            enemy -> speed = 4.0f; 
        break;
    }
}

void doUpdateEnemyHeading(enemyClass* enemy)
{
    if (enemy -> vector[0] == 0 && enemy -> vector[1] < 0)
    {
        enemy -> heading = up;
    }
    
    if (enemy -> vector[0] == 0 && enemy -> vector[1] > 0)
    {
        enemy -> heading = down;
    }
    
    if (enemy -> vector[0] < 0 && enemy -> vector[1] == 0)
    {
        enemy -> heading = left;
    }

    if (enemy -> vector[0] > 0 && enemy -> vector[1] == 0)
    {
        enemy -> heading = right;
    }
}

void doEnemyMove(gameClass* game, const playerClass* player, enemyClass* enemy)
{
    for (int i = 0; i < 4; i++)
    {  
        if (!enemy[i].isMoving)
        {   
            doPathFinding(&enemy[i]);

            nodeClass *tmp = nodeEnd;
            while (tmp != nodeStart)
            {
                enemy[i].newGridPos = tmp -> gridPtr; 
                tmp = tmp -> nodeParent;
            }

            enemy[i].vector[0] = (int)(enemy[i].newGridPos -> gridX - enemy[i].curGridPos -> gridX) / SIZE_TILE; // 1, -1, 0
            enemy[i].vector[1] = (int)(enemy[i].newGridPos -> gridY - enemy[i].curGridPos -> gridY) / SIZE_TILE; // 1, -1, 0
            enemy[i].isMoving = SDL_TRUE;
            doUpdateEnemyHeading(&enemy[i]);
        }
    
        // after enemy move is finished, we update his position and state
        if (enemy[i].posX == enemy[i].newGridPos -> gridX && enemy[i].posY == enemy[i].newGridPos -> gridY)
        {
            enemy[i].isMoving = SDL_FALSE;
            enemy[i].curGridPos = enemy[i].newGridPos;
            enemy[i].vector[0] = enemy[i].vector[1] = 0;
            doUpdateEnemySpeed(game, &enemy[i]);
        } else 
        {
            enemy[i].posX += enemy[i].speed * (float)enemy[i].vector[0]; 
            enemy[i].posY += enemy[i].speed * (float)enemy[i].vector[1];
        }
        doTeleport(&enemy[i].posX, &enemy[i].posY, &enemy[i].curGridPos, &enemy[i].newGridPos, enemy[i].heading);
    }
}

// GLOBAL EVENTS THAT AFFECT BOTH PLAYER AND ENEMY

void doEatFood(gameClass* game, playerClass* player, enemyClass* enemy)
{
    if (player -> curGridPos -> food == smallBall)
    {
        player -> curGridPos -> food = noFood;
        game -> currentScore += 10;   
        game -> ballsLeft--; 
    } 
    else if (player -> curGridPos -> food == largeBall)
    {
        player -> curGridPos -> food = noFood;
        game -> currentScore += 100;
        game -> ballsLeft--;
        for (int i = 0; i < 4; i++)
        {
            if (enemy[i].state != eaten)
            {
                enemy[i].state = frightened;
                enemy[i].timeEnd = enemy[i].isTimeAlmostEnd = 0;
            }
        }
    }
}

// PROCESS ENCOUNTERS

SDL_bool doCollisionBox(const playerClass* player, const enemyClass* enemy)
{
    SDL_bool checkX = SDL_FALSE;
    SDL_bool checkY = SDL_FALSE;

    checkX = fabsf(player -> posX - enemy -> posX) * 2.0f < (float)(SIZE_TILE * 2); 
    checkY = fabsf(player -> posY - enemy -> posY) * 2.0f < (float)(SIZE_TILE * 2); 
    
    if (checkX && checkY)
    {
        return SDL_TRUE;
    }
    
    return SDL_FALSE;
}

void doCheckEncounter(gameClass* game, playerClass* player, enemyClass* enemy)
{
    for (int i = 0; i < 4; i++)
    {
        if (doCollisionBox(player, &enemy[i]))
        {
            if (enemy[i].state == frightened)
            {
                enemy[i].state = eaten;
                game -> currentScore += 100;
            }
            
            if (enemy[i].state == scatter || enemy[i].state == chase)
            {
                player -> isAlive = SDL_FALSE;
            }
        }
    }
}

// GAME PAUSE

SDL_bool doGamePause(gameClass* game, const unsigned int time)
{
    if (!game -> timeDelay)
    {
        game -> timeDelay = SDL_GetTicks() / 1000 + time;
    }

    if (game -> timeDelay && game -> timeDelay == SDL_GetTicks() / 1000)
    {
        game -> timeDelay = 0;
        return SDL_TRUE;
    }
    
    return SDL_FALSE;
}

// INIT CHARACTERS

void doInitPlayer(playerClass* player)
{
    player -> isAlive = SDL_TRUE; 
    player -> speed = 4.0f; 
    player -> posX = grid[23][14].gridX; 
    player -> posY = grid[23][14].gridY;
    player -> curGridPos = player -> newGridPos = &grid[23][14]; 
    player -> vector[0] = player -> vector[1] = 0;
    player -> curHeading = player -> newHeading = idle;
    player -> pacmanTextureCrop.x = player -> pacmanTextureCrop.y = 0; 
    player -> pacmanTextureCrop.w = player -> pacmanTextureCrop.h = 32; 
    player -> killTextureCrop.x = player -> killTextureCrop.y = 0; 
    player -> killTextureCrop.w = player -> killTextureCrop.h = 32; 
    player -> timeFrame = 0;
}

void doInitEnemy(enemyClass* enemy)
{
    for (int i = 0; i < 4; i++)
    {
        enemy[i].target = NULL;
        enemy[i].speed = 2.5f;
        enemy[i].vector[0] = enemy[i].vector[1] = 0;
        enemy[i].ghostTextureCrop.x = enemy[i].ghostTextureCrop.y = 0;
        enemy[i].ghostTextureCrop.w = enemy[i].ghostTextureCrop.h = 32;
        enemy[i].isMoving = enemy[i].isRandLocationSet = enemy[i].isTimeAlmostEnd = SDL_FALSE;  
        enemy[i].timeEnd = 0;

        switch (i) 
        {
            case blinky:
                enemy[i].heading = up;
                enemy[i].state = scatter;
                enemy[i].posX = grid[11][14].gridX;
                enemy[i].posY = grid[11][14].gridY;
                enemy[i].curGridPos = enemy[i].newGridPos = &grid[11][14];
                enemy[i].scatterPointOne = &grid[5][27];
                enemy[i].scatterPointTwo = &grid[1][22];
            break;
        
            case pinky:
                enemy[i].heading = left;
                enemy[i].state = home;
                enemy[i].posX = grid[14][13].gridX;
                enemy[i].posY = grid[14][13].gridY;
                enemy[i].curGridPos = enemy[i].newGridPos = &grid[14][13];
                enemy[i].scatterPointOne = &grid[1][7];
                enemy[i].scatterPointTwo = &grid[5][2];
            break;

            case inky:
                enemy[i].heading = down;
                enemy[i].state = home;
                enemy[i].posX = grid[14][14].gridX;
                enemy[i].posY = grid[14][14].gridY;
                enemy[i].curGridPos = enemy[i].newGridPos = &grid[14][14];
                enemy[i].scatterPointOne = &grid[23][7];
                enemy[i].scatterPointTwo = &grid[29][8];
            break;

            case clyde:
                enemy[i].heading = right;
                enemy[i].state = home;
                enemy[i].posX = grid[14][15].gridX;
                enemy[i].posY = grid[14][15].gridY;
                enemy[i].curGridPos = enemy[i].newGridPos = &grid[14][15];
                enemy[i].scatterPointOne = &grid[23][22];
                enemy[i].scatterPointTwo = &grid[29][21];
            break;
        }
    }
}

// INIT GRID

void doInitGrid(void) 
{
    int gridWallInit[33][30] = { 
        { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1 },
        { 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1 },
        { 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1 },
        { 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1 },
        { 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1 },
        { 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1 },
        { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
        { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
        { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
        { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 0, 0, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
        { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
        { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
        { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
        { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
        { 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1 },
        { 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1 },
        { 1, 1, 0, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 0, 1, 1 },
        { 1, 1, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 1, 1 },
        { 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1 },
        { 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 0, 1, 1 },
        { 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1 },
        { 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1 },
        { 1, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1 },
        { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    };

    int gridFoodlInit[33][30] = { 
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 
        { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 }, 
        { 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0 }, 
        { 0, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0 }, 
        { 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0 }, 
        { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 }, 
        { 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0 }, 
        { 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0 }, 
        { 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0 }, 
        { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }, 
        { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }, 
        { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }, 
        { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }, 
        { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }, 
        { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }, 
        { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }, 
        { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }, 
        { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }, 
        { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }, 
        { 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0 }, 
        { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 }, 
        { 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0 }, 
        { 0, 0, 2, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 2, 0, 0 }, 
        { 0, 0, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 1, 0, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 0, 0 }, 
        { 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0 }, 
        { 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0 }, 
        { 0, 0, 1, 1, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 0, 0, 1, 1, 1, 1, 1, 1, 0, 0 }, 
        { 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0 }, 
        { 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 0 }, 
        { 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 0 },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 }, 
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
        { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },
    };

    for (int y = 0; y < 33; y++)
    {
        for (int x = 0; x < 30; x++) 
        {
            if (x == 0) 
            {
                grid[y][x].gridX = (float) -SIZE_TILE;
            }
            else 
            {
                grid[y][x].gridX = (float) x * SIZE_TILE - SIZE_TILE; 
            }
            grid[y][x].gridY = (float) y * SIZE_TILE;
            grid[y][x].food = gridFoodlInit[y][x];
            grid[y][x].isWall = gridWallInit[y][x];
        }
    }
}

// RENDER ROUTINES

void doRefreshScreen(SDL_Renderer* renderer)
{
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
    SDL_RenderClear(renderer);
}

void doDrawBackground(SDL_Renderer* renderer, const gameClass* game, SDL_Texture* mazeTexture)
{
    SDL_Rect maze = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT - SIZE_TILE * 2};

    if (game -> ballsLeft > 0)
    {
        SDL_RenderCopy(renderer, mazeTexture, NULL, &maze);
    }
    else
    {
        if (SDL_GetTicks() / 100 % 2)
        {
            SDL_RenderCopy(renderer, mazeTexture, NULL, &maze);
        }
        else
        {
            SDL_Rect noMaze = { 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT - SIZE_TILE * 2 }; 
            SDL_RenderDrawRect(renderer, &noMaze);
        }
    }
}

void doDrawFood(SDL_Renderer* renderer, SDL_Texture* foodTexture)
{
    for (int y = 0; y < 33; y++)
    {
        for (int x = 0; x < 30; x++)
        {   
            if (grid[y][x].food)
            {
                SDL_Rect foodTextureCrop = { 0, 0, 32, 32 };
                SDL_Rect foodTexturePosition = { (int)( grid[y][x].gridX - SIZE_TILE * 0.25f ), (int)( grid[y][x].gridY - SIZE_TILE * 0.25f ), 32, 32 };

                switch (grid[y][x].food)
                {
                    case smallBall: 
                        foodTextureCrop.x = 32; 
                    break;
                    
                    case largeBall: 
                        foodTextureCrop.x = 0; 
                    break;
                    
                    case noFood: 
                    break;
                }    

                SDL_RenderCopy(renderer, foodTexture, &foodTextureCrop, &foodTexturePosition);
            }
        }
    }
}

void doDrawPacman(SDL_Renderer* renderer, playerClass* player, SDL_Texture* pacmanTexture)
{
    SDL_Rect pacmanTexturePosition = { (int)( player -> posX - SIZE_TILE * 0.25f ), (int)( player -> posY - SIZE_TILE * 0.25f ), 32, 32 }; 

    switch (player -> curHeading)
    {
        case up: 
            player -> pacmanTextureCrop.y = 96; 
        break;
        
        case down: 
            player -> pacmanTextureCrop.y = 32; 
        break;
        
        case left: 
            player -> pacmanTextureCrop.y = 64; 
        break;
        
        case right: case idle: 
            player -> pacmanTextureCrop.y = 0; 
        break;
    }

    player -> timeFrame++;

    if (30 / player -> timeFrame == 5)
    {
        player -> pacmanTextureCrop.x += 32;
        player -> timeFrame = 0;

        if (player -> pacmanTextureCrop.x >= 128)
        {
            player -> pacmanTextureCrop.x = 0;
        }
    }

    SDL_RenderCopy(renderer, pacmanTexture, &player -> pacmanTextureCrop, &pacmanTexturePosition);
}

void doDrawGhosts(SDL_Renderer* renderer, const playerClass* player, enemyClass* enemy, SDL_Texture* ghostTexture)
{
    for (int i = 0; i < 4; i++)
    {
        SDL_Rect ghostTexturePosition = { (int)( enemy[i].posX - SIZE_TILE * 0.25f ), (int)( enemy[i].posY - SIZE_TILE * 0.25f ), 32, 32 };

        switch (enemy[i].state)
        {
            case home: case scatter: case chase:
                enemy[i].ghostTextureCrop.y = 32 * i;
            break;
        
            case eaten:
                enemy[i].ghostTextureCrop.y = 128;
            break;

            case frightened:
                enemy[i].ghostTextureCrop.y = 160;

                if (enemy[i].isTimeAlmostEnd && SDL_GetTicks() / 100 % 2)
                {
                    enemy[i].ghostTextureCrop.x = 64;
                }
                else 
                {
                    enemy[i].ghostTextureCrop.x = 0; 
                }
            break;
        }

        if (enemy[i].state != frightened)
        {
            switch (enemy[i].heading)
            {   
                case up: 
                    enemy[i].ghostTextureCrop.x = 192; 
                break;
                
                case down: 
                    enemy[i].ghostTextureCrop.x = 64; 
                break;
                
                case left: 
                    enemy[i].ghostTextureCrop.x = 128; 
                break;
                
                case right: case idle: 
                    enemy[i].ghostTextureCrop.x = 0; 
                break;
            }
        }

        if (enemy[i].state != eaten && player -> curHeading != idle) 
        {
            if (SDL_GetTicks() / 100 % 2)
            {
                enemy[i].ghostTextureCrop.x += 32;
            }
        }

        SDL_RenderCopy(renderer, ghostTexture, &enemy[i].ghostTextureCrop, &ghostTexturePosition);
    }
}

void doDrawPacmanKill(SDL_Renderer* renderer, gameClass* game, playerClass* player, SDL_Texture* pacmanTexture, SDL_Texture *killTexture)
{
    if ( (game -> timeDelay - SDL_GetTicks() / 1000) > 2)
    {
        SDL_Rect pacmanTexturePosition = { (int)( player -> posX - SIZE_TILE * 0.25f ), (int)( player -> posY - SIZE_TILE * 0.25f ), 32, 32 }; 
        SDL_RenderCopy(renderer, pacmanTexture, &player -> pacmanTextureCrop, &pacmanTexturePosition);
    } 
    else
    {
        SDL_Rect killTexturePosition = { (int)( player -> posX - SIZE_TILE * 0.25f ), (int)( player -> posY - SIZE_TILE * 0.25f ), 32, 32 };
        player -> timeFrame++;

        if (30 / player -> timeFrame == 5)
        {
            player -> killTextureCrop.x += 32;
            player -> timeFrame = 0;
        }   

        SDL_RenderCopy(renderer, killTexture, &player -> killTextureCrop, &killTexturePosition);
    }
}  

void doDrawLives(SDL_Renderer* renderer, gameClass* game, SDL_Texture* pacmanTexture)
{
    for (int i = 0, m = 0; i < game -> playerLives; i++, m += SIZE_TILE + SIZE_TILE / 4)
    {
        SDL_Rect livesTextureCrop = { 32, 0, 32, 32 }; 
        SDL_Rect livesTexturePosition = { (int)( grid[31][3].gridX - SIZE_TILE * 0.5f + m ), (int)( grid[32][3].gridY - SIZE_TILE * 0.5f ), SIZE_TILE, SIZE_TILE };
        SDL_RenderCopy(renderer, pacmanTexture, &livesTextureCrop, &livesTexturePosition);
    }
}

void doDrawScore(SDL_Renderer* renderer, gameClass* game, SDL_Texture* numbersTexture)
{ 
    unsigned int tmp = 0, offsetX, score;
    
    // draw current score   
    offsetX = 120;
    score = game -> currentScore;

    while (score)
    {
        tmp = score % 10;
        SDL_Rect numbersTextureCrop = { tmp * 12, 0, 12, 20 };
        SDL_Rect numbersTexturePosition = { (int)( grid[31][10].gridX + offsetX ), (int)( grid[31][10].gridY + SIZE_TILE * 0.5f ), 12, 20 };
        SDL_RenderCopy(renderer, numbersTexture, &numbersTextureCrop, &numbersTexturePosition);
        offsetX -= 12;
        score /= 10;
    }
    
    // draw highest score
    offsetX = 120;
    score = game -> highestScore;

    while (score)
    {
        tmp = score % 10;
        SDL_Rect numbersTextureCrop = { tmp * 12, 20, 12, 20 };
        SDL_Rect numbersTexturePosition = { (int)( grid[31][21].gridX + offsetX ), (int)( grid[31][21].gridY + SIZE_TILE * 0.5f ), 12, 20 };
        SDL_RenderCopy(renderer, numbersTexture, &numbersTextureCrop, &numbersTexturePosition);
        offsetX -= 12;
        score /= 10;
    }

}

void doDrawTextReady(SDL_Renderer* renderer, SDL_Texture* readyTexture)
{
    SDL_Rect ready = { (int)( grid[17][12].gridX + 10 ), (int)( grid[17][12].gridY - 3 ), 100, 25 };
    SDL_RenderCopy(renderer, readyTexture, NULL, &ready);
}

void doDrawTextGameOver(SDL_Renderer* renderer, SDL_Texture* gameOverTexture)
{
    SDL_Rect gameOver = { (int)( grid[15][11].gridX + 6 ), (int)( grid[15][11].gridY - 3 ), 150, 25 };
    SDL_RenderCopy(renderer, gameOverTexture, NULL, &gameOver);
}

// SDL2 INIT

void doInitEngine(SDL_Window** window, SDL_Renderer** renderer)
{
    if (SDL_Init(SDL_INIT_VIDEO)) 
    {
        fprintf(stderr, "Failed to initialize SDL2 library: %s\n", SDL_GetError());
        exit(1);
    }
    
    *window = SDL_CreateWindow(WINDOW_TITLE, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, SCREEN_WIDTH, SCREEN_HEIGHT, 0);
    if (!*window) 
    {
        fprintf(stderr, "Failed to create window: %s\n", SDL_GetError());
        exit(2);
    }
        
    *renderer = SDL_CreateRenderer  (*window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!*renderer) 
    {
        fprintf(stderr, "Failed to create renderer: %s\n", SDL_GetError());
        exit(3);
    }
}

// TRACKING THE HIGHEST SCORE

void doReadScore(gameClass* game)
{
    FILE *tmp = fopen("score", "r");

    if (tmp)
    {
        game -> highestScore = getw(tmp); 
    }

    fclose(tmp);
}

void doWriteScore(gameClass* game)
{
    FILE *tmp = fopen("score", "w");
    putw(game -> highestScore, tmp);
    fclose(tmp);
}

void doCheckScore(gameClass* game)
{
    if (game -> currentScore > game -> highestScore)
    {
        game -> highestScore = game -> currentScore;
    }    
}

// TEXTURES

void doLoadTextures(SDL_Renderer* renderer, SDL_Texture** textures)
{
    SDL_Surface *surfaces[8] = {
        IMG_Load("../img/maze.png"), // 0
        IMG_Load("../img/food.png"), // 1 
        IMG_Load("../img/ghost.png"), // 2
        IMG_Load("../img/pacman.png"), // 3
        IMG_Load("../img/kill.png"), // 4
        IMG_Load("../img/ready.png"), // 5
        IMG_Load("../img/gameover.png"), // 6
        IMG_Load("../img/numbers.png") // 7
    };

    for (int i = 0; i < 8; i++)
    {
        textures[i] = SDL_CreateTextureFromSurface(renderer, surfaces[i]);
        SDL_FreeSurface(surfaces[i]);
    }

}

void doCleanAll(SDL_Window* window, SDL_Renderer* renderer, SDL_Texture** textures)
{
    for (int i = 0; i < 8; i++)
    {
        SDL_DestroyTexture(textures[i]);
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
}

// GAME LOOP

void doGameLoop(SDL_Window* window, SDL_Renderer* renderer, SDL_Texture** textures)
{
    SDL_bool done = SDL_FALSE;
    SDL_Event event;

    gameClass game = { SDL_FALSE, 3, 245, 0, 0, 0 };   
    playerClass player;
    enemyClass enemy[4];

    doInitGrid();
    doInitPlayer(&player);
    doInitEnemy(enemy);

    doReadScore(&game);
    
    while (!done)
    {
        done = doPlayerMove(window, &event, &game, &player);
        
        doCheckScore(&game);
        doRefreshScreen(renderer);

        if (!game.gameOver)
        {
            doDrawBackground(renderer, &game, textures[0]);
        
            if (player.isAlive) 
            {   
                player.isMoving = SDL_TRUE;
                if (player.curHeading == idle)
                {
                    doDrawTextReady(renderer, textures[5]);
                }

                if (game.ballsLeft > 0)
                {
                    if (player.curHeading != idle)
                    {
                        doEatFood(&game, &player, enemy);
                        doUpdateEnemyState(&game, &player, enemy);
                        doEnemyMove(&game, &player, enemy);
                        doCheckEncounter(&game, &player, enemy);
                    }
                } 
                else 
                {
                    player.isMoving = SDL_FALSE;
                    if (doGamePause(&game, 3))
                    {
                        doInitGrid();
                        doInitPlayer(&player);
                        doInitEnemy(enemy);
                        game.ballsLeft = 245;
                    }
                }

                doDrawFood(renderer, textures[1]);
                doDrawPacman(renderer, &player, textures[3]);
                doDrawGhosts(renderer, &player, enemy, textures[2]);
                doDrawLives(renderer, &game, textures[3]);
                doDrawScore(renderer, &game, textures[7]);
            } 
            else
            {
                player.isMoving = SDL_FALSE;
                if (doGamePause(&game, 3))
                {
                    if (game.playerLives > 1)
                    {
                        game.playerLives--;
                    }
                    else
                    {
                        game.gameOver = SDL_TRUE;
                        doInitGrid();
                        game.playerLives = 3;
                        game.ballsLeft = 245;
                        game.currentScore = 0;
                    }
                
                    doInitPlayer(&player);
                    doInitEnemy(enemy);
                }
                
                // this condition is to avoid a frame leak of pacman which is visible after gameover becomes true
                if (!game.gameOver)
                {
                    doDrawPacmanKill(renderer, &game, &player, textures[3], textures[4]);
                }
            }
        } 
        else 
        {
            player.isMoving = SDL_FALSE;
            doDrawTextGameOver(renderer, textures[6]);
        }

        SDL_RenderPresent(renderer);
        SDL_Delay(FPS);
    }
    
    doWriteScore(&game);
}

// MAIN ROUTINES

int main(void)
{
    SDL_Window *window = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Texture *textures[8]; 

    doInitEngine(&window, &renderer);
    doLoadTextures(renderer, textures);
    srand(time(NULL));  
    doGameLoop(window, renderer, textures);
    doCleanAll(window, renderer, textures);

    return 0;
}