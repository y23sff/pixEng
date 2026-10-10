#ifndef PIXEL_WIN_H
#define PIXEL_WIN_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    int x, y, z;
} CharacterPosition;

typedef struct {
    char **rows;
    int width;
    int height;
    int rowCount;
    int capacity;
} PixelLayer;

typedef struct {
    char *name;
    PixelLayer *layers;
    int depth;
    int layerCapacity;
} PixelCharacter;

typedef struct SceneInstance {
    int id;
    PixelCharacter *character;
    CharacterPosition pos;
    int hovered;
    int alive;
} SceneInstance;

typedef struct {
    float x, y;
    float zoom;
    int followId;
} PixelCamera;

typedef enum {
    eventCollide = 1,
    eventMouseClick,
    eventMouseEnter,
    eventMouseLeave,
    eventUpdate,
    eventDraw,
    eventCustom
} PixelEventType;

typedef struct {
    PixelEventType type;
    SceneInstance *self;
    SceneInstance *other;
    int customId;
    void *data;
} PixelEvent;

typedef void (*PixelEventHandler)(PixelEvent *event);

void pixelWinInit(const char *title, int width, int height, int pixelSize);
int  pixelWinIsOpen(void);
void pixelWinUpdate(void);
void pixelWinClose(void);
void pixelWinMouseCell(float *outX, float *outY);

void pixelWinSet(char c, unsigned char r, unsigned char g, unsigned char b);
void pixelWinSetDefault(unsigned char r, unsigned char g, unsigned char b);
void pixelWinSetBackground(unsigned char r, unsigned char g, unsigned char b);

void pixelWinDraw(const char *pixels);
void pixelWinClear(void);

PixelCharacter *characterCreate(const char *name);
void characterDestroy(PixelCharacter *ch);
void characterClear(PixelCharacter *ch);
void characterAddRow(PixelCharacter *ch, const char *row);
void characterAddLayer(PixelCharacter *ch);
void characterSetPixels(PixelCharacter *ch, const char *pixels);
char characterCell(const PixelCharacter *ch, int x, int y, int z);
PixelCharacter *characterFind(const char *name);
int  characterCount(void);
PixelCharacter *characterGet(int index);

int loadMcf(const char *path);

int  scene3dCharacterAdd(const char *characterName, int x, int y, int z);
int  scene3dCharacterAddAt(const char *characterName, CharacterPosition pos);
void scene3dCharacterRemove(int instanceId);
void scene3dCharacterMove(int instanceId, int dx, int dy, int dz);
void scene3dCharacterSetPosition(int instanceId, int x, int y, int z);
void scene3dClear(void);
void scene3dSetZShift(int shiftX, int shiftY);
SceneInstance *scene3dGetInstance(int instanceId);
int  scene3dInstanceCount(void);

void cameraSetPosition(float x, float y);
void cameraGetPosition(float *outX, float *outY);
void cameraMove(float dx, float dy);
void cameraSetZoom(float zoom);
float cameraGetZoom(void);
void cameraFollow(int instanceId);
void cameraReset(void);
PixelCamera *cameraGet(void);
void cameraScreenToWorld(float sx, float sy, float *outX, float *outY);
void cameraWorldToScreen(float wx, float wy, float *outX, float *outY);

int instancesCollide(SceneInstance *a, SceneInstance *b);
int collideCharacters(const char *nameA, const char *nameB);

void addEventToCharacterCollide(const char *characterName, PixelEventHandler handler);
void addEventToCharacterMouseClick(const char *characterName, PixelEventHandler handler);
void addEventToCharacterMouseEnter(const char *characterName, PixelEventHandler handler);
void addEventToCharacterMouseLeave(const char *characterName, PixelEventHandler handler);
void addEventUpdate(PixelEventHandler handler);
void addEventDraw(PixelEventHandler handler);
void emitEvent(int customId, void *data);
int  pollEvent(PixelEvent *outEvent);

