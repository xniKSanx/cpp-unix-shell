//
// Created by Nikita Matrosov on 21/11/2025.
//
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <signal.h>
#include <ftw.h>
#include <limits.h>
#include <ctime>
#include <algorithm>
#include <cstdlib>
#include <pwd.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <arpa/inet.h>
#include <iostream>
#include <sstream>
#include <vector>
#include <string>
#include <iomanip>
#include <regex>
#include <cstring>

#include "Commands.h"
#include "SmallShell.h"

using namespace std;

extern char** environ;
#define NO_DIRECTORY_SET "-9999"

// ==================================================================================
//                                Global Static Helpers
// ==================================================================================

const std::string WHITESPACE = " \n\r\t\f\v";

#if 0
#define FUNC_ENTRY()  \
  cout << __PRETTY_FUNCTION__ << " --> " << endl;

#define FUNC_EXIT()  \
  cout << __PRETTY_FUNCTION__ << " <-- " << endl;
#else
#define FUNC_ENTRY()
#define FUNC_EXIT()
#endif

static bool deleteEntry(const char* name) {
    size_t len = strlen(name);
    char** currentEnvVar = environ;
    while (*currentEnvVar) {
        if (strncmp(*currentEnvVar, name, len) == 0 && (*currentEnvVar)[len] == '=') {
            char** remainingEnvVars = currentEnvVar;
            while (*remainingEnvVars) {
                *remainingEnvVars = *(remainingEnvVars + 1);
                ++remainingEnvVars;
            }
            return true;
        }
        ++currentEnvVar;
    }
    return false;
}

static bool envExistsProcfs(const char* name) {
    std::string env;
    std::string path = "/proc/self/environ";
    int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) return false;
    char buf[4096]; ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) env.append(buf,n);
    close(fd);
    size_t len = strlen(name);
    size_t pos = 0;
    while (pos < env.size()) {
        const char* p = env.c_str() + pos;
        if (strncmp(p, name, len) == 0 && p[len] == '=') return true;
        pos += strlen(p) + 1;
    }
    return false;
}

static bool isNumber(const std::string &s,int *num = nullptr) {
    if (s.empty()) {return false;}
    try{
        if (num != nullptr) {
            *num = std::stoi(s);
        }else {
            std::stoi(s);
        }
        return true;
    } catch (...) {
        return false;
    }
}

// ------------------------ String Manipulation Helpers -------------------------

string _ltrim(const std::string &s) {
    size_t start = s.find_first_not_of(WHITESPACE);
    return (start == std::string::npos) ? "" : s.substr(start);
}

string _rtrim(const std::string &s) {
    size_t end = s.find_last_not_of(WHITESPACE);
    return (end == std::string::npos) ? "" : s.substr(0, end + 1);
}

string _trim(const std::string &s) {
    return _rtrim(_ltrim(s));
}

// ------------------------ Command Parsing Helpers -----------------------------

int _parseCommandLine(const char *cmd_line, char **args) {
    FUNC_ENTRY()
    int i = 0;
    std::istringstream iss(_trim(string(cmd_line)).c_str());
    for (std::string s; iss >> s;) {
        args[i] = (char *) malloc(s.length() + 1);
        memset(args[i], 0, s.length() + 1);
        strcpy(args[i], s.c_str());
        args[++i] = NULL;
    }
    return i;
    FUNC_EXIT()
}

std::vector<std::string> splitCommandLine(const std::string &cmd_line) {
    std::istringstream iss(cmd_line);
    std::vector<std::string> args;
    std::string word;
    while (iss >> word) {
        args.push_back(word);
    }
    return args;
}

bool _isBackgroundComamnd(const char *cmd_line) {
    const string str(cmd_line);
    if(str.empty()) return false;
    return str[str.find_last_not_of(WHITESPACE)] == '&';
}

void _removeBackgroundSign(char *cmd_line) {
    const string str(cmd_line);
    // find last character other than spaces
    size_t idx = str.find_last_not_of(WHITESPACE);
    // if all characters are spaces then return
    if (idx == string::npos) {
        return;
    }
    // if the command line does not end with & then return
    if (cmd_line[idx] != '&') {
        return;
    }
    // replace the & (background sign) with space and then remove all tailing spaces.
    cmd_line[idx] = ' ';
    // truncate the command line string up to the last non-space character
    cmd_line[str.find_last_not_of(WHITESPACE, idx) + 1] = 0;
}

// ==================================================================================
//                                Class: Command
// ==================================================================================

