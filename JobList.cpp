//
// Created by Nikita Matrosov on 18/04/2025.
//

#include <iostream>
#include <vector>
#include <sstream>
#include <sys/wait.h>
#include <csignal>
#include <regex>
#include <algorithm>
#include "JobList.h"
#include "SmallShell.h"

using namespace std;

// ==================================================================================
//                                Global / Static Helpers
// ==================================================================================

std::ostream &operator<<(std::ostream &os, const JobsList::JobEntry &job) {
    os << "[" << job.getJobId() << "] " << job.getPrintCommandLine()
       << ((job.getStopped()) ? " (stopped)" : "" ) << "\n";
    return os;
}

// ==================================================================================
//                            Class: JobEntry Implementation
// ==================================================================================

JobsList::JobEntry::JobEntry(pid_t pid, int jobId, const string &procecced_commandLine, bool isStopped, const string &print_cmd_line):
        m_pid(pid),
        m_procecced_commandLine(procecced_commandLine),
        m_org_commandLine(""),
        m_jobId(jobId),
        m_isStopped(isStopped),
        m_insertionTime(0)
{
    if (print_cmd_line == "") {
        m_org_commandLine = procecced_commandLine;
    } else {
        m_org_commandLine = print_cmd_line;
    }
    m_insertionTime = time(nullptr);
}

// ==================================================================================
//                                Lifecycle (Destructor)
// ==================================================================================

JobsList::~JobsList() {
    for (auto& pair : m_jobsMap) {
        delete pair.second;
    }
}

// ==================================================================================
//                                Core Job Management
// ==================================================================================

void JobsList::addJob(pid_t pid, const string &proccesed_cmd_line, bool isStopped, int jobId, const string &print_cmd_line) {
    SmallShell &smash = SmallShell::getInstance();

    if (jobId == -1) {
        jobId = smash.getNextFreeJobId();
    }

    JobEntry* jobToAdd = new JobEntry(pid, jobId, proccesed_cmd_line, isStopped, print_cmd_line);
    smash.setJobIdUsed(jobId);
    m_jobsMap.insert({jobToAdd->getJobId(), jobToAdd});

    if (isStopped) {
        m_stoppedJobsQueue.push_back(jobToAdd);
    } else {
        m_runningJobsQueue.push_back(jobToAdd);
    }
}

void JobsList::removeFinishedJobs() {
    SmallShell &smash = SmallShell::getInstance();
    vector<JobEntry*> finishedJobs;

    //  Identify finished jobs
    for (const auto & jobPair: m_jobsMap) {
        JobEntry* jobPtr = jobPair.second;
        int status;
        pid_t pState = waitpid(jobPtr->getPid(), &status, WNOHANG);

        if (pState == -1) {
            perror("smash error: waitpid failed");
            continue;
        }

        if (pState == 0) {
            continue; // Job still running
        } else {
            finishedJobs.push_back(jobPtr);
        }
    }

    //  Remove them from all structures
    for(const auto & jobPtr : finishedJobs) {
        smash.setJobIdFree(jobPtr->getJobId());
        m_jobsMap.erase(jobPtr->getJobId());

        // Remove from queues using erase-remove idiom
        m_runningJobsQueue.erase(std::remove(m_runningJobsQueue.begin(), m_runningJobsQueue.end(), jobPtr), m_runningJobsQueue.end());
        m_stoppedJobsQueue.erase(std::remove(m_stoppedJobsQueue.begin(), m_stoppedJobsQueue.end(), jobPtr), m_stoppedJobsQueue.end());

        delete jobPtr;
    }
}

void JobsList::killAllJobs() {
    SmallShell &smash = SmallShell::getInstance();

    // Clear queues first
    m_runningJobsQueue.clear();
    m_stoppedJobsQueue.clear();

    for (const auto & jobPair : m_jobsMap) {
        JobEntry* jobPtr = jobPair.second;
        smash.setJobIdFree(jobPtr->getJobId());

        // Send SIGKILL
        kill(jobPtr->getPid(), SIGKILL);

        delete jobPtr;
    }
    m_jobsMap.clear();
}

void JobsList::removeJobByIdWithoutKillingIt(int jobId) {
    auto it = m_jobsMap.find(jobId);
    if (it != m_jobsMap.end()) {
        JobEntry* jobPtr = it->second;
        m_jobsMap.erase(it);

        m_runningJobsQueue.erase(std::remove(m_runningJobsQueue.begin(), m_runningJobsQueue.end(), jobPtr), m_runningJobsQueue.end());
        m_stoppedJobsQueue.erase(std::remove(m_stoppedJobsQueue.begin(), m_stoppedJobsQueue.end(), jobPtr), m_stoppedJobsQueue.end());

        delete jobPtr;
    }
}

// ==================================================================================
//                                  Lookups & Getters
// ==================================================================================

JobsList::JobEntry* JobsList::getJobById(int jobId) {
    auto it = m_jobsMap.find(jobId);
    if (it != m_jobsMap.end()) {
        return it->second;
    }
    return nullptr;
}

JobsList::JobEntry* JobsList::getLastJob(int *lastJobId) {
    if (m_jobsMap.empty()) {
        return nullptr;
    }
    // Map is sorted by key (jobId), so rbegin() gives the largest ID
    JobEntry* lastJobEntryPtr = m_jobsMap.rbegin()->second;

    if (lastJobId != nullptr) {
        *lastJobId = lastJobEntryPtr->getJobId();
    }
    return lastJobEntryPtr;
}

JobsList::JobEntry* JobsList::getLastStoppedJob(int *jobId) {
    if (m_stoppedJobsQueue.empty()) {
        return nullptr;
    }
    JobEntry* lastStoppedJobEntryPtr = m_stoppedJobsQueue.back();
    *jobId = lastStoppedJobEntryPtr->getJobId();
    return lastStoppedJobEntryPtr;
}

// ==================================================================================
//                                     Printing
// ==================================================================================

void JobsList::printJobsList() {
    for (const auto & job : m_jobsMap) {
        cout << *job.second;
    }
}