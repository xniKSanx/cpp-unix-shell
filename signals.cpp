
#include <iostream>
#include <csignal>
#include "SmallShell.h"

using namespace std;


void ctrlCHandler(int)
{
    cout << "smash: got ctrl-C" << endl;

    SmallShell &smash = SmallShell::getInstance();

    if (!smash.isFGrunning())
        return;

    pid_t fg_pid = smash.getCJPid();

    if (kill(fg_pid, SIGKILL) == -1) {
        perror("smash error: kill failed");
        return;
    }

    cout << "smash: process " << fg_pid << " was killed" << endl;

    smash.updateSmashAfterCjFinished();
}