Command::Command(const char *cmd_line) {
    // 1. Check if command is background
    m_isBackground = _isBackgroundComamnd(cmd_line);

    // 2. Copy the raw command line
    m_cmd_line = strdup(cmd_line);

    // 3. Process background sign if needed
    if (m_isBackground){
        _removeBackgroundSign(m_cmd_line);
        _trim(m_cmd_line);
    }

    // 4. Parse arguments
    char* temp_arg[COMMAND_MAX_LENGTH];
    m_args_num = _parseCommandLine(m_cmd_line, temp_arg);

    // 5. Store arguments in vector
    for (int i = 0; i < m_args_num; i++) {
        m_args.push_back(string(temp_arg[i]));
        free(temp_arg[i]);
    }
}

Command::~Command() {
    free(m_cmd_line);
}

// ==================================================================================
//                           Class: BuiltInCommand
// ==================================================================================

BuiltInCommand::BuiltInCommand(const char* cmd_line)
        : Command(cmd_line){}

// ==================================================================================
//                           Class: ExternalCommand
// ==================================================================================

ExternalCommand::ExternalCommand(const char* cmd_line)
        : Command(cmd_line){}

void ExternalCommand::execute()
{
    SmallShell &smash = SmallShell::getInstance();

    std::string cmdTxt = std::string(getCmdLine());

    // Check for wildcards to determine if bash is needed
    bool complex = (cmdTxt.find('*') != std::string::npos ||
                    cmdTxt.find('?') != std::string::npos);

    bool bg = this->isBackground();

    // Fork Process
    pid_t cpid = fork();

    if (cpid == -1) {
        perror("smash error: fork failed");
        return;
    }

    // Child Process Logic
    if (cpid == 0) {
        setpgrp(); // Create new process group

        if (complex) {
            // Complex command: let /bin/bash handle it
            execl("/bin/bash", "bash", "-c", cmdTxt.c_str(), (char*)nullptr);
            perror("smash error: execl failed");
        } else {
            // Simple command: parse and execute directly
            char *argv[COMMAND_MAX_ARGS];
            _parseCommandLine(cmdTxt.c_str(), argv);

            execvp(argv[0], argv);
            perror("smash error: execvp failed");
        }

        _exit(EXIT_FAILURE); // Ensure child exits if exec fails
    }

    // Parent Process Logic
    if (bg) {
        // --- Background Execution ---

        // Handle the printed command text (handling any buffered text from parsing)
        std::string printTxt = smash.takeNextBGPrint();
        if (printTxt.empty())
            printTxt = _trim(std::string(getCmdLine())) + " &";

        int jobId = smash.getNextFreeJobId();

        // Add to job list
        smash.addBGJob(cpid,
                       cmdTxt,
                       false,
                       jobId,
                       printTxt);

        smash.setJobIdUsed(jobId);

    } else {
        // --- Foreground Execution ---

        smash.setCJPid(cpid);
        smash.setCJobId(smash.getNextFreeJobId());
        smash.setCJCommandLine(cmdTxt);

        int status;
        // Wait for the child process
        waitpid(cpid, &status, WUNTRACED);

        if (WIFSTOPPED(status)) {
            // Process was stopped (Ctrl-Z)
            smash.setCJisStopped(true);
            smash.setCJinsertionTime(time(nullptr));
            smash.addBGJob(cpid, cmdTxt, true, smash.getCJobId(), smash.getCJPrintCommandLine());
        } else {
            // Process finished normally
            smash.updateSmashAfterCjFinished();
        }
    }
}
// ==================================================================================
//                           Class: RedirectionCommand
// ==================================================================================

RedirectionCommand::RedirectionCommand(const char* cmd_line)
        : Command(cmd_line){}

