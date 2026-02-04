#include <iostream>
#include <signal.h>
#include "Commands.h"
#include "signals.h"
#include "SmallShell.h"

int main(int argc, char *argv[]) {
    if (signal(SIGINT, ctrlCHandler) == SIG_ERR) {
        perror("smash error: failed to set ctrl-C handler");
    }
    SmallShell &smash = SmallShell::getInstance();
    while (true) {
        std::cout << smash.getPrompt();
        std::string cmd_line;
        if (!std::getline(std::cin, cmd_line)) {
            break; // exit on EOF (Ctrl+D)
        }
        smash.executeCommand(cmd_line.c_str());
    }

    return 0;
}