#ifdef __cplusplus
}
#endif

#endif

#if defined(PIXEL_WIN_IMPLEMENTATION) && !defined(PIXEL_WIN_IMPL_DONE)
#define PIXEL_WIN_IMPL_DONE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "raylib.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct { unsigned char r, g, b; unsigned char isSet; } PixelColor;

static int gInitialized = 0;
static int gPixelSize = 1;
static int gWindowWidth = 0, gWindowHeight = 0;
static int gZShiftX = 1, gZShiftY = 1;
static PixelColor gPalette[256];
static unsigned char gDefaultColor[3] = {0, 0, 0};
static unsigned char gBgColor[3] = {0, 0, 0};
static PixelCamera gCamera = { 0.0f, 0.0f, 1.0f, -1 };

static PixelCharacter **gCharacters = NULL;
static int gCharacterCount = 0, gCharacterCapacity = 0;

static SceneInstance *gInstances = NULL;
static int gInstanceCount = 0, gInstanceCapacity = 0;
static int gNextInstanceId = 1;

static char *gDecor = NULL;

static int gHoveredInstanceId = -1;

static void *pwAlloc(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p) { fprintf(stderr, "pixelWin: out of memory\n"); abort(); }
    return p;
}
static void *pwRealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { fprintf(stderr, "pixelWin: out of memory\n"); abort(); }
    return q;
}
static char *pwStrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *d = (char *)pwAlloc(n);
    memcpy(d, s, n);
    return d;
}

static char layerCell(const PixelLayer *l, int x, int y) {
    if (!l || x < 0 || y < 0 || x >= l->width || y >= l->height) return ' ';
    const char *row = l->rows[y];
    if (!row || x >= (int)strlen(row)) return ' ';
    return row[x];
}
static int cellSolid(char c) { return c != ' ' && c != '\0'; }

static Color cellColor(unsigned char c) {
    const PixelColor *pc = &gPalette[c];
    if (pc->isSet)
        return (Color){ pc->r, pc->g, pc->b, 255 };
    return (Color){ gDefaultColor[0], gDefaultColor[1], gDefaultColor[2], 255 };
}

static float worldScale(void) {
    return (float)gPixelSize * gCamera.zoom;
}
static int cellScreenSize(void) {
    int s = (int)(worldScale() + 0.5f);
    return s < 1 ? 1 : s;
}
static int cellScreenX(const SceneInstance *in, int lz, int lx) {
    return (int)(((float)(in->pos.x + lz * gZShiftX + lx) - gCamera.x) * worldScale());
}
static int cellScreenY(const SceneInstance *in, int lz, int ly) {
    return (int)(((float)(in->pos.y - lz * gZShiftY + ly) - gCamera.y) * worldScale());
}

void pixelWinSet(char c, unsigned char r, unsigned char g, unsigned char b) {
    PixelColor *pc = &gPalette[(unsigned char)c];
    pc->r = r; pc->g = g; pc->b = b; pc->isSet = 1;
}
void pixelWinSetDefault(unsigned char r, unsigned char g, unsigned char b) {
    gDefaultColor[0] = r; gDefaultColor[1] = g; gDefaultColor[2] = b;
}
void pixelWinSetBackground(unsigned char r, unsigned char g, unsigned char b) {
    gBgColor[0] = r; gBgColor[1] = g; gBgColor[2] = b;
}

PixelCharacter *characterCreate(const char *name) {
    PixelCharacter *ch = (PixelCharacter *)calloc(1, sizeof(PixelCharacter));
    if (!ch) { fprintf(stderr, "pixelWin: out of memory\n"); abort(); }
    ch->name = pwStrdup(name ? name : "");
    if (gCharacterCount >= gCharacterCapacity) {
        gCharacterCapacity = gCharacterCapacity ? gCharacterCapacity * 2 : 16;
        gCharacters = (PixelCharacter **)pwRealloc(gCharacters, (size_t)gCharacterCapacity * sizeof(PixelCharacter *));
    }
    gCharacters[gCharacterCount++] = ch;
    return ch;
}