void RedirectionCommand::execute()
{
    std::string line = std::string(getCmdLine());
    bool append = false;

    // 1. Parse Redirection Type (Overwrite '>' or Append '>>')
    std::size_t arrow = line.find(">>");
    if (arrow != std::string::npos) {
        append = true;
    } else {
        arrow = line.find('>');
        if (arrow == std::string::npos) {
            std::cerr << "smash error: redirection: invalid command" << std::endl;
            return;
        }
    }

    // Split Command: Left (Action) & Right (File)
    std::string leftPart = _trim(line.substr(0, arrow));
    std::string filePart = _trim(line.substr(arrow + (append ? 2 : 1)));

    if (filePart.empty()) {
        std::cerr << "smash error: redirection: missing output file" << std::endl;
        return;
    }

    //  Open Output File
    int flags = O_WRONLY | O_CREAT | (append ? O_APPEND : O_TRUNC);
    // Mode 0666 allows read/write for owner/group/others (modified by umask)
    int fd = open(filePart.c_str(), flags, 0666);
    if (fd == -1) {
        perror("smash error: open failed");
        return;
    }

    //  Redirect stdout
    int saved_stdout = dup(STDOUT_FILENO); // Backup original stdout


    if (saved_stdout == -1) {
        perror("smash error: dup failed");
        close(fd);
        return;
    }

    if (dup2(fd, STDOUT_FILENO) == -1) {
        perror("smash error: dup2 failed");
        close(saved_stdout);
        close(fd);
        return;
    }

    // File descriptor is now duplicated to stdout, we can close the original file fd
    close(fd);

    // Execute the command (recursively)
    SmallShell::getInstance().executeCommand(leftPart.c_str());

    // Restore stdout
    if (dup2(saved_stdout, STDOUT_FILENO) == -1) {
        perror("smash error: dup2 failed");
    }
    close(saved_stdout);
}

// ==================================================================================
//                           Class: PipeCommand
// ==================================================================================

// --- Static Helper for Pipe Execution ---

static pid_t launchProcessWithPipe(const std::string& cmd,
                                   int read_end,
                                   int write_end,
                                   bool connect_stdin,
                                   bool connect_stdout,
                                   int fds[2],
                                   bool use_stderr)
{
    pid_t cpid = fork();
    if (cpid == -1) {
        perror("smash error: fork failed");
        return -1;
    }

    // --- Child Process ---
    if (cpid == 0) {
        setpgrp(); // Set process group

        // Connect Input (if needed)
        if (connect_stdin && dup2(read_end, STDIN_FILENO) == -1) {
            perror("smash error: dup2 failed");
            exit(EXIT_FAILURE);
        }

        // Connect Output  -> either stdout or stderr based on pipe type
        int target_fd = use_stderr ? STDERR_FILENO : STDOUT_FILENO;
        if (connect_stdout && dup2(write_end, target_fd) == -1) {
            perror("smash error: dup2 failed");
            exit(EXIT_FAILURE);
        }

        // Close raw pipe descriptors (child has its own copies now)
        close(fds[0]);
        close(fds[1]);

        // Execute logic
        SmallShell::getInstance().executeCommand(cmd.c_str());
        exit(EXIT_SUCCESS);
    }

    // --- Parent Process returns child PID ---
    return cpid;
}

// --- PipeCommand Implementation ---

PipeCommand::PipeCommand(const char* cmd_line)
        : Command(cmd_line)
{
}

void PipeCommand::execute()
{
    std::string text = std::string(getCmdLine());
    bool use_stderr = false;

    // 1. Parse Pipe Type (Standard '|' or Error Pipe '|&')
    std::size_t bar = text.find("|&");
    if (bar != std::string::npos) {
        use_stderr = true;
    } else {
        bar = text.find('|');
    }

    if (bar == std::string::npos) {
        std::cerr << "smash error: pipe: invalid syntax\n";
        return;
    }

    // Split Command
    std::string left = _trim(text.substr(0, bar));
    std::string right = _trim(text.substr(bar + (use_stderr ? 2 : 1)));

    // Clean background signs from sub-commands if present
    if (_isBackgroundComamnd(left.c_str()))  _removeBackgroundSign(&left[0]);
    if (_isBackgroundComamnd(right.c_str())) _removeBackgroundSign(&right[0]);

    // 3. Create Pipe
    int fds[2];
    if (pipe(fds) == -1) {
        perror("smash error: pipe failed");
        return;
    }

    // Fork Children

    // Left Child: Writes to pipe (fds[1])
    // connect_stdin=false, connect_stdout=true
    pid_t left_pid = launchProcessWithPipe(left, -1, fds[1], false, true, fds, use_stderr);

    if (left_pid == -1) {
        close(fds[0]);
        close(fds[1]);
        return;
    }

    // Right Child: Reads from pipe (fds[0])
    // connect_stdin=true, connect_stdout=false
    pid_t right_pid = launchProcessWithPipe(right, fds[0], -1, true, false, fds, use_stderr);

    if (right_pid == -1) {
        close(fds[0]);
        close(fds[1]);
        return;
    }

    // 5. Parent Cleanup & Wait
    // Close pipe ends in parent so children get EOF/SIGPIPE correctly
    close(fds[0]);
    close(fds[1]);

    waitpid(left_pid, nullptr, 0);
    waitpid(right_pid, nullptr, 0);
}

// ==================================================================================
//                            Job Control Commands
// ==================================================================================

