// cc -lSDL2 this.c ; ./a.out media/sprite.bmp media/tileset.bmp media/tilemap.bin 256
#if tcc
#define SDL_MAIN_HANDLED
#define SDL_DISABLE_IMMINTRIN_H
#endif
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#define MAX(A, B) (A > B ? A : B)
#define MIN(A, B) (A < B ? A : B)
#define u8str(u) hex(u >> 4), hex(u)
#define u16str(u) u8str(u >> 8), u8str(u)
char hex(uint8_t u)
{
    u &= 0xf;
    return (u >= 0 && u <= 9) ? '0' + u : 'A' + u - 10;
}

struct {
    SDL_Renderer* render;
    SDL_Window* window;
    uint32_t width, height;
    int32_t x, y; // cam position on the world map
    uint32_t shift; // cam latency (log2) when following player
    SDL_Texture* font;
} view;
struct {
    uint8_t* map;
    uint32_t cols, rows, total, walkable;
} tilemap;
struct {
    SDL_Texture* texture;
    int size;
} tileset;
struct {
    SDL_Texture* texture;
    uint32_t col, row, width, height;
    uint32_t x, y;
    uint8_t* map_cache;
} layers[1];
struct {
    SDL_Texture* texture;
    uint32_t size, dir, step, speed;
    int32_t x, y; // player position on the worldmap
    _Bool debug, dead, god;
} player;

uint8_t mapAt(int32_t dx, int32_t dy)
{
    int32_t x = (dx + (player.x + (player.size / 2))) / tileset.size;
    int32_t y = (dy + (player.y + player.size - 4)) / tileset.size;
    if (x < 0 || x >= tilemap.cols || y < 0 || y >= tilemap.rows)
        return 0;
    return tilemap.map[x + (tilemap.cols * y)];
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
        player.speed = event.key.keysym.mod & KMOD_SHIFT ? 16 : 1;
        int32_t move_x = player.speed * (sc == SDL_SCANCODE_LEFT ? -1 : sc == SDL_SCANCODE_RIGHT);
        int32_t move_y = player.speed * (sc == SDL_SCANCODE_UP ? -1 : sc == SDL_SCANCODE_DOWN);
        player.x += (player.god || mapAt(move_x, 0) < tilemap.walkable) ? move_x : 0;
        player.y += (player.god || mapAt(0, move_y) < tilemap.walkable) ? move_y : 0;
        moved |= (SDL_SCANCODE_RIGHT <= sc) && (sc <= SDL_SCANCODE_UP);
        if (sc == SDL_SCANCODE_G)
            player.god = !player.god;
        if (sc == SDL_SCANCODE_F)
            SDL_SetWindowFullscreen(view.window, (SDL_GetWindowFlags(view.window) & SDL_WINDOW_FULLSCREEN) ^ SDL_WINDOW_FULLSCREEN);
    }
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
    view.window = SDL_CreateWindow("tilemap demo", 2000, 0, view.width, view.height, 0);
    view.render = SDL_CreateRenderer(view.window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    view.font = SDL_CreateTextureFromSurface(view.render, SDL_LoadBMP(getVal(conf, conf_len, "DBGFONT=") ?: "font.bmp")); // convert -depth 8 -size 16x544 rgba:zelda.rgba rgba.png

    player.texture = SDL_CreateTextureFromSurface(view.render, SDL_LoadBMP(getVal(conf, conf_len, "SPRITES=") ?: "sprites.bmp"));
    SDL_QueryTexture(player.texture, NULL, NULL, &player.size, NULL);
    tileset.texture = SDL_CreateTextureFromSurface(view.render, SDL_LoadBMP(getVal(conf, conf_len, "TILESET="))); // convert -depth 8 -size 16x544 rgba:zelda.rgba rgba.png
    SDL_QueryTexture(tileset.texture, NULL, NULL, &tileset.size, NULL); // use BMP width as sprite size reference
    layers[0].width = view.width; // TODO:scale layer to view width + 1 sprite for wrapping
    layers[0].height = view.height;
    layers[0].texture = SDL_CreateTexture(view.render, SDL_PIXELFORMAT_RGBA8888, SDL_TEXTUREACCESS_TARGET, layers[0].width, layers[0].height);
    // layers[0].map_cache = malloc((layers[0].width / tileset.size) * (layers[0].height / tileset.size) * sizeof(*tilemap.map));
    size_t tilemap_size;
    tilemap.map = loadFile(getVal(conf, conf_len, "TILEMAP="), &tilemap_size);
    tilemap.cols = atoi(getVal(conf, conf_len, "MAPCOLS="));
    tilemap.rows = tilemap_size / tilemap.cols;
    tilemap.total = tilemap.rows * tilemap.cols;
    tilemap.walkable = atoi(getVal(conf, conf_len, "WALKIDX="));
    player.x = atoi(getVal(conf, conf_len, "SPAWN_X="));
    player.y = atoi(getVal(conf, conf_len, "SPAWN_Y="));
}

