#ifndef SMASH_COMMAND_H_
#define SMASH_COMMAND_H_

#include <vector>
#include <string>
#include <deque>
#include <map>
#include <set>
#include <ctime>
#include <iostream>

// Forward declarations
class JobsList;

// Constants
#define COMMAND_MAX_LENGTH (200)
#define COMMAND_MAX_ARGS (20)
#define MAX_BG_JOBS (101)

using namespace std;

// ==================================================================================
//                                Class: Command (Abstract Base)
// ==================================================================================

class Command {
private:
    vector<string> m_args;
    char *m_cmd_line;
    int m_args_num;
    bool m_isBackground;

public:
    Command(const char *cmd_line);
    virtual ~Command();

    // --------------------------- Getters --------------------------------------
    int getArgsNum() const { return m_args_num; }
    char* getCmdLine() const { return m_cmd_line; }
    bool isBackground() const { return m_isBackground; }
    string getArg(int i) const {
        if (i < 0 || i >= m_args_num) {
            throw std::out_of_range("Index out of range");
        }
        return m_args[i];
    }

    // --------------------------- Abstract Methods -----------------------------
    virtual void execute() = 0;
};

// ==================================================================================
//                            Intermediate Base Classes
// ==================================================================================

class BuiltInCommand : public Command {
public:
    BuiltInCommand(const char *cmd_line);
    virtual ~BuiltInCommand() {}
};

class ExternalCommand : public Command {
public:
    ExternalCommand(const char *cmd_line);
    virtual ~ExternalCommand() {}

    void execute() override;
};

// ==================================================================================
//                            Special Commands (Pipes & IO)
// ==================================================================================

class RedirectionCommand : public Command {
public:
    explicit RedirectionCommand(const char *cmd_line);
    virtual ~RedirectionCommand() {}

    void execute() override;
};

class PipeCommand : public Command {
public:
    PipeCommand(const char *cmd_line);
    virtual ~PipeCommand() {}

    void execute() override;
};

// ==================================================================================
//                            Job Control Commands
// ==================================================================================

class JobsCommand : public BuiltInCommand {
public:
    JobsCommand(const char *cmd_line): BuiltInCommand(cmd_line) {}
    virtual ~JobsCommand() {}

    void execute() override;
};

class ForegroundCommand : public BuiltInCommand {
public:
    ForegroundCommand(const char *cmd_line): BuiltInCommand(cmd_line) {}
    virtual ~ForegroundCommand() {}

    void execute() override;
};

class KillCommand : public BuiltInCommand {
public:
    KillCommand(const char *cmd_line): BuiltInCommand(cmd_line) {}
    virtual ~KillCommand() {}

    void execute() override;
};

class QuitCommand : public BuiltInCommand {
public:
    QuitCommand(const char *cmd_line): BuiltInCommand(cmd_line) {}
    virtual ~QuitCommand() {}

    void execute() override;
};

// ==================================================================================
//                            Shell Environment Commands
// ==================================================================================

class ChpromtCommand : public BuiltInCommand {
private:
    string m_promptMsg;
public:
    ChpromtCommand(const char *cmd_line);
    virtual ~ChpromtCommand() = default;

    void execute() override;
};

class ShowPidCommand : public BuiltInCommand {
public:
    ShowPidCommand(const char *cmd_line);
    virtual ~ShowPidCommand() {}

    void execute() override;
};

class GetCurrDirCommand : public BuiltInCommand {
public:
    GetCurrDirCommand(const char *cmd_line);
    virtual ~GetCurrDirCommand() {}

    void execute() override;
};

class ChangeDirCommand : public BuiltInCommand {
private:
    string m_dir;
public:
    ChangeDirCommand(const char *cmd_line);
    virtual ~ChangeDirCommand() {}

    void execute() override;
};

// ==================================================================================
//                            Alias & Environment Variables
// ==================================================================================

class AliasCommand : public BuiltInCommand {
public:
    AliasCommand(const char *cmd_line): BuiltInCommand(cmd_line) {}
    virtual ~AliasCommand() {}

    void execute() override;
};

class UnAliasCommand : public BuiltInCommand {
public:
    UnAliasCommand(const char *cmd_line): BuiltInCommand(cmd_line) {}
    virtual ~UnAliasCommand() {}

    void execute() override;
};

class UnSetEnvCommand : public BuiltInCommand {
public:
    UnSetEnvCommand(const char *cmd_line);
    virtual ~UnSetEnvCommand() {}

    void execute() override;
};

// ==================================================================================
//                            System Info & Monitoring
// ==================================================================================

class DiskUsageCommand : public Command {
public:
    DiskUsageCommand(const char *cmd_line);
    virtual ~DiskUsageCommand() {}

    void execute() override;
};

class WhoAmICommand : public Command {
public:
    WhoAmICommand(const char *cmd_line);
    virtual ~WhoAmICommand() {}

    void execute() override;
};

class NetInfo : public Command {
public:
    NetInfo(const char *cmd_line);
    virtual ~NetInfo() {}

    void execute() override;
};

class WatchProcCommand : public BuiltInCommand {
public:
    WatchProcCommand(const char *cmd_line);
    virtual ~WatchProcCommand() {}

    void execute() override;
};

#endif //SMASH_COMMAND_H_