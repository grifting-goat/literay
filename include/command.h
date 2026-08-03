#ifndef COMMAND_H
#define COMMAND_H

#include <windows.h>
#undef IN
#undef OUT
#undef OPTIONAL

typedef void (*CommandFunc)(void* context, int argc, char* argv[]);

typedef struct {
    CommandFunc fn;
    void* context; //needed so structs can mutate state
} CommandEntry;

typedef struct {
    char* key;
    CommandEntry value;
} CommandMap;

typedef struct {
    HANDLE thread;
} CommandThreadPool_t;

typedef struct {
    CommandMap* command_map;
    CommandThreadPool_t pool;
} Commander;

Commander commander_create();
void commander_start(Commander* cmd);
void commander_end(Commander* cmd);

void command_register(Commander* cmd, const char* name, CommandFunc func, void* context);


#endif //COMMAND_H