void fini()
{
    SDL_DestroyTexture(layers[0].texture);
    SDL_DestroyTexture(player.texture);
    SDL_DestroyRenderer(view.render);
    SDL_DestroyWindow(view.window);
    SDL_Quit();
}

void player_draw()
{
    &(SDL_Rect) { player.x /*- view.x*/ + 2, player.y /*- view.y*/ + player.size - 2, player.size - 4, 2 };
    SDL_RenderFillRect(view.render,
        &(SDL_Rect) { player.x - view.x + 2, player.y - view.y + player.size - 2, player.size - 4, 2 });
    SDL_RenderCopy(view.render, player.texture,
        &(SDL_Rect) { 0, (player.dir * 2 + player.step) * player.size, player.size, player.size },
        &(SDL_Rect) { player.x - view.x, player.y - view.y, player.size, player.size });
}

void layer_render()
{
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
        SDL_RenderCopy(view.render, layers[0].texture, &coord[i], &coord[i + 1]);
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

// TODO use layers[0].map_cache to avoid useless RenderCopy
uint32_t last_view_x = ~0, last_view_y = ~0;
int last;
void map_to_layer(uint32_t view_x, uint32_t view_y)
{
    // no update if we are on the same tile
    if (view_x / tileset.size == last_view_x / tileset.size
        && view_y / tileset.size == last_view_y / tileset.size)
        return;
    SDL_SetRenderTarget(view.render, layers[0].texture);

    for (uint32_t map_row = view_y / tileset.size, map_end = (view_y + view.height) / tileset.size; map_row < map_end; map_row++) {
        for (uint32_t map_col = view_x / tileset.size, map_end = (view_x + view.width) / tileset.size; map_col < map_end; map_col++) {
            uint32_t tile = map_col + (map_row * tilemap.cols);
            uint32_t layer_col = map_col % (layers[0].width / tileset.size);
            uint32_t layer_row = map_row % (layers[0].height / tileset.size);
            SDL_RenderCopy(view.render, tileset.texture,
                &(SDL_Rect) { 0, tilemap.map[tile % tilemap.total] * tileset.size, tileset.size, tileset.size },
                &(SDL_Rect) { layer_col * tileset.size, layer_row * tileset.size, tileset.size, tileset.size });
            if (player.debug)
                printXY(layer_col * 16, layer_row * 16, (char[]) { u8str(tilemap.map[tile % tilemap.total]), 0 });
        }
    }
    last = view_x / tileset.size + view_y / tileset.size;
    SDL_SetRenderTarget(view.render, NULL);
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
        map_to_layer(view.x, view.y);
        layer_render();
        player_draw();
        printXY(6, 8, (char[]) { 'C', 'A', 'M', ':', u16str(view.x), ' ', u16str(view.y), 0 });
        printXY(6, 24, (char[]) { 'U', 'S', 'R', ':', u16str(player.x), ' ', u16str(player.y), 0 });
        printXY(6, 32, (char[]) { 'G', 'O', 'D', ':', player.god ? 'Y' : 'N', 0 });
        SDL_RenderPresent(view.render);
    }
    fini();
}