void characterDestroy(PixelCharacter *ch) {
    if (!ch) return;
    for (int i = 0; i < gCharacterCount; i++) {
        if (gCharacters[i] == ch) {
            memmove(&gCharacters[i], &gCharacters[i + 1],
                    (size_t)(gCharacterCount - i - 1) * sizeof(PixelCharacter *));
            gCharacterCount--;
            break;
        }
    }
    free(ch->name);
    characterClear(ch);
    free(ch->layers);
    free(ch);
}

void characterClear(PixelCharacter *ch) {
    if (!ch) return;
    for (int i = 0; i < ch->depth; i++) {
        PixelLayer *l = &ch->layers[i];
        for (int r = 0; r < l->rowCount; r++) free(l->rows[r]);
        free(l->rows);
        memset(l, 0, sizeof *l);
    }
    ch->depth = 0;
}

void characterAddLayer(PixelCharacter *ch) {
    if (!ch) return;
    if (ch->depth >= ch->layerCapacity) {
        ch->layerCapacity = ch->layerCapacity ? ch->layerCapacity * 2 : 4;
        ch->layers = (PixelLayer *)pwRealloc(ch->layers, (size_t)ch->layerCapacity * sizeof(PixelLayer));
    }
    PixelLayer *l = &ch->layers[ch->depth++];
    memset(l, 0, sizeof *l);
}

void characterAddRow(PixelCharacter *ch, const char *row) {
    if (!ch || !row) return;
    if (ch->depth == 0) characterAddLayer(ch);
    PixelLayer *l = &ch->layers[ch->depth - 1];
    if (l->rowCount >= l->capacity) {
        l->capacity = l->capacity ? l->capacity * 2 : 8;
        l->rows = (char **)pwRealloc(l->rows, (size_t)l->capacity * sizeof(char *));
    }
    l->rows[l->rowCount++] = pwStrdup(row);
    int len = (int)strlen(row);
    if (len > l->width) l->width = len;
    l->height = l->rowCount;
}

void characterSetPixels(PixelCharacter *ch, const char *pixels) {
    if (!ch || !pixels) return;
    characterClear(ch);
    if (*pixels == '\0') return;
    const char *start = pixels;
    const char *p = pixels;
    for (;;) {
        if (*p == '\n' || *p == '\0') {
            size_t len = (size_t)(p - start);
            char *line = (char *)pwAlloc(len + 1);
            memcpy(line, start, len);
            line[len] = '\0';
            if (strcmp(line, "//**//") == 0)
                characterAddLayer(ch);
            else
                characterAddRow(ch, line);
            free(line);
            if (*p == '\0') break;
            p++;
            start = p;
            if (*start == '\0') break;
            continue;
        }
        p++;
    }
}

char characterCell(const PixelCharacter *ch, int x, int y, int z) {
    if (!ch || z < 0 || z >= ch->depth) return ' ';
    return layerCell(&ch->layers[z], x, y);
}

PixelCharacter *characterFind(const char *name) {
    if (!name) return NULL;
    for (int i = 0; i < gCharacterCount; i++)
        if (gCharacters[i]->name && strcmp(gCharacters[i]->name, name) == 0)
            return gCharacters[i];
    return NULL;
}
int characterCount(void) { return gCharacterCount; }
PixelCharacter *characterGet(int index) {
    if (index < 0 || index >= gCharacterCount) return NULL;
    return gCharacters[index];
}

