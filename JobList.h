#ifndef SMASH_JOB_LIST_H_
#define SMASH_JOB_LIST_H_

#include <vector>
#include <string>
#include <deque>
#include <map>
#include <set>
#include <iostream>
#include <ctime>

// Same macros as in SmallShell.h to ensure consistency
#define COMMAND_MAX_LENGTH (200)
#define COMMAND_MAX_ARGS (20)
#define MAX_BG_JOBS (101)

using namespace std;

// Forward declaration
class SmallShell;

// ==================================================================================
//                                Class: JobsList
// ==================================================================================
class JobsList {
public:
    // ==============================================================================
    //                            Nested Class: JobEntry
    // ==============================================================================
    class JobEntry {
    private:
        // ------------------------- Private Fields ---------------------------------
        pid_t m_pid;
        string m_procecced_commandLine;
        string m_org_commandLine;
        int m_jobId;
        bool m_isStopped;
        time_t m_insertionTime;

    public:
        // ----------------------- Constr & Destr -----------------------------------
        JobEntry(pid_t pid, int jobId, const string &cmd_line, bool isStopped = false, const string &print_cmd_line = "");
        ~JobEntry() = default;

        // --------------------------- Getters --------------------------------------
        pid_t getPid() const { return m_pid; }
        int getJobId() const { return m_jobId; }
        string getCommandLine() const { return m_procecced_commandLine; }
        string getPrintCommandLine() const { return m_org_commandLine; }
        bool getStopped() const { return m_isStopped; }
        time_t getInsertionTime() const { return m_insertionTime; }
    };

private:
    // ==============================================================================
    //                                 Private Fields
    // ==============================================================================

    // ------------------------ Containers & Data Structures ------------------------
    map<int, JobEntry*> m_jobsMap;
    deque<JobEntry*> m_runningJobsQueue;
    deque<JobEntry*> m_stoppedJobsQueue;

    // ---------------------------- Friend Declarations -----------------------------
    friend std::ostream& operator<<(std::ostream&, const JobEntry&);

public:
    // ==============================================================================
    //                            Lifecycle (Constr/Destr)
    // ==============================================================================
    JobsList() = default;
    ~JobsList();

    // ==============================================================================
    //                            Core Job Management
    // ==============================================================================

    // Add a new job to the list (running or stopped)
    void addJob(pid_t pid, const string& proccesed_cmd_line, bool isStopped = false, int jobId = -1, const string &print_cmd_line = "");

    // Removes finished jobs from the list (zombie cleanup)
    void removeFinishedJobs();

    // Kill all jobs (SIGKILL) and clear the list
    void killAllJobs();

    // Remove a specific job by ID from the data structures without sending a signal
    void removeJobByIdWithoutKillingIt(int jobId);

    // ==============================================================================
    //                              Lookup & Access
    // ==============================================================================

    JobEntry *getJobById(int jobId);

    // Retreives the last job added (pointer) and optionally its ID via out-param
    JobEntry *getLastJob(int *lastJobId);

    // Retreives the last stopped job (pointer) and optionally its ID via out-param
    JobEntry *getLastStoppedJob(int *jobId);

    bool isContainsJob(int jobId) const { return m_jobsMap.find(jobId) != m_jobsMap.end(); }

    // ==============================================================================
    //                             Status & Printing
    // ==============================================================================

    void printJobsList();

    int getSize() const { return m_jobsMap.size(); }

    bool isEmpty() const { return m_jobsMap.empty(); }
};

#endif //SMASH_JOB_LIST_H_