void JobsCommand::execute() {
    SmallShell &smash = SmallShell::getInstance();
    smash.printJobsList();
}

void ForegroundCommand::execute() {
    SmallShell &smash = SmallShell::getInstance();

    //  Validate Arguments
    if (this->getArgsNum() > 2) {
        cerr << "smash error: fg: invalid arguments" << endl;
        return;
    }

    int jobId = -1;

    //  Determine Job ID to bring to foreground
    if (this->getArgsNum() == 2) {
        // Case: Specific Job ID requested
        string arg = this->getArg(1);
        if (!isNumber(arg, &jobId)) {
            cerr << "smash error: fg: invalid arguments" << endl;
            return;
        }
        if (!smash.isBGNotEmpty()){
            cerr << "smash error: fg: job-id " << jobId << " does not exist" << endl;
            return;
        }
        if (!smash.isContainsBGJob(jobId)) {
            cerr << "smash error: fg: job-id " << jobId << " does not exist" << endl;
            return;
        }

    } else {
        // Case: No argument provided -> take the last job (maximal ID)
        if (smash.isBGNotEmpty()){
            jobId = smash.getLastJobJId();
        } else {
            cerr << "smash error: fg: jobs list is empty"<< endl;
            return;
        }
    }

    // Bring Job to Foreground
    smash.setBGJobToFGbyJID(jobId);

    // Continue process if it was stopped
    if (smash.isCJisStopped()) {
        kill(smash.getCJPid(), SIGCONT);
    }

    // Print command line and PID
    cout << smash.getCJPrintCommandLine() << " " << smash.getCJPid() << endl;

    // Remove from background list (since it's now FG)
    smash.removeBGjobByJID(jobId);

    //  Wait for the process to finish or stop
    int status;
    pid_t finishedPid = waitpid(smash.getCJPid(), &status, WUNTRACED);

    if (finishedPid == -1) {
        perror("smash error: waitpid failed");
        smash.updateSmashAfterCjFinished();
        return;
    }

    // Handle Post-Wait Status
    if (WIFSTOPPED(status)) {
        // Process stopped (Ctrl-Z) -> Move back to background
        smash.setCJisStopped(true);
        smash.setCJinsertionTime(time(nullptr));
        smash.addBGJob(smash.getCJPid(), smash.getCJCommandLine(), true, smash.getCJobId(), smash.getCJPrintCommandLine().c_str());
    } else {
        // Process finished -> Cleanup
        smash.updateSmashAfterCjFinished();
    }
}

void QuitCommand::execute(){
    SmallShell &smash = SmallShell::getInstance();

    // Kill Foreground Process if exists
    pid_t fg_pid = smash.isFGrunning() ? smash.getCJPid() : -1;
    if (fg_pid > 0) {
        kill(fg_pid, SIGKILL);
    }

    // Check for "kill" argument
    if ((this->getArgsNum() >= 2) && (this->getArg(1) == "kill")) {

        // Calculate total running jobs (including FG if it was running)
        int runningJobsNum = ((smash.isFGrunning() ? smash.getBGNumOfJobs() + 1 : smash.getBGNumOfJobs()));
        cout << "smash: sending SIGKILL signal to " << runningJobsNum << " jobs:" << endl;

        // Iterate and print jobs to be killed
        for (int i = 1; i < MAX_BG_JOBS; i++) {
            if (!smash.isJobIdUsed(i)) { continue; }

            if (smash.isContainsBGJob(i)) {
                cout << smash.getBGjobPidById(i) << ": " << smash.getBGJobPrintMsgByJobId(i) << endl;
                continue;
            } else if (i == smash.getCJobId()) {
                cout << smash.getCJPid() << ": " << smash.getCJPrintCommandLine() << endl;
                continue;
            }
            throw (std::runtime_error("error accured in quit command"));
        }

        // Perform the actual killing
        smash.killAllBGJobsWithoutPrint();
    }

    // 3. Exit the shell
    exit(0);
}