int loadMcf(const char *path) {
    if (!path) return 0;
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[2048];
    PixelCharacter *current = NULL;
    while (fgets(line, sizeof line, f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;
        if (line[0] == '#' && (line[1] == ' ' || line[1] == '\t' || line[1] == '\0'))
            continue;
        if (len >= 4 && strncmp(line, "--", 2) == 0 && strcmp(line + len - 2, "--") == 0) {
            char name[128];
            size_t nameLen = len - 4;
            if (nameLen >= sizeof name) nameLen = sizeof name - 1;
            memcpy(name, line + 2, nameLen);
            name[nameLen] = '\0';
            char *s = name;
            while (*s == ' ' || *s == '\t') s++;
            char *e = s + strlen(s);
            while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
            PixelCharacter *old = characterFind(s);
            if (old) characterDestroy(old);
            current = characterCreate(s);
            continue;
        }
        if (!current) continue;
        if (strcmp(line, "//**//") == 0)
            characterAddLayer(current);
        else
            characterAddRow(current, line);
    }
    fclose(f);
    return 1;
}

static void sceneEnsureCapacity(void) {
    if (gInstanceCount < gInstanceCapacity) return;
    gInstanceCapacity = gInstanceCapacity ? gInstanceCapacity * 2 : 32;
    gInstances = (SceneInstance *)pwRealloc(gInstances, (size_t)gInstanceCapacity * sizeof(SceneInstance));
}

int scene3dCharacterAddAt(const char *characterName, CharacterPosition pos) {
    PixelCharacter *ch = characterFind(characterName);
    if (!ch) return -1;
    sceneEnsureCapacity();
    SceneInstance *in = &gInstances[gInstanceCount];
    in->id = gNextInstanceId++;
    in->character = ch;
    in->pos = pos;
    in->hovered = 0;
    in->alive = 1;
    gInstanceCount++;
    return in->id;
}

int scene3dCharacterAdd(const char *characterName, int x, int y, int z) {
    CharacterPosition pos = { x, y, z };
    return scene3dCharacterAddAt(characterName, pos);
}

SceneInstance *scene3dGetInstance(int instanceId) {
    for (int i = 0; i < gInstanceCount; i++)
        if (gInstances[i].id == instanceId)
            return &gInstances[i];
    return NULL;
}

void scene3dCharacterRemove(int instanceId) {
    SceneInstance *in = scene3dGetInstance(instanceId);
    if (!in) return;
    in->alive = 0;
    if (gHoveredInstanceId == instanceId) gHoveredInstanceId = -1;
}

void scene3dCharacterMove(int instanceId, int dx, int dy, int dz) {
    SceneInstance *in = scene3dGetInstance(instanceId);
    if (!in) return;
    in->pos.x += dx; in->pos.y += dy; in->pos.z += dz;
}

void scene3dCharacterSetPosition(int instanceId, int x, int y, int z) {
    SceneInstance *in = scene3dGetInstance(instanceId);
    if (!in) return;
    in->pos.x = x; in->pos.y = y; in->pos.z = z;
}

void scene3dClear(void) {
    gInstanceCount = 0;
    gHoveredInstanceId = -1;
}

void scene3dSetZShift(int shiftX, int shiftY) {
    gZShiftX = shiftX;
    gZShiftY = shiftY;
}

int scene3dInstanceCount(void) {
    int n = 0;
    for (int i = 0; i < gInstanceCount; i++)
        if (gInstances[i].alive) n++;
    return n;
}

void cameraSetPosition(float x, float y) {
    gCamera.x = x;
    gCamera.y = y;
}
void cameraGetPosition(float *outX, float *outY) {
    if (outX) *outX = gCamera.x;
    if (outY) *outY = gCamera.y;
}
void cameraMove(float dx, float dy) {
    gCamera.x += dx;
    gCamera.y += dy;
}
void cameraSetZoom(float zoom) {
    if (zoom < 0.1f) zoom = 0.1f;
    if (zoom > 10.0f) zoom = 10.0f;
    gCamera.zoom = zoom;
}
float cameraGetZoom(void) { return gCamera.zoom; }
void cameraFollow(int instanceId) { gCamera.followId = instanceId; }
void cameraReset(void) {
    gCamera.x = 0.0f;
    gCamera.y = 0.0f;
    gCamera.zoom = 1.0f;
    gCamera.followId = -1;
}
PixelCamera *cameraGet(void) { return &gCamera; }

void cameraScreenToWorld(float sx, float sy, float *outX, float *outY) {
    float scale = worldScale();
    if (outX) *outX = scale > 0.0f ? sx / scale + gCamera.x : 0.0f;
    if (outY) *outY = scale > 0.0f ? sy / scale + gCamera.y : 0.0f;
}
void cameraWorldToScreen(float wx, float wy, float *outX, float *outY) {
    float scale = worldScale();
    if (outX) *outX = (wx - gCamera.x) * scale;
    if (outY) *outY = (wy - gCamera.y) * scale;
}

static void cameraUpdateFollow(void) {
    if (gCamera.followId < 0) return;
    SceneInstance *in = scene3dGetInstance(gCamera.followId);
    if (!in || !in->alive) { gCamera.followId = -1; return; }
    float scale = worldScale();
    float viewW = scale > 0.0f ? (float)gWindowWidth / scale : 0.0f;
    float viewH = scale > 0.0f ? (float)gWindowHeight / scale : 0.0f;
    gCamera.x = (float)in->pos.x - viewW / 2.0f;
    gCamera.y = (float)in->pos.y - viewH / 2.0f;
}

static void cameraUpdateWheelZoom(void) {
    float wheel = GetMouseWheelMove();
    if (wheel == 0.0f) return;
    float factor = wheel > 0.0f ? 1.25f : 0.8f;
    cameraSetZoom(gCamera.zoom * factor);
}

int instancesCollide(SceneInstance *a, SceneInstance *b) {
    if (!a || !b || !a->alive || !b->alive) return 0;
    if (!a->character || !b->character) return 0;
    PixelCharacter *ca = a->character;
    PixelCharacter *cb = b->character;
    for (int la = 0; la < ca->depth; la++) {
        PixelLayer *layer = &ca->layers[la];
        for (int ay = 0; ay < layer->height; ay++) {
            for (int ax = 0; ax < layer->width; ax++) {
                if (!cellSolid(layerCell(layer, ax, ay))) continue;
                int gx = a->pos.x + ax;
                int gy = a->pos.y + ay;
                int gz = a->pos.z + la;
                int bx = gx - b->pos.x;
                int by = gy - b->pos.y;
                int bz = gz - b->pos.z;
                if (bz < 0 || bz >= cb->depth) continue;
                if (cellSolid(layerCell(&cb->layers[bz], bx, by))) return 1;
            }
        }
    }
    return 0;
}

int collideCharacters(const char *nameA, const char *nameB) {
    if (!nameA || !nameB) return 0;
    for (int i = 0; i < gInstanceCount; i++) {
        SceneInstance *a = &gInstances[i];
        if (!a->alive || !a->character || !a->character->name) continue;
        if (strcmp(a->character->name, nameA) != 0) continue;
        for (int j = 0; j < gInstanceCount; j++) {
            if (i == j) continue;
            SceneInstance *b = &gInstances[j];
            if (!b->alive || !b->character || !b->character->name) continue;
            if (strcmp(b->character->name, nameB) != 0) continue;
            if (instancesCollide(a, b)) return 1;
        }
    }
    return 0;
}

typedef struct {
    PixelEventType type;
    char name[64];
    PixelEventHandler handler;
} EventBinding;

static EventBinding *gBindings = NULL;
static int gBindingCount = 0, gBindingCapacity = 0;

static PixelEvent *gQueue = NULL;
static int gQueueCount = 0, gQueueCapacity = 0;

static void addBinding(PixelEventType type, const char *name, PixelEventHandler handler) {
    if (gBindingCount >= gBindingCapacity) {
        gBindingCapacity = gBindingCapacity ? gBindingCapacity * 2 : 32;
        gBindings = (EventBinding *)pwRealloc(gBindings, (size_t)gBindingCapacity * sizeof(EventBinding));
    }
    EventBinding *b = &gBindings[gBindingCount++];
    b->type = type;
    snprintf(b->name, sizeof b->name, "%s", (name && handler) ? name : "");
    b->handler = handler;
}

static void pushEvent(PixelEvent ev) {
    if (gQueueCount >= gQueueCapacity) {
        gQueueCapacity = gQueueCapacity ? gQueueCapacity * 2 : 64;
        gQueue = (PixelEvent *)pwRealloc(gQueue, (size_t)gQueueCapacity * sizeof(PixelEvent));
    }
    gQueue[gQueueCount++] = ev;
}

int pollEvent(PixelEvent *outEvent) {
    if (gQueueCount == 0 || !outEvent) return 0;
    *outEvent = gQueue[0];
    memmove(gQueue, gQueue + 1, (size_t)(gQueueCount - 1) * sizeof(PixelEvent));
    gQueueCount--;
    return 1;
}

static void dispatchEvent(PixelEvent *ev) {
    for (int i = 0; i < gBindingCount; i++) {
        EventBinding *b = &gBindings[i];
        if (b->type != ev->type || !b->handler) continue;
        if (b->name[0] != '\0') {
            const char *selfName = (ev->self && ev->self->character)
                                   ? ev->self->character->name : NULL;
            if (!selfName || strcmp(b->name, selfName) != 0) continue;
        }
        b->handler(ev);
    }
}

static void fireGlobalEvent(PixelEventType type) {
    PixelEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.type = type;
    for (int i = 0; i < gBindingCount; i++) {
        EventBinding *b = &gBindings[i];
        if (b->type == type && b->name[0] == '\0' && b->handler)
            b->handler(&ev);
    }
}

void addEventToCharacterCollide(const char *characterName, PixelEventHandler handler) {
    addBinding(eventCollide, characterName, handler);
}
void addEventToCharacterMouseClick(const char *characterName, PixelEventHandler handler) {
    addBinding(eventMouseClick, characterName, handler);
}
void addEventToCharacterMouseEnter(const char *characterName, PixelEventHandler handler) {
    addBinding(eventMouseEnter, characterName, handler);
}
void addEventToCharacterMouseLeave(const char *characterName, PixelEventHandler handler) {
    addBinding(eventMouseLeave, characterName, handler);
}
void addEventUpdate(PixelEventHandler handler) { addBinding(eventUpdate, NULL, handler); }
void addEventDraw(PixelEventHandler handler)   { addBinding(eventDraw, NULL, handler); }

void emitEvent(int customId, void *data) {
    PixelEvent ev;
    memset(&ev, 0, sizeof ev);
    ev.type = eventCustom;
    ev.customId = customId;
    ev.data = data;
    pushEvent(ev);
}

static int instanceAtMouse(int mouseX, int mouseY) {
    int bestId = -1, bestZ = 0;
    int cellSz = cellScreenSize();
    for (int i = 0; i < gInstanceCount; i++) {
        SceneInstance *in = &gInstances[i];
        if (!in->alive || !in->character) continue;
        PixelCharacter *ch = in->character;
        for (int lz = 0; lz < ch->depth; lz++) {
            int zTotal = in->pos.z + lz;
            if (bestId >= 0 && zTotal < bestZ) continue;
            PixelLayer *layer = &ch->layers[lz];
            for (int ly = 0; ly < layer->height; ly++) {
                for (int lx = 0; lx < layer->width; lx++) {
                    if (!cellSolid(layerCell(layer, lx, ly))) continue;
                    int sx = cellScreenX(in, lz, lx);
                    int sy = cellScreenY(in, lz, ly);
                    if (mouseX >= sx && mouseX < sx + cellSz &&
                        mouseY >= sy && mouseY < sy + cellSz) {
                        if (bestId < 0 || zTotal > bestZ) {
                            bestId = in->id;
                            bestZ = zTotal;
                        }
                    }
                }
            }
        }
    }
    return bestId;
}

static void detectMouseEvents(void) {
    Vector2 m = GetMousePosition();
    int hoverId = instanceAtMouse((int)m.x, (int)m.y);

    if (hoverId != gHoveredInstanceId) {
        if (gHoveredInstanceId >= 0) {
            SceneInstance *old = scene3dGetInstance(gHoveredInstanceId);
            if (old) {
                PixelEvent ev; memset(&ev, 0, sizeof ev);
                ev.type = eventMouseLeave;
                ev.self = old;
                pushEvent(ev);
            }
        }
        if (hoverId >= 0) {
            SceneInstance *nw = scene3dGetInstance(hoverId);
            if (nw) {
                PixelEvent ev; memset(&ev, 0, sizeof ev);
                ev.type = eventMouseEnter;
                ev.self = nw;
                pushEvent(ev);
            }
        }
        gHoveredInstanceId = hoverId;
    }

    for (int i = 0; i < gInstanceCount; i++)
        gInstances[i].hovered = (gInstances[i].id == hoverId);

    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && hoverId >= 0) {
        SceneInstance *in = scene3dGetInstance(hoverId);
        if (in) {
            PixelEvent ev; memset(&ev, 0, sizeof ev);
            ev.type = eventMouseClick;
            ev.self = in;
            pushEvent(ev);
        }
    }
}

