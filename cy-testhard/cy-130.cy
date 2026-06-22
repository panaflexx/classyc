/* Test 130: Stress test - many features combined */
#include <stdio.h>
#include "list.h"
#include "map.h"

interface Task {
    void execute();
    String getName();
};

class ComputeTask impl Task {
    int id;
    int iterations;
    ComputeTask(int i, int it) { this.id = i; this.iterations = it; }
    void execute() {
        int sum = 0;
        for (int j = 0; j < this.iterations; j++) sum += j;
        printf("Task %d computed sum=%d\n", this.id, sum);
    }
    String getName() { return f"ComputeTask-{this.id}"; }
};

class IOTask impl Task {
    String resource;
    IOTask(String r) { this.resource = r; }
    void execute() { printf("IO Task reading %s\n", this.resource); }
    String getName() { return f"IOTask-{this.resource}"; }
};

class TaskScheduler {
    List<Any<Task>*>* tasks;
    Map<String, int>* stats;

    TaskScheduler() {
        this.tasks = new List<Any<Task>*>();
        this.stats = new Map<String, int>();
    }

    void addTask(Any<Task>* task) {
        this.tasks->Add(task);
        this.stats[task->getName()] = 0;
    }

    void runAll() {
        for (auto task in this.tasks) {
            String name = task->getName();
            printf("Running %s...\n", name);
            task->execute();
            this.stats[name] = this.stats[name] + 1;
        }
    }

    void printStats() {
        for (auto name, count in this.stats) {
            printf("  %s: %d runs\n", name, count);
        }
    }
};

int main() {
    TaskScheduler* scheduler = new TaskScheduler();

    scheduler->addTask(any<Task>(new ComputeTask(1, 1000)));
    scheduler->addTask(any<Task>(new IOTask("file.txt")));
    scheduler->addTask(any<Task>(new ComputeTask(2, 500)));
    scheduler->addTask(any<Task>(new IOTask("network")));

    scheduler->runAll();
    scheduler->printStats();

    // Exception in task
    try {
        auto badTask = any<Task>((Task*)NULL);
        badTask->execute();
    } catch (NullException e) {
        printf("Caught null task: %s\n", e.msg);
    }

    return 0;
}
