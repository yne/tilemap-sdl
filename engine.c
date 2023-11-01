// cc -lSDL2 this.c ; ./a.out media/sprite.bmp media/tileset.bmp media/tilemap.bin 256
#if tcc
#define SDL_MAIN_HANDLED
#define SDL_DISABLE_IMMINTRIN_H
#endif
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <string.h>
#define MAX(A, B) (A > B ? A : B)
#define MIN(A, B) (A < B ? A : B)
#define u8str(u) hex(u >> 4), hex(u)
#define u16str(u) u8str(u >> 8), u8str(u)

struct {
    SDL_Renderer* render;
    SDL_Window* window;
    uint32_t width, height;
    int32_t x, y; // cam position on the world map
    uint32_t shift; // cam latency (log2) when following player
    SDL_Texture* font;
} view;
struct {
    uint8_t* map[2];
    uint32_t cols, rows, total;
    uint8_t walkable, colision, swimming, paddling, sloshing, solid_se, solid_sw, solid_ne, solid_nw, slippery, teleport, bridge_h, bridge_v, stair_ru, stair_rd;
} tilemap;
struct {
    SDL_Texture* texture[2];
    int size;
} tileset;
struct {
    SDL_Texture* texture[2];
    uint32_t col, row, width, height;
    uint32_t x, y;
//    uint8_t* map_cache;
} layers;
struct {
    SDL_Texture* texture;
    uint32_t dir, step, speed;
    int32_t size, x, y, old_x, old_y; // in pixel
    _Bool debug, dead, god, on_bridge_from_side;
} player;

char hex(uint8_t u)
{
    u &= 0xf;
    return (u >= 0 && u <= 9) ? '0' + u : 'A' + u - 10;
}
uint8_t getTileAt(uint32_t x, uint32_t y) {
    int32_t tile_x = x / tileset.size; // (player.x + (player.size / 2))
    int32_t tile_y = y / tileset.size; // (player.y + player.size - 4)
    return tilemap.map[0][tile_x + (tile_y * tilemap.cols)];
}
void movePlayer(int32_t dx, int32_t dy)
{
    int32_t old_x = player.x;
    int32_t old_y = player.y;
    if (player.x + dx > tilemap.cols * tileset.size)
        dx = 0;
    if (player.y + dy > tilemap.rows * tileset.size)
        dy = 0;
    uint8_t tile_idx = getTileAt(player.x + dx, player.y + dy);
    if (player.god || tile_idx <= tilemap.walkable) {
        player.x += dx;
        player.y += dy;
    } else if (tile_idx <= tilemap.colision) {
    } else if (tile_idx <= tilemap.swimming) {
        player.x += dx / 2;
        player.y += dy / 2;
    } else if (tile_idx <= tilemap.paddling) {
        player.x += dx / 2;
        player.y += dy / 2;
    } else if (tile_idx <= tilemap.sloshing) {
        player.x += dx / 2;
        player.y += dy / 2;
        //SE SW NE NW
    } else if (tile_idx <= tilemap.solid_se) {
        int32_t tile_x = (player.x + dx) % tileset.size;
        int32_t tile_y = (player.y + dy) % tileset.size;
        if (tile_x < (tileset.size - tile_y)) { //not in the N-E corner: can move
            player.x += dx;
            player.y += dy;
        }
    } else if (tile_idx <= tilemap.solid_sw) {
        int32_t tile_x = (player.x + dx) % tileset.size;
        int32_t tile_y = (player.y + dy) % tileset.size;
        if (tile_x > tile_y) { // not in the N-W corner: can move
            player.x += dx;
            player.y += dy;
        }
    } else if (tile_idx <= tilemap.solid_ne) {
        int32_t tile_x = (player.x + dx) % tileset.size;
        int32_t tile_y = (player.y + dy) % tileset.size;
        if (tile_x < tile_y) { // not in the N-W corner: can move
            player.x += dx;
            player.y += dy;
        }
    } else if (tile_idx <= tilemap.solid_nw) {
        int32_t tile_x = (player.x + dx) % tileset.size;
        int32_t tile_y = (player.y + dy) % tileset.size;
        if (tile_x > (tileset.size - tile_y)) { //not in the N-E corner: can move
            player.x += dx;
            player.y += dy;
        }
    } else if (tile_idx <= tilemap.slippery) {
        int32_t h = (player.x - player.old_x)?:dx;
        player.x += h;
        if(!h)player.y += (player.y - player.old_y)?:dy;//only allow 1 slide direction
    } else if (tile_idx <= tilemap.teleport) {
        player.x=0xD0;
        player.y=0xA0;
    } else if (tile_idx <= tilemap.bridge_h) {
        uint8_t old_tile = getTileAt(player.x, player.y);
        _Bool was_on_bridge = tilemap.teleport < old_tile && old_tile <= tilemap.bridge_h;
        if (was_on_bridge) { // still on the bridge
            player.x += dx;
            player.y += dy;
        } else { // we entered the bridge
            //int on_bridge_from_side = old_tile_x == tile_x;
        }
    } else if (tile_idx <= tilemap.bridge_v) {
        uint8_t old_tile = getTileAt(player.x, player.y);
        _Bool was_on_bridge = tilemap.bridge_h < old_tile && old_tile <= tilemap.bridge_v;
        if (was_on_bridge) { // still on the bridge => can move
            player.x += dx;
            player.y += dy;
        } else{
            //int on_bridge_from_side = old_tile_x == tile_x;
        }
    } else if (tile_idx <= tilemap.stair_ru) {
        player.x += (dx + dy);
        player.y += (dx + dy);
    } else if (tile_idx <= tilemap.stair_rd) {
        player.x += (-dx + -dy);
        player.y += (-dx + -dy);
    }
    player.old_x = old_x;
    player.old_y = old_y;
}
// no SDL_LoadFile* on SDL<2.0.6
void* loadFile(char* path, size_t* size)
{
    FILE* m = fopen(path, "rb");
    if (!m)
        exit(fprintf(stderr, "%s: not found\n", path));
    struct stat sb;
    stat(path, &sb);
    void* content = malloc(sb.st_size);
    fread(content, sb.st_size, 1, m);
    if (size)
        *size = sb.st_size;
    fclose(m);
    return content;
}