static void detectCollisionEvents(void) {
    for (int i = 0; i < gInstanceCount; i++) {
        SceneInstance *a = &gInstances[i];
        if (!a->alive || !a->character) continue;
        for (int j = i + 1; j < gInstanceCount; j++) {
            SceneInstance *b = &gInstances[j];
            if (!b->alive || !b->character) continue;
            if (!instancesCollide(a, b)) continue;
            PixelEvent ev; memset(&ev, 0, sizeof ev);
            ev.type = eventCollide; ev.self = a; ev.other = b; pushEvent(ev);
            ev.self = b; ev.other = a; pushEvent(ev);
        }
    }
}

static void drawDecor(void) {
    if (!gDecor) return;
    float scale = worldScale();
    int cellSz = cellScreenSize();
    const char *start = gDecor;
    const char *p = gDecor;
    int row = 0;
    for (;;) {
        if (*p == '\n' || *p == '\0') {
            for (const char *c = start; c < p; c++) {
                if (!cellSolid(*c)) continue;
                int sx = (int)(((float)(c - start) - gCamera.x) * scale);
                int sy = (int)(((float)row - gCamera.y) * scale);
                DrawRectangle(sx, sy, cellSz, cellSz, cellColor((unsigned char)*c));
            }
            if (*p == '\0') break;
            p++;
            start = p;
            row++;
            if (*start == '\0') break;
        } else {
            p++;
        }
    }
}

