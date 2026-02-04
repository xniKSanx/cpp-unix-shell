//
// Created by Nikita Matrosov on 18/04/2025.
//

#include "SmallShell.h"
#include <unistd.h>
#include <iostream>
#include <vector>
#include <csignal>
#include <climits>
#include <algorithm>

// ==================================================================================
//                                Static Helper Functions
// ==================================================================================

/**
 * Removes the '&' sign from the end of a string if present.
 * Used to clean up background commands before execution.
 */
static void _removeBackgroundSignString(std::string &s)
{
    const std::string WHITESPACE = " \n\r\t\f\v";
    if (s.empty()) return;

    std::size_t idx = s.find_last_not_of(WHITESPACE);
    if (idx == std::string::npos) return;

    if (s[idx] == '&') {
        s.erase(idx, 1);                        // drop '&'
        // strip trailing spaces that may remain
        s.erase(s.find_last_not_of(WHITESPACE) + 1);
    }
}

/**
 * Finds a character in a string, ignoring instances inside single quotes.
 * Critical for parsing pipes (|) and redirections (>) correctly.
 */
static std::size_t findOutsideQuotes(const std::string& s, char ch)
{
    bool inSingle = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\'')  inSingle = !inSingle;
        else if (!inSingle && s[i] == ch)  return i;
    }
    return std::string::npos;
}

// ==================================================================================
//                                Lifecycle & Constructor
// ==================================================================================

SmallShell::SmallShell():
        m_joblist(),
        m_promptMsg("smash> ")
{
    m_reservedWordsSet = {
            "chprompt", "showpid", "pwd", "cd", "jobs", "fg", "quit", "kill", "alias", "unalias", "whoami", "netinfo"
    };
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == nullptr) {throw std::runtime_error("getcwd() error");}

    m_currentPwd = string(cwd);
    m_lastPwd = "-9999";
    m_cJobId = -1;
    m_cJPid = -1;
    m_cJCommandLine = "";
    m_cJisStopped = false;
    m_cJinsertionTime = 0;
}

// ==================================================================================
//                            Factory & Command Execution
// ==================================================================================

/**
 * Factory method: Parses the command line and creates the appropriate Command object.
 * Handles special syntax like pipes and redirection first.
 */
Command* SmallShell::CreateCommand(const char *cmd_line)
{
    // check if the input line is empty or null
    if (!cmd_line) return nullptr;
    std::string trimmed = _trim(cmd_line);
    if (trimmed.empty()) return nullptr;

    // check for pipe command ('|') - must be outside quotes
    if (findOutsideQuotes(trimmed, '|') != std::string::npos)
        return new PipeCommand(cmd_line);

    // check for redirection command ('>') - must be outside quotes
    if (findOutsideQuotes(trimmed, '>') != std::string::npos)
        return new RedirectionCommand(cmd_line);


    // parse command into a vector of strings
    std::vector<std::string> args = splitCommandLine(trimmed);

    if (args.empty()) return nullptr; // safety check

    std::string firstWord = args[0];

    Command *cmd = nullptr;

    // check against all built-in commands and create the right object
    if (firstWord == "chprompt")       cmd = new ChpromtCommand(cmd_line);
    else if (firstWord == "showpid")   cmd = new ShowPidCommand(cmd_line);
    else if (firstWord == "pwd")       cmd = new GetCurrDirCommand(cmd_line);
    else if (firstWord == "cd")        cmd = new ChangeDirCommand(cmd_line);
    else if (firstWord == "jobs")      cmd = new JobsCommand(cmd_line);
    else if (firstWord == "fg")        cmd = new ForegroundCommand(cmd_line);
    else if (firstWord == "quit")      cmd = new QuitCommand(cmd_line);
    else if (firstWord == "kill")      cmd = new KillCommand(cmd_line);
    else if (firstWord == "alias")     cmd = new AliasCommand(cmd_line);
    else if (firstWord == "unalias")   cmd = new UnAliasCommand(cmd_line);
    else if (firstWord == "unsetenv")  cmd = new UnSetEnvCommand(cmd_line);
    else if (firstWord == "watchproc") cmd = new WatchProcCommand(cmd_line);
    else if (firstWord == "du")        cmd = new DiskUsageCommand(cmd_line);
    else if (firstWord == "whoami")    cmd = new WhoAmICommand(cmd_line);
    else if (firstWord == "netinfo")   cmd = new NetInfo(cmd_line);

        // if it's not a built-in command, treat it as an external command
    else                               cmd = new ExternalCommand(cmd_line);


    return cmd;
}

/**
 * Main Execution Loop logic:
 * 1. Cleans up zombies.
 * 2. Checks aliases.
 * 3. Dispatches to Foreground or Background execution.
 */
