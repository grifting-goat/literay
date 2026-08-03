#include "command.h"

#include "stb_ds.h"
#include <stdio.h>
#include <string.h>

void parse_command(Commander* cmd, int argc, char* argv[]);

static DWORD WINAPI command_thread_task(LPVOID param) {
    Commander* cmd = (Commander*)param;

    char line[256];
    for (;;) {
        if (!fgets(line, sizeof(line), stdin)) { break; }

        char* argv[16];
        int argc = 0;
        char* tok = strtok(line, " \t\r\n");
        while (tok && argc < 16) {
            argv[argc++] = tok;
            tok = strtok(NULL, " \t\r\n");
        }
        if (argc == 0) { continue; }

        parse_command(cmd, argc, argv);
    }
    return 0;
}

Commander commander_create() {
    Commander cmd = {0};

    cmd.command_map = NULL;
    sh_new_strdup(cmd.command_map);

    return cmd;
}

void commander_start(Commander* cmd) {
    cmd->pool.thread = CreateThread(NULL, 0, command_thread_task, cmd, 0, NULL);
}

void commander_end(Commander* cmd) {
    CancelSynchronousIo(cmd->pool.thread);
    WaitForSingleObject(cmd->pool.thread, INFINITE);
    CloseHandle(cmd->pool.thread);
}

void command_register(Commander* cmd, const char* name, CommandFunc func, void* context) {
    CommandEntry entry = {.fn = func, .context = context};
    shput(cmd->command_map, name, entry);
}

void parse_command(Commander* cmd, int argc, char* argv[]) {
    int idx = shgeti(cmd->command_map, argv[0]);
    if (idx == -1) {
        printf("unknown command: %s\n", argv[0]);
        return;
    }
    CommandEntry entry = cmd->command_map[idx].value;
    entry.fn(entry.context, argc, argv);
}