typedef struct {
    SceneInstance *inst;
    int lx, ly, lz;
    unsigned char c;
} DrawCell;

static DrawCell *gDrawCells = NULL;
static int gDrawCellCapacity = 0;

static int drawCellCompare(const void *pa, const void *pb) {
    const DrawCell *a = (const DrawCell *)pa;
    const DrawCell *b = (const DrawCell *)pb;
    int za = a->inst->pos.z + a->lz;
    int zb = b->inst->pos.z + b->lz;
    return za - zb;
}

static void drawScene(void) {
    drawDecor();
    int cellSz = cellScreenSize();
    int count = 0;
    for (int i = 0; i < gInstanceCount; i++) {
        SceneInstance *in = &gInstances[i];
        if (!in->alive || !in->character) continue;
        PixelCharacter *ch = in->character;
        for (int lz = 0; lz < ch->depth; lz++) {
            PixelLayer *layer = &ch->layers[lz];
            for (int ly = 0; ly < layer->height; ly++) {
                for (int lx = 0; lx < layer->width; lx++) {
                    char c = layerCell(layer, lx, ly);
                    if (!cellSolid(c)) continue;
                    if (count >= gDrawCellCapacity) {
                        gDrawCellCapacity = gDrawCellCapacity ? gDrawCellCapacity * 2 : 256;
                        gDrawCells = (DrawCell *)pwRealloc(gDrawCells, (size_t)gDrawCellCapacity * sizeof(DrawCell));
                    }
                    DrawCell *dc = &gDrawCells[count++];
                    dc->inst = in;
                    dc->lx = lx; dc->ly = ly; dc->lz = lz;
                    dc->c = (unsigned char)c;
                }
            }
        }
    }
    qsort(gDrawCells, (size_t)count, sizeof(DrawCell), drawCellCompare);
    for (int i = 0; i < count; i++) {
        DrawCell *dc = &gDrawCells[i];
        DrawRectangle(cellScreenX(dc->inst, dc->lz, dc->lx),
                      cellScreenY(dc->inst, dc->lz, dc->ly),
                      cellSz, cellSz,
                      cellColor(dc->c));
    }
}