void SmallShell::executeCommand(const char *org_cmd_line) {
    if (org_cmd_line == nullptr) return;
    if (_trim(string(org_cmd_line)).empty()) return;

    SmallShell &smash = SmallShell::getInstance();

    // clean up finished jobs (zombies) before starting a new one
    smash.m_joblist.removeFinishedJobs();

    // check if command is an alias and replace it
    string procceced_cmd_line = smash.reproduceWithAlias(org_cmd_line);

    // foreground command - run and wait
    if (!_isBackgroundComamnd(procceced_cmd_line.c_str())) {
        Command *cmd = CreateCommand(procceced_cmd_line.c_str());
        if (cmd) {
            cmd->execute();
            delete cmd;
        }
        return;
    }

    // background command (ends with &)
    Command *cmd = CreateCommand(procceced_cmd_line.c_str());
    if (!cmd) return;

    // special case: built-in commands run in foreground even if they have &
    if (dynamic_cast<BuiltInCommand*>(cmd) != nullptr) {
        delete cmd;
        _removeBackgroundSignString(procceced_cmd_line); // remove the &

        // recreate as a normal foreground command
        cmd = CreateCommand(procceced_cmd_line.c_str());
        if (cmd) {
            cmd->execute();
            delete cmd;
        }
        return;
    }

    // external command in background - save cmd string for jobs list
    smash.setNextBGPrint(std::string(org_cmd_line));
    cmd->execute();
    delete cmd;
}

// ==================================================================================
//                                Alias Management
// ==================================================================================

string  SmallShell::reproduceWithAlias(const char *cmd_line) {
    char* args[COMMAND_MAX_ARGS];
    int argsNum = _parseCommandLine(cmd_line, args);

    if (argsNum == 0) {
        string  str = string(cmd_line);
        return str;
    }

    string result;

    if (this->isAlias(string(args[0]))) {
        result = this->getAliasMeaning(string(args[0]));
        for (int i = 1; i < argsNum; i++) {
            result += " " + string(args[i]);
        }
    } else {
        result = string(cmd_line);
    }

    for (int i = 0; i < argsNum; ++i) {
        free(args[i]);
    }

    return result;
}

bool SmallShell::isReservedWord(const string &word) {
    return m_reservedWordsSet.find(word) != m_reservedWordsSet.end();
}

bool SmallShell::isAlias(const string &alias) {
    return m_aliasesMap.find(alias) != m_aliasesMap.end();
}

void SmallShell::addAlias(const string &alias, const string &commandStr) {
    if (m_aliasesMap.find(alias) != m_aliasesMap.end()) {
        throw std::invalid_argument("Alias already exists");
    }
    m_aliasesMap.insert({alias, commandStr});
    m_aliasOrder.push_back(alias);
}

void SmallShell::removeAlias(const string &alias) {
    if (m_aliasesMap.find(alias) == m_aliasesMap.end()) {
        throw std::invalid_argument("Alias not found");
    }
    m_aliasesMap.erase(alias);
    m_aliasOrder.erase(std::remove(m_aliasOrder.begin(),
                                   m_aliasOrder.end(), alias),
                       m_aliasOrder.end());
}

string SmallShell::getAliasMeaning(const string &alias) {
    if (m_aliasesMap.find(alias) == m_aliasesMap.end()) {
        throw std::invalid_argument("Alias not found");
    }
    return m_aliasesMap[alias];
}

void SmallShell::printAllAliases()
{
    for (const auto& key : m_aliasOrder) {
        if(m_aliasesMap.find(key) != m_aliasesMap.end()){
            std::cout << key << "='" << m_aliasesMap[key] << "'" << std::endl;
        }
    }
}

// ==================================================================================
//                                Job ID Management
// ==================================================================================

int SmallShell::getNextFreeJobId()
{
    int max_id = 0;
    for (int i = 1; i < MAX_BG_JOBS; ++i) {
        if (m_jobIDArray[i] && i > max_id) {
            max_id = i;
        }
    }
    return max_id + 1;
}

void SmallShell::setJobIdFree(int jobId) {
    if (jobId == -1) { return;}
    if (jobId <= 0 || jobId >= MAX_BG_JOBS) {
        throw std::out_of_range("Job ID out of range");
    }
    m_jobIDArray[jobId] = false;
}

void SmallShell::setJobIdUsed(int jobId) {
    if (jobId == -1) { return;}
    if (jobId <= 0 || jobId >= MAX_BG_JOBS) {
        throw std::out_of_range("Job ID out of range");
    }
    m_jobIDArray[jobId] = true;
}

