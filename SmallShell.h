
#ifndef SMASH_SMALL_SHELL_H_
#define SMASH_SMALL_SHELL_H_

#include <vector>
#include <string>
#include <deque>
#include <map>
#include <set>
#include <iostream>
#include "JobList.h"
#include "Commands.h"

#define COMMAND_MAX_LENGTH (200)
#define COMMAND_MAX_ARGS (20)
#define MAX_BG_JOBS (101)

using namespace std;

// ==================================================================================
//                                Global Helper Functions
// ==================================================================================
// String manipulation and command line parsing utilities
string _trim(const std::string &s);
bool _isBackgroundComamnd(const char *cmd_line);
void _removeBackgroundSign(char *cmd_line);
int _parseCommandLine(const char *cmd_line, char **args);
std::vector<std::string> splitCommandLine(const std::string &cmd_line);


// ==================================================================================
//                                Class: SmallShell
// ==================================================================================
class SmallShell {
private:
    // ==============================================================================
    //                                 Private Fields
    // ==============================================================================

    // ------------------------ Containers & Data Structures ------------------------
    JobsList m_joblist;

    // Bitmap to track used job IDs (index corresponds to Job ID)
    bool m_jobIDArray[MAX_BG_JOBS] = {false};

    map<string, string> m_aliasesMap;
    set<string> m_reservedWordsSet;
    std::vector<std::string> m_aliasOrder; // Maintains insertion order for printing

    // --------------------------- Shell State & Config -----------------------------
    string m_promptMsg;
    string m_lastPwd;     // Stores previous directory for 'cd -'
    string m_currentPwd;

    // -------------------------- Current Foreground Job ----------------------------
    // Holds the state of the currently running foreground process (for signal handling)
    pid_t m_cJPid;
    int m_cJobId;
    string m_cJCommandLine;
    string m_cJPrintCommandLine;
    bool m_cJisStopped;
    time_t m_cJinsertionTime;

    // -------------------------- Background Job Management -------------------------
    std::string m_nextBGPrintCmdLine;

    // ==============================================================================
    //                                Private Methods
    // ==============================================================================
    SmallShell(); // Private Constructor for Singleton

    friend class ChangeDirCommand;

    void setLastPwd(const string &lastPwd) { m_lastPwd = lastPwd; }
    void setCurrentPwd(const string &currentPwd) { m_currentPwd = currentPwd; }
    JobsList::JobEntry *getJobById(int jobId) { return m_joblist.getJobById(jobId); }

public:
    // ==============================================================================
    //                           Singleton Pattern Access
    // ==============================================================================
    static SmallShell &getInstance() {
        static SmallShell instance;
        return instance;
    }

    SmallShell(SmallShell const &) = delete;
    void operator=(SmallShell const &) = delete;
    ~SmallShell() = default;

    // ==============================================================================
    //                          Core Execution & Factory
    // ==============================================================================

    // Main entry point: Parses, handles aliases, and executes a command line
    void executeCommand(const char *procceced_cmd_line);

    // Factory method: Creates a specific Command object based on the first word
    Command *CreateCommand(const char *cmd_line);

    // ==============================================================================
    //                         Foreground Job Getters/Setters
    // ==============================================================================
    pid_t getCJPid() const;
    void setCJPid(pid_t mCJPid);

    int getCJobId() const;
    void setCJobId(int mCJobId);

    const string &getCJCommandLine() const;
    void setCJCommandLine(const string &mCJCommandLine);

    bool isCJisStopped() const;
    void setCJisStopped(bool mCJisStopped);

    string getCJPrintCommandLine() const;

    time_t getCJinsertionTime() const;
    void setCJinsertionTime(time_t mCJinsertionTime);

    bool isFGrunning() const;

    // Resets the shell's foreground job state (called after FG process ends or is stopped)
    void updateSmashAfterCjFinished();

    // Moves a background job to the foreground (updates internal state for signals)
    void setBGJobToFGbyJID(int job);

    // ==============================================================================
    //                             Environment & Prompt
    // ==============================================================================
    string getLastPwd();
    string getCurrentPwd();

    const string getPrompt();
    void setPrompt(const string &prompt);

    // ==============================================================================
    //                            Job ID Management
    // ==============================================================================

    // Returns the smallest unused positive integer for a new job
    int getNextFreeJobId();
    void setJobIdUsed(int jobId);
    void setJobIdFree(int jobId);
    bool isJobIdUsed(int jobId) const;

    // ==============================================================================
    //                        Background Jobs Management
    // ==============================================================================
    void addBGJob(pid_t pid, const string& proccesed_cmd_line, bool isStopped = false, int jobId = -1, const string & print_cmd_line = "");
    void printJobsList();
    int getLastJobJId();
    pid_t getLastJobPid();
    int getBGNumOfJobs();
    bool isBGNotEmpty();
    void killAllBGJobsWithoutPrint(); // Used by 'quit kill'
    bool isContainsBGJob(int jobId);
    void removeBGjobByJID(int jobId);

    // Iterates over jobs and removes those that have finished (waitpid with WNOHANG)
    void removeFinishedJobs() { m_joblist.removeFinishedJobs(); }

    pid_t getBGjobPidById(int jobId);
    string getBGJobPrintMsgByJobId(int jobId);

    // buffer mechanism to handle correct printing of complex background commands
    void setNextBGPrint(const std::string& s) { m_nextBGPrintCmdLine = s; }
    std::string takeNextBGPrint() {
        std::string tmp = m_nextBGPrintCmdLine;
        m_nextBGPrintCmdLine.clear();
        return tmp;
    }

    // ==============================================================================
    //                             Alias Management
    // ==============================================================================
    bool isReservedWord(const string &word);
    bool isAlias(const string &alias);
    void addAlias(const string &alias, const string &commandStr);
    void removeAlias(const string &alias);
    string getAliasMeaning(const string &alias);

    // Replaces the command word with its alias value if it exists in the map
    string reproduceWithAlias(const char* cmd_line);
    void printAllAliases();
};

#endif //SMASH_SMALL_SHELL_H_