void pixelWinInit(const char *title, int width, int height, int pixelSize) {
    if (gInitialized) return;
    if (pixelSize < 1) pixelSize = 1;
    InitWindow(width, height, title ? title : "pixelWin");
    SetTargetFPS(60);
    gPixelSize = pixelSize;
    gWindowWidth = width;
    gWindowHeight = height;
    gZShiftX = 1;
    gZShiftY = 1;
    gHoveredInstanceId = -1;
    cameraReset();
    memset(gPalette, 0, sizeof gPalette);
    gDefaultColor[0] = gDefaultColor[1] = gDefaultColor[2] = 0;
    gBgColor[0] = gBgColor[1] = gBgColor[2] = 0;
    gInitialized = 1;
}

int pixelWinIsOpen(void) {
    return gInitialized && !WindowShouldClose();
}

void pixelWinUpdate(void) {
    if (!gInitialized) return;
    PixelEvent ev;
    while (pollEvent(&ev)) dispatchEvent(&ev);
    fireGlobalEvent(eventUpdate);
    cameraUpdateFollow();
    cameraUpdateWheelZoom();
    detectMouseEvents();
    detectCollisionEvents();
    BeginDrawing();
        ClearBackground((Color){ gBgColor[0], gBgColor[1], gBgColor[2], 255 });
        drawScene();
    EndDrawing();
    fireGlobalEvent(eventDraw);
}

