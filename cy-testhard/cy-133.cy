/* Test 133: Complex initialization order and static initialization */
#include <stdio.h>

class Logger {
    String name;
    static int instance_count;

    Logger(String n) {
        this.name = n;
        instance_count++;
        printf("Logger[%d] created: %s\n", instance_count, this.name);
    }

    ~Logger() {
        printf("Logger destroyed: %s\n", this.name);
        instance_count--;
    }

    void log(String msg) { printf("[%s] %s\n", this.name, msg); }

    static int getCount() { return instance_count; }
};

int Logger::instance_count = 0;

class Module {
    String name;
    Logger* logger;

    Module(String n) {
        this.name = n;
        this.logger = new Logger(f"Module-{n}");
        this.logger->log("initialized");
    }

    ~Module() {
        this.logger->log("shutting down");
        delete this.logger;
    }

    void doWork() { this.logger->log("working"); }
};

// Global static instances (constructed before main)
static Module* global_module = new Module("Global");

int main() {
    printf("Main started, global count: %d\n", Logger::getCount());

    Module* m1 = new Module("A");
    Module* m2 = new Module("B");

    m1->doWork();
    m2->doWork();

    printf("Before delete, count: %d\n", Logger::getCount());

    delete m1;
    printf("After m1 delete, count: %d\n", Logger::getCount());

    delete m2;
    printf("After m2 delete, count: %d\n", Logger::getCount());

    // Global deleted at program end
    return 0;
}