char* getVal(char* haystack, size_t haystack_len, char* needle)
{
    char* val = memmem(haystack, haystack_len, needle, strlen(needle));
    return val ? val + strlen(needle) : NULL;
}

_Bool HandleEvent()
{
    _Bool moved = 0;
    uint32_t map_w = tilemap.cols * tileset.size;
    uint32_t map_h = tilemap.rows * tileset.size;
    for (SDL_Event event; SDL_PollEvent(&event);) { // event loop
        player.dead |= event.type == SDL_QUIT; // closed from OS
        if (event.type != SDL_KEYDOWN)
            break; // we only care about keydown
        SDL_Scancode sc = event.key.keysym.scancode; // shorthand
        player.debug ^= sc == SDL_SCANCODE_D; // enable debug view on "D" press
        player.dead |= sc == SDL_SCANCODE_ESCAPE; // quit on ESC
        player.dir = sc & 0x3; // SDL2 dependant, but simple enought
        player.speed = event.key.keysym.mod & KMOD_SHIFT ? 16 : 2;
        int32_t move_x = player.speed * (sc == SDL_SCANCODE_LEFT ? -1 : sc == SDL_SCANCODE_RIGHT);
        int32_t move_y = player.speed * (sc == SDL_SCANCODE_UP ? -1 : sc == SDL_SCANCODE_DOWN);
        movePlayer(move_x, move_y);
        moved |= (SDL_SCANCODE_RIGHT <= sc) && (sc <= SDL_SCANCODE_UP);
        if (sc == SDL_SCANCODE_G)
            player.god = !player.god;
        if (sc == SDL_SCANCODE_F)
            SDL_SetWindowFullscreen(view.window, (SDL_GetWindowFlags(view.window) & SDL_WINDOW_FULLSCREEN) ^ SDL_WINDOW_FULLSCREEN);
    }
    movePlayer(0,0);// continue sliding
    return moved;
}