void KillCommand::execute()
{
    SmallShell& smash = SmallShell::getInstance();

    //  Validate Argument Count
    if (getArgsNum() != 3) {
        cerr << "smash error: kill: invalid arguments" << endl;
        return;
    }

    // Parse Signal Argument
    std::string sig_str = getArg(1);
    if (sig_str.empty()) {
        cerr << "smash error: kill: invalid arguments" << endl;
        return;
    } else if (sig_str[0] != '-') {
        cerr << "smash error: kill: invalid arguments" << endl;
        return;
    }

    int sig_num;
    try {
        sig_num = std::stoi(sig_str.substr(1));
    } catch (...) {
        cerr << "smash error: kill: invalid arguments" << endl;
        return;
    }

    //  Parse Job ID Argument
    int job_id;
    try {
        job_id = std::stoi(getArg(2));
    } catch (...) {
        cerr << "smash error: kill: invalid arguments" << endl;
        return;
    }

    //  Validate Job Logic
    if (job_id < 0) {
        cerr << "smash error: kill: invalid arguments" << endl;
        return;
    }
    if (job_id == 0) {
        cerr << "smash error: kill: job-id " << job_id << " does not exist" << endl;
        return;
    }

    if (!smash.isContainsBGJob(job_id)) {
        cerr << "smash error: kill: job-id " << job_id << " does not exist" << endl;
        return;
    }

    // 5. Send Signal
    pid_t pid = smash.getBGjobPidById(job_id);
    cout << "signal number " << sig_num << " was sent to pid " << pid << endl;

    if (kill(pid, sig_num) == -1) {
        perror("smash error: kill failed");
        return;
    }
}
// ==================================================================================
//                        Shell Environment Commands
// ==================================================================================

// ==================================================================================
//                           Class: ChpromtCommand
// ==================================================================================

ChpromtCommand::ChpromtCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {
    if (getArgsNum() > 1) {
        m_promptMsg = getArg(1);
    } else {
        m_promptMsg = "smash";
    }
}

void ChpromtCommand::execute() {
    SmallShell::getInstance().setPrompt(m_promptMsg + "> ");
}

// ==================================================================================
//                           Class: ShowPidCommand
// ==================================================================================

ShowPidCommand::ShowPidCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {}

void ShowPidCommand::execute() {
    std::cout << "smash pid is " << getpid() << std::endl;
}

// ==================================================================================
//                           Class: GetCurrDirCommand
// ==================================================================================

GetCurrDirCommand::GetCurrDirCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {}

void GetCurrDirCommand::execute() {
    SmallShell &smash = SmallShell::getInstance();
    cout << smash.getCurrentPwd() << std::endl;
}

// ==================================================================================
//                           Class: ChangeDirCommand
// ==================================================================================

ChangeDirCommand::ChangeDirCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {
    int args_num = getArgsNum();

    if (args_num > 2) {
        cerr << "smash error: cd: too many arguments" << endl;
        m_dir = "";
    }
    else if (args_num == 2) {
        m_dir = getArg(1);
    }
    else {
        m_dir = "";
    }
}

void ChangeDirCommand::execute() {
    SmallShell &smash = SmallShell::getInstance();

    // Handle "-" argument (Previous Directory)
    if (m_dir == "-") {
        m_dir = smash.getLastPwd();
        if (m_dir == NO_DIRECTORY_SET) {
            cerr << "smash error: cd: OLDPWD not set" << endl;
            return;
        }
    }

    // Handle empty directory (no operation)
    if (m_dir == "") {
        return;
    }

    // Perform change directory syscall
    if (chdir(m_dir.c_str()) != 0) {
        perror("smash error: chdir failed");
        return;
    }

    // Update PWD in shell state
    char cwd[PATH_MAX];
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        perror("smash error: getcwd failed");
        return;
    }

    smash.setLastPwd(smash.getCurrentPwd());
    smash.setCurrentPwd(string(cwd));
}

// ==================================================================================
//                            Alias & Environment Variables
// ==================================================================================

// ==================================================================================
//                           Class: AliasCommand
// ==================================================================================


void AliasCommand::execute()
{
    SmallShell& smash = SmallShell::getInstance();

    // If no arguments provided, print all aliases
    if (this->getArgsNum() == 1) {
        smash.printAllAliases();
        return;
    }

    // Parse command using Regex to extract name and command
    // Format: alias name='command'
    static const std::regex re(R"(^\s*alias\s+([A-Za-z0-9_]+)='([^']*)'\s*$)");

    std::smatch m;
    std::string line = getCmdLine();

    if (!std::regex_match(line, m, re)) {
        std::cerr << "smash error: alias: invalid alias format" << std::endl;
        return;
    }

    std::string name = m[1];
    std::string body = m[2];

    // Validation: Ensure name is not a reserved word or existing alias
    if (smash.isReservedWord(name) || smash.isAlias(name)) {
        std::cerr << "smash error: alias: " << name
                  << " already exists or is a reserved command" << std::endl;
        return;
    }

    // Add the new alias
    smash.addAlias(name, body);
}