bool SmallShell::isJobIdUsed(int jobId) const {
    if (jobId == -1) { return false;}
    if (jobId <= 0 || jobId >= MAX_BG_JOBS) {
        throw std::out_of_range("Job ID out of range");
    }
    return m_jobIDArray[jobId];
}

// ==================================================================================
//                          Foreground Job State (Accessors)
// ==================================================================================

bool SmallShell::isFGrunning() const {
    return m_cJobId != -1;
}

void SmallShell::updateSmashAfterCjFinished(){
    this->setJobIdFree(m_cJobId);
    m_cJobId = -1;
    m_cJPid = -1;
    m_cJCommandLine = "";
    m_cJisStopped = false;
    m_cJinsertionTime = 0;
    m_cJPrintCommandLine = "";
}

void SmallShell::setBGJobToFGbyJID(int job){
    if (isContainsBGJob(job)){
        m_cJobId = job;
        m_cJPid = getJobById(job)->getPid();
        m_cJCommandLine = getJobById(job)->getCommandLine();
        m_cJisStopped = getJobById(job)->getStopped();
        m_cJinsertionTime = getJobById(job)->getInsertionTime();
        m_cJPrintCommandLine = getJobById(job)->getPrintCommandLine();

    }
    else{throw std::invalid_argument("Job not found");}
}

pid_t SmallShell::getCJPid() const{ return m_cJPid; }
void SmallShell::setCJPid(pid_t mCJPid){ m_cJPid = mCJPid; }

int SmallShell::getCJobId() const{ return m_cJobId;  }
void SmallShell::setCJobId(int mCJobId){ m_cJobId = mCJobId; }

const string& SmallShell::getCJCommandLine() const{ return m_cJCommandLine; }
void SmallShell::setCJCommandLine(const string &mCJCommandLine){ m_cJCommandLine = mCJCommandLine; }

bool SmallShell::isCJisStopped() const{ return m_cJisStopped; }
void SmallShell::setCJisStopped(bool mCJisStopped) { m_cJisStopped = mCJisStopped; }

string SmallShell::getCJPrintCommandLine() const{ return m_cJPrintCommandLine; }

time_t SmallShell::getCJinsertionTime() const{ return m_cJinsertionTime; }
void SmallShell::setCJinsertionTime(time_t mCJinsertionTime){ m_cJinsertionTime = mCJinsertionTime; }

// ==================================================================================
//                          Background Jobs List Wrappers
// ==================================================================================

void SmallShell::addBGJob(pid_t pid, const string& proccesed_cmd_line, bool isStopped, int jobId,const string & print_cmd_line) {
    m_joblist.addJob(pid, proccesed_cmd_line, isStopped,jobId, ((print_cmd_line=="")? proccesed_cmd_line :print_cmd_line));
}

void SmallShell::printJobsList() { m_joblist.printJobsList(); }

int SmallShell::getLastJobJId() {
    int jobId;
    m_joblist.getLastJob(&jobId);
    return jobId;
}

pid_t SmallShell::getLastJobPid() {
    int jobId;
    JobsList::JobEntry *job = m_joblist.getLastJob(&jobId);
    if (job == nullptr) {
        throw std::runtime_error("No last job found");
    }
    return job->getPid();
}

int SmallShell::getBGNumOfJobs() { return m_joblist.getSize(); }
bool SmallShell::isBGNotEmpty() { return !m_joblist.isEmpty(); }

void SmallShell::killAllBGJobsWithoutPrint() { m_joblist.killAllJobs(); }
bool SmallShell::isContainsBGJob(int jobId) { return m_joblist.isContainsJob(jobId); }
void SmallShell::removeBGjobByJID(int jobId) { m_joblist.removeJobByIdWithoutKillingIt(jobId); }

string SmallShell::getBGJobPrintMsgByJobId(int jobId) {
    if (m_joblist.isContainsJob(jobId)) {
        return m_joblist.getJobById(jobId)->getPrintCommandLine();
    }
    else { throw std::invalid_argument("Job not found"); }
}

pid_t SmallShell::getBGjobPidById(int jobId) {
    if (m_joblist.isContainsJob(jobId)) {
        return m_joblist.getJobById(jobId)->getPid();
    }
    else { throw std::invalid_argument("Job not found"); }
}

// ==================================================================================
//                                Environment & Config
// ==================================================================================

const string SmallShell::getPrompt(){return m_promptMsg;}
void SmallShell::setPrompt(const string &prompt) { m_promptMsg = prompt; }

string SmallShell::getLastPwd() { return m_lastPwd; }
string SmallShell::getCurrentPwd() { return m_currentPwd; }