void pixelWinClose(void) {
    if (!gInitialized) return;
    for (int i = 0; i < gCharacterCount; i++)
        characterDestroy(gCharacters[i]);
    gCharacterCount = 0;
    free(gCharacters); gCharacters = NULL; gCharacterCapacity = 0;
    free(gInstances); gInstances = NULL; gInstanceCount = gInstanceCapacity = 0;
    free(gBindings); gBindings = NULL; gBindingCount = gBindingCapacity = 0;
    free(gQueue); gQueue = NULL; gQueueCount = gQueueCapacity = 0;
    free(gDrawCells); gDrawCells = NULL; gDrawCellCapacity = 0;
    free(gDecor); gDecor = NULL;
    cameraReset();
    CloseWindow();
    gInitialized = 0;
    gHoveredInstanceId = -1;
}

void pixelWinMouseCell(float *outX, float *outY) {
    Vector2 m = GetMousePosition();
    float scale = worldScale();
    if (outX) *outX = scale > 0.0f ? (float)m.x / scale + gCamera.x : 0.0f;
    if (outY) *outY = scale > 0.0f ? (float)m.y / scale + gCamera.y : 0.0f;
}

void pixelWinDraw(const char *pixels) {
    free(gDecor);
    gDecor = pixels ? pwStrdup(pixels) : NULL;
}

void pixelWinClear(void) {
    free(gDecor);
    gDecor = NULL;
}

#ifdef __cplusplus
}
#endif

#endif