// ==================================================================================
//                           Class: UnAliasCommand
// ==================================================================================
void UnAliasCommand::execute() {
    SmallShell &smash = SmallShell::getInstance();

    // Validate arguments count
    if (this->getArgsNum() <= 1) {
        cerr << "smash error: unalias: not enough arguments" << endl;
        return;
    }

    // Iterate over arguments and remove aliases
    for (int i = 1; i < this->getArgsNum(); ++i) {
        string aliasName = this->getArg(i);

        if (smash.isAlias(aliasName)) {
            smash.removeAlias(aliasName);
        } else {
            // Stop at first error as per assignment logic implies (or just report it)
            cerr << "smash error: unalias: " << aliasName << " alias does not exist" << endl;
            return;
        }
    }
}

// ==================================================================================
//                           Class: UnSetEnvCommand
// ==================================================================================

UnSetEnvCommand::UnSetEnvCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {
    // Constructor passthrough
}

void UnSetEnvCommand::execute() {
    // Validate arguments count
    if (getArgsNum() == 1) {
        std::cerr << "smash error: unsetenv: not enough arguments\n";
        return;
    }

    int i = 1;
    while (i < getArgsNum()) {
        std::string varName = getArg(i);
        const char* var = varName.c_str();

        // Check if environment variable exists
        if (!envExistsProcfs(var)) {
            std::cerr << "smash error: unsetenv: "
                      << varName << " does not exist\n";
            return;
        }

        // Delete the environment variable
        deleteEntry(var);
        i++;
    }
}

// ==================================================================================
//                            System Info & Monitoring
// ==================================================================================

// ==================================================================================
//                           Helpers for WatchProc
// ==================================================================================

static bool readFileWhole(const std::string& filePath, std::string& outputBuffer) {
    int fd = open(filePath.c_str(), O_RDONLY);
    if (fd < 0) return false;
    char buf[4096];
    ssize_t bytesRead;
    while ((bytesRead = read(fd, buf, sizeof(buf))) > 0)
        outputBuffer.append(buf, bytesRead);
    close(fd);
    return bytesRead >= 0;
}

static bool readTotals(const std::string& pidStr, long& userTime, long& systemTime,
                       long& startTime, long& systemTotal) {
    std::string fileContent;
    if (!readFileWhole("/proc/"+pidStr+"/stat", fileContent)) return false;
    std::istringstream statStream(fileContent);
    std::vector<std::string> tokens;
    for (std::string word; statStream >> word;) tokens.push_back(word);
    if (tokens.size() < 22) return false;
    userTime   = std::stol(tokens[13]);
    systemTime = std::stol(tokens[14]);
    startTime  = std::stol(tokens[21]);

    fileContent.clear();
    if (!readFileWhole("/proc/stat", fileContent)) return false;
    std::istringstream cpuStatStream(fileContent);
    std::string dummy;
    cpuStatStream >> dummy;
    systemTotal = 0;
    long value;
    while (cpuStatStream >> value) systemTotal += value;
    return true;
}

// ==================================================================================
//                           Class: WatchProcCommand
// ==================================================================================

WatchProcCommand::WatchProcCommand(const char *cmd_line) : BuiltInCommand(cmd_line) {}

void WatchProcCommand::execute() {
    // Validate Arguments
    if (getArgsNum() != 2) {
        std::cerr << "smash error: watchproc: invalid arguments\n";
        return;
    }
    std::string pidStr = getArg(1);
    int pid = -1;
    try { pid = std::stoi(pidStr); }
    catch (...) {
        std::cerr << "smash error: watchproc: invalid arguments\n";
        return;
    }

    // Read First Sample (t1)
    long ut1, st1, stime1, sys1;
    if (!readTotals(pidStr, ut1, st1, stime1, sys1)) {
        std::cerr << "smash error: watchproc: pid " << pid
                  << " does not exist\n";
        return;
    }

    // Wait Interval
    sleep(1);

    // Read Second Sample (t2)
    long ut2, st2, stime2, sys2;
    if (!readTotals(pidStr, ut2, st2, stime2, sys2)) {
        std::cerr << "smash error: watchproc: pid " << pid
                  << " does not exist\n";
        return;
    }

    // Calculate CPU Usage
    long procDelta = (ut2 + st2) - (ut1 + st1);
    long sysDelta  = sys2 - sys1;
    double cpuPct  = sysDelta ? (100.0 * procDelta / sysDelta) : 0.0;

    //  Calculate Memory Usage
    double memMB = 0.0;
    std::string status;
    if (readFileWhole("/proc/"+pidStr+"/status", status)) {
        std::istringstream ls(status); std::string line;
        while (std::getline(ls, line))
            if (line.rfind("VmRSS:",0) == 0) {
                std::istringstream p(line); std::string lbl; double kb;
                p >> lbl >> kb; memMB = kb / 1024.0; break;
            }
    }

    //  Print Results
    std::cout << "PID: " << pid
              << " | CPU Usage: "    << std::fixed << std::setprecision(1)
              << cpuPct << "% | Memory Usage: "
              << std::fixed << std::setprecision(1) << memMB << " MB\n";
}