// void init(char* player_path, char* tileset_path, char* tilemap_path, uint32_t tilemap_cols, char* font_path)
void init(char* ini)
{
    SDL_Init(SDL_INIT_EVERYTHING);
    size_t conf_len;
    char* conf = loadFile(ini, &conf_len);
    for (size_t i = 0; i < conf_len; i++) // replace \n with string terminator
        if (conf[i] == '\n')
            conf[i] = '\0';
    view.width = atoi(getVal(conf, conf_len, "WINDOWW=") ?: "320");
    view.height = atoi(getVal(conf, conf_len, "WINDOWH=") ?: "240");
    view.shift = 5;
    view.window = SDL_CreateWindow("tilemap demo", 0, 0, view.width, view.height, 0);
    view.render = SDL_CreateRenderer(view.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    view.font = SDL_CreateTextureFromSurface(view.render, SDL_LoadBMP(getVal(conf, conf_len, "DBGFONT=") ?: "font.bmp")); // convert -depth 8 -size 16x544 rgba:zelda.rgba rgba.png

    player.texture = SDL_CreateTextureFromSurface(view.render, SDL_LoadBMP(getVal(conf, conf_len, "SPRITES=") ?: "sprites.bmp"));
    SDL_QueryTexture(player.texture, NULL, NULL, &player.size, NULL);
    tileset.texture[0] = SDL_CreateTextureFromSurface(view.render, SDL_LoadBMP(getVal(conf, conf_len, "TILESET=")?:getVal(conf, conf_len, "TILESET0="))); // convert -depth 8 -size 16x544 rgba:zelda.rgba rgba.png
    SDL_QueryTexture(tileset.texture[0], NULL, NULL, &tileset.size, NULL); // use BMP width as sprite size reference
    layers.width = view.width; // TODO:scale layer to view width + 1 sprite for wrapping
    layers.height = view.height;
    layers.texture[0] = SDL_CreateTexture(view.render, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, layers.width, layers.height);
    // layers.map_cache = malloc((layers.width / tileset.size) * (layers.height / tileset.size) * sizeof(*tilemap.map));
    size_t tilemap_size;
    tilemap.map[0] = loadFile(getVal(conf, conf_len, "TILEMAP=")?:getVal(conf, conf_len, "TILEMAP0="), &tilemap_size);
    tilemap.cols = atoi(getVal(conf, conf_len, "MAPCOLS="));
    tilemap.rows = tilemap_size / tilemap.cols;
    tilemap.total = tilemap.rows * tilemap.cols;
    char* overlay = getVal(conf, conf_len, "TILEMAP1=");
    if (overlay) {
        tilemap.map[1] = loadFile(overlay, &tilemap_size);
        layers.texture[1] = SDL_CreateTexture(view.render, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, layers.width, layers.height);
        tileset.texture[1] = SDL_CreateTextureFromSurface(view.render, SDL_LoadBMP(getVal(conf, conf_len, "TILESET1=")));
        SDL_SetTextureBlendMode(tileset.texture[1], SDL_BLENDMODE_NONE); // copying tiles shall replace targeted ones
        SDL_SetTextureBlendMode(layers.texture[1], SDL_BLENDMODE_BLEND); // copying this layer shall blend the alpha
    }
    tilemap.walkable = atoi(getVal(conf, conf_len, "WALKABLE=")?:"99");
    tilemap.colision = atoi(getVal(conf, conf_len, "COLISION=")?:"0");
    tilemap.swimming = atoi(getVal(conf, conf_len, "SWIMMING=")?:"0");
    tilemap.paddling = atoi(getVal(conf, conf_len, "PADDLING=")?:"0");
    tilemap.sloshing = atoi(getVal(conf, conf_len, "SLOSHING=")?:"0");
    tilemap.solid_se = atoi(getVal(conf, conf_len, "SOLID_SE=")?:"0");
    tilemap.solid_sw = atoi(getVal(conf, conf_len, "SOLID_SW=")?:"0");
    tilemap.solid_ne = atoi(getVal(conf, conf_len, "SOLID_NE=")?:"0");
    tilemap.solid_nw = atoi(getVal(conf, conf_len, "SOLID_NW=")?:"0");
    tilemap.teleport = atoi(getVal(conf, conf_len, "TELEPORT=")?:"0");
    tilemap.slippery = atoi(getVal(conf, conf_len, "SLIPPERY=")?:"0");
    tilemap.bridge_h = atoi(getVal(conf, conf_len, "BRIDGE_H=")?:"0");
    tilemap.bridge_v = atoi(getVal(conf, conf_len, "BRIDGE_V=")?:"0");
    tilemap.stair_ru = atoi(getVal(conf, conf_len, "STAIR_RU=")?:"0");
    tilemap.stair_rd = atoi(getVal(conf, conf_len, "STAIR_RD=")?:"0");
    player.x = atoi(getVal(conf, conf_len, "SPAWN_X=")?:"0");
    player.y = atoi(getVal(conf, conf_len, "SPAWN_Y=")?:"0");
}

void fini()
{
    for(int i = 0; i < sizeof(layers.texture) / sizeof(*layers.texture); i++)
        SDL_DestroyTexture(layers.texture[i]);
    SDL_DestroyTexture(layers.texture[1]);
    SDL_DestroyTexture(player.texture);
    SDL_DestroyRenderer(view.render);
    SDL_DestroyWindow(view.window);
    SDL_Quit();
}

void player_draw()
{
    SDL_RenderCopy(view.render, player.texture,
        &(SDL_Rect) { 0, (player.dir * 2 + player.step) * player.size, player.size, player.size },
        &(SDL_Rect) { player.x - view.x - (player.size/2), player.y - view.y - player.size, player.size, player.size });
}

void layer_render(uint8_t level)
{
    //if(!layers.texture[level])return;
    SDL_Point v = {
        // viewport-relativ point (@)
        view.width - (view.x % view.width),
        view.height - (view.y % view.height),
    };
    SDL_Rect coord[] = {
        // surface C: anchor to top-left corner [0,0, v.x,v.y]
        { (view.x) % view.width, (view.y % view.height), v.x, v.y },
        { 0, 0, v.x, v.y },
        // surface D: anchor to top-right corner [v.x,0, view.width-v.x,v.y]
        { (view.x + v.x) % view.width, view.y % view.height, view.width - v.x, v.y },
        { v.x, 0, view.width - v.x, v.y },
        // surface B: anchor to bottom-left [0,v.y, v.x,view.height-v.y]
        { (view.x) % view.width, (view.y + v.y) % view.height, v.x, view.height - v.y },
        { 0, v.y, v.x, view.height - v.y },
        // surface A: anchor to bottom-right [v.x,v.y ,view.width,view.height]
        { (view.x + v.x) % view.width, (view.y + v.y) % view.height, view.width - v.x, view.height - v.y },
        { v.x, v.y, view.width - v.x, view.height - v.y },
    };
    for (uint32_t i = 0; i < sizeof(coord) / sizeof(*coord); i += 2) {
        SDL_RenderCopy(view.render, layers.texture[level], &coord[i], &coord[i + 1]);
    }
}
uint32_t printXY(uint32_t x, uint32_t y, char* str)
{
    for (; *str; str++, x += 5) {
        char c = (*str > '_' ? *str - 32 : *str) % 64; // wrap ascii table to 64chars
        SDL_RenderCopy(view.render, view.font,
            &(SDL_Rect) { 0, c * 8, 6, 8 },
            &(SDL_Rect) { x, y, 6, 8 });
    }
    return x;
}

// TODO use layers.map_cache to avoid useless RenderCopy
uint32_t last_view_x = ~0, last_view_y = ~0;
int last;
void map_to_layers(uint32_t view_x, uint32_t view_y)
{
    // no update if we are on the same tile
    if (view_x / tileset.size == last_view_x / tileset.size
        && view_y / tileset.size == last_view_y / tileset.size)
        return;
    // no such layer to draw
    for (uint8_t level = 0; level < sizeof(layers.texture) / sizeof(*layers.texture); level++) {
        if(!tilemap.map[level] || !layers.texture[level] || !tileset.texture[level])
            return;
        SDL_SetRenderTarget(view.render, layers.texture[level]);
        for (uint32_t map_row = view_y / tileset.size, map_end = (view_y + view.height) / tileset.size; map_row < map_end; map_row++) {
            for (uint32_t map_col = view_x / tileset.size, map_end = (view_x + view.width) / tileset.size; map_col < map_end; map_col++) {
                uint32_t tile = map_col + (map_row * tilemap.cols);
                uint32_t layer_col = map_col % (layers.width / tileset.size);
                uint32_t layer_row = map_row % (layers.height / tileset.size);
                SDL_RenderCopy(view.render, tileset.texture[level],
                    &(SDL_Rect) { 0, tilemap.map[level][tile % tilemap.total] * tileset.size, tileset.size, tileset.size },
                    &(SDL_Rect) { layer_col * tileset.size, layer_row * tileset.size, tileset.size, tileset.size });
                if (player.debug)
                    printXY(layer_col * 16, layer_row * 16, (char[]) { u8str(tilemap.map[level][tile % tilemap.total]), 0 });
            }
        }
    }
    last = view_x / tileset.size + view_y / tileset.size;
    last_view_x = view_x;
    last_view_y = view_y;
}
void camera_follow()
{
    int delta_x = player.x - (view.x + view.width / 2);
    int delta_y = player.y - (view.y + view.height / 2);
    view.x += delta_x >> view.shift; // update view.x,y now to avoid bike-shading effect when moving
    view.y += delta_y >> view.shift; // TODO: look toward player direction
    view.x = MIN(MAX(0, view.x), tilemap.cols * tileset.size - view.width);
    view.y = MIN(MAX(0, view.y), tilemap.rows * tileset.size - view.height);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        return printf("USAGE:\n\t%s conf.ini\n", argv[0]);
    }
    init(argv[1]);
    while (!player.dead) { // mainloop (shall SDL_Delay(1000/60) ? )
        _Bool moved = HandleEvent(); // update player.x,y
        camera_follow();

        map_to_layers(view.x, view.y);

        SDL_SetRenderTarget(view.render, NULL);
        layer_render(0);
        player_draw();
        layer_render(1);


        printXY(6, 8, (char[]) { 'C', 'A', 'M', ':', u16str(view.x), ' ', u16str(view.y), 0 });
        printXY(6, 16, (char[]) { 'U', 'S', 'R', ':', u16str(player.x), ' ', u16str(player.y), 0 });
        printXY(6, 24, (char[]) { 'G', 'O', 'D', ':', player.god ? 'Y' : 'N', 0 });
        SDL_RenderCopy(view.render, tileset.texture[0],
            &(SDL_Rect) { 0, getTileAt(player.x,player.y) * tileset.size, tileset.size, tileset.size },
            &(SDL_Rect) { 6, 40, tileset.size, tileset.size });
        SDL_SetRenderDrawColor(view.render, 0xFF, 0x00, 0x00, 0xFF);
        SDL_RenderDrawPoint(view.render, 6 + player.x%tileset.size, 40 + player.y%tileset.size);
        SDL_RenderPresent(view.render);
    }
    fini();
}
/*
TILES flags:
- hoverlay
- slash stairs: y++ when x++; z++ when enter from Down or Left
- backslash stairs: y-- when x++; z++ when enter from Down or Left
- crossover:flag if from L or R, can walk any crossover, can ony exit from L/R if flag
- snow: walk using velocity
- water: walk using velocity
*/