// ==================================================================================
//                           Helpers for DiskUsage
// ==================================================================================

static off_t duplicationStaticTotal = 0;

static int CallBackFunctionOfDuForNFTW (const char *path , const struct stat *sb ,
                                        int  , struct FTW * )
{
    if (S_ISDIR(sb->st_mode)) {duplicationStaticTotal += sb->st_blocks;}
    else if (S_ISREG(sb->st_mode)) {duplicationStaticTotal += sb->st_blocks;}
    return 0;
}

// ==================================================================================
//                           Class: DiskUsageCommand
// ==================================================================================

DiskUsageCommand::DiskUsageCommand(const char *cmd_line) : Command(cmd_line) {
    // Constructor passthrough
}

void DiskUsageCommand::execute()
{
    char *argv[COMMAND_MAX_ARGS];
    int   argc = _parseCommandLine(getCmdLine(), argv);

    auto cleanup = [&]() {for (int i = 0; i < argc; ++i) free(argv[i]);};

    // 1. Validate Arguments
    if (argc > 2) {
        std::cerr << "smash error: du: too many arguments\n";
        cleanup();
        return;
    }

    const char *target = (argc == 2) ? argv[1] : ".";
    struct stat st{};

    // 2. Check if directory exists
    if (stat(target, &st) == -1) {
        std::cerr << "smash error: du: directory " << target
                  << " does not exist\n";
        cleanup();
        return;
    }

    // 3. Reset Counter & Calculate Usage (using nftw)
    duplicationStaticTotal = 0;
    if (nftw(target, CallBackFunctionOfDuForNFTW, 20, FTW_PHYS) == -1) {
        perror("smash error: nftw failed");
        cleanup();
        return;
    }

    // 4. Print Result
    std::cout << "Total disk usage: "<< (duplicationStaticTotal + 1) / 2 << " KB\n";

    cleanup();
}

// ==================================================================================
//                           Class: WhoAmICommand
// ==================================================================================

WhoAmICommand::WhoAmICommand(const char *cmd_line) : Command(cmd_line) {
    // Constructor passthrough
}

void WhoAmICommand::execute()
{
    uid_t my_uid = getuid();
    int fd = open("/etc/passwd", O_RDONLY);
    if (fd == -1) {
        perror("smash error: whoami: open failed");
        return;
    }

    constexpr size_t BUF_SZ = 4096;
    char buf[BUF_SZ];
    std::string line;
    ssize_t nread;

    // 1. Parse /etc/passwd line by line
    while ((nread = read(fd, buf, BUF_SZ)) > 0) {
        for (ssize_t i = 0; i < nread; ++i) {
            char c = buf[i];
            if (c != '\n') {
                line.push_back(c);

            } else {
                // Process complete line
                size_t start = 0, colon;
                std::string fields[7];
                int idx = 0;
                for (; idx < 7; ) {
                    colon = line.find(':', start);
                    fields[idx++] = line.substr(start,
                                                (colon == std::string::npos ? colon : colon - start));
                    if (colon == std::string::npos) break;
                    start = colon + 1;
                }

                // 2. Check if UID matches
                if (idx >= 6) {
                    char* endptr = nullptr;
                    unsigned long file_uid = std::strtoul(fields[2].c_str(), &endptr, 10);
                    if (*endptr == '\0' && static_cast<uid_t>(file_uid) == my_uid) {
                        std::cout << fields[0] << " " << fields[5] << std::endl;
                        close(fd);
                        return;
                    }
                }
                line.clear();
            }
        }
    }

    if (nread == -1)
        perror("smash error: whoami: read failed");
    else
        std::cerr << "smash error: whoami: user not found" << std::endl;

    close(fd);
}

// ==================================================================================
//                           Class: NetInfo
// ==================================================================================

NetInfo::NetInfo(const char *cmd_line) : Command(cmd_line) {
    // Constructor passthrough
}

void NetInfo::execute() {
    // 1. Validate Arguments
    if (getArgsNum() <= 1) {
        std::cerr << "smash error: netinfo: interface not specified" << std::endl;
        return;
    }
    if (getArgsNum() > 2) {
        std::cerr << "smash error: netinfo: too many arguments" << std::endl;
        return;
    }

    // Clean up interface name (remove potential trailing '&' if passed improperly)
    std::string interfaceName = getArg(1);
    if (!interfaceName.empty() && interfaceName.back() == '&') {
        interfaceName.pop_back();
        while (!interfaceName.empty() && isspace(interfaceName.back())) {
            interfaceName.pop_back();
        }
    }

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (sockfd < 0) {
        perror("smash error: netinfo: socket failed");
        return;
    }

    struct ifreq ifr;
    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, interfaceName.c_str(), IFNAMSIZ - 1);
    ifr.ifr_name[IFNAMSIZ - 1] = '\0';

    // 2. Get IP Address
    if (ioctl(sockfd, SIOCGIFADDR, &ifr) < 0) {
        std::cerr << "smash error: netinfo: interface " << interfaceName
                  << " does not exist" << std::endl;
        close(sockfd);
        return;
    }
    struct sockaddr_in* ip_addr = (struct sockaddr_in*)&ifr.ifr_addr;
    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &ip_addr->sin_addr, ipStr, INET_ADDRSTRLEN);
    std::string ipAddress = ipStr;

    // 3. Get Subnet Mask
    if (ioctl(sockfd, SIOCGIFNETMASK, &ifr) < 0) {
        perror("smash error: netinfo: SIOCGIFNETMASK failed");
        close(sockfd);
        return;
    }
    struct sockaddr_in* netmask_addr = (struct sockaddr_in*)&ifr.ifr_addr;
    inet_ntop(AF_INET, &netmask_addr->sin_addr, ipStr, INET_ADDRSTRLEN);
    std::string subnetMask = ipStr;

    close(sockfd);

    // 4. Get Default Gateway (from /proc/net/route)
    std::string defaultGateway = "";
    int fd = open("/proc/net/route", O_RDONLY);
    if (fd >= 0) {
        char buffer[4096];
        ssize_t bytesRead = read(fd, buffer, sizeof(buffer) - 1);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            char* line = strtok(buffer, "\n");
            line = strtok(nullptr, "\n"); // Skip header
            while (line) {
                char iface[IFNAMSIZ], destHex[9], gatewayHex[9];
                if (sscanf(line, "%s %8s %8s", iface, destHex, gatewayHex) == 3) {
                    if (interfaceName == iface && strcmp(destHex, "00000000") == 0) {
                        unsigned long gw_val = strtoul(gatewayHex, nullptr, 16);
                        uint32_t gw = static_cast<uint32_t>(gw_val);
                        struct in_addr gw_addr;
                        gw_addr.s_addr = htonl(gw);
                        char gwStr[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &gw_addr, gwStr, INET_ADDRSTRLEN);
                        defaultGateway = gwStr;
                        break;
                    }
                }
                line = strtok(nullptr, "\n");
            }
        }
        close(fd);
    }

    // 5. Get DNS Servers (from /etc/resolv.conf)
    std::vector<std::string> dnsServers;
    fd = open("/etc/resolv.conf", O_RDONLY);
    if (fd >= 0) {
        char buffer[4096];
        ssize_t bytesRead = read(fd, buffer, sizeof(buffer) - 1);
        if (bytesRead > 0) {
            buffer[bytesRead] = '\0';
            char* line = strtok(buffer, "\n");
            while (line) {
                while (*line == ' ' || *line == '\t') ++line;
                if (strncmp(line, "nameserver", 10) == 0 && isspace(line[10])) {
                    char* ip = line + 11;
                    while (*ip == ' ' || *ip == '\t') ++ip;
                    char* end = ip;
                    while (*end && !isspace(*end)) ++end;
                    *end = '\0';
                    if (*ip)
                        dnsServers.push_back(std::string(ip));
                }
                line = strtok(nullptr, "\n");
            }
        }
        close(fd);
    }

    std::string dnsList;
    for (size_t i = 0; i < dnsServers.size(); ++i) {
        dnsList += dnsServers[i];
        if (i < dnsServers.size() - 1) {
            dnsList += ", ";
        }
    }

    // 6. Print Final Info
    std::cout << "IP Address: " << ipAddress << std::endl;
    std::cout << "Subnet Mask: " << subnetMask << std::endl;
    std::cout << "Default Gateway: " << defaultGateway << std::endl;
    std::cout << "DNS Servers: " << dnsList << std::endl;
}
