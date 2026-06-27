/* Deep call-chain crash test — exercises 15+ class/method frames.
   @expect: crash   (intentionally dereferences null to demo the stack trace)
   Parliament → GovernmentAgency → Regulator → Country → Region →
   Customer → Bank → Account → Address → Validator → Logger → CRASH */
int printf(const char *fmt, ...);
void *malloc(long);

class Logger {
    int level;

    Logger(int level) { this.level = level; }

    int log_crash(int code) {
        /* Bottom of the chain: null-deref → SIGSEGV */
        int *bad = (int *)0;
        return *bad + code;
    }

    int log_message(int severity, int code) {
        if (severity > this.level)
            return this.log_crash(code);
        return 0;
    }
};

class Validator {
    Logger *logger;

    Validator(Logger *logger) { this.logger = logger; }

    int validate_range(int value, int lo, int hi) {
        if (value < lo || value > hi)
            return this.logger->log_message(5, value);
        return 1;
    }

    int validate_name(int name_len) {
        return this.validate_range(name_len, 1, 255);
    }
};

class Address {
    int zip;
    int street_len;
    Validator *v;

    Address(int zip, int street_len, Validator *v) {
        this.zip = zip;
        this.street_len = street_len;
        this.v = v;
    }

    int verify(int depth) {
        this.v->validate_range(this.zip, 10000, 99999);
        return this.v->validate_name(this.street_len);
    }
};

class Account {
    int id;
    int balance;
    Address *addr;

    Account(int id, int balance, Address *addr) {
        this.id = id;
        this.balance = balance;
        this.addr = addr;
    }

    int check_balance(int minimum) {
        if (this.balance < minimum) return -1;
        return this.addr->verify(1);
    }

    int withdraw(int amount) {
        return this.check_balance(amount);
    }
};

class Bank {
    Account *primary;
    Account *savings;

    Bank(Account *primary, Account *savings) {
        this.primary = primary;
        this.savings = savings;
    }

    int transfer(int amount) {
        int ok = this.primary->withdraw(amount);
        if (ok < 0) return ok;
        return this.savings->check_balance(0);
    }

    int audit() {
        return this.transfer(100);
    }
};

class Customer {
    int customer_id;
    Bank *bank;

    Customer(int id, Bank *bank) {
        this.customer_id = id;
        this.bank = bank;
    }

    int request_audit() {
        return this.bank->audit();
    }

    int monthly_review(int month) {
        printf("  reviewing month %d for customer %d...\n", month, this.customer_id);
        return this.request_audit();
    }
};

class Region {
    int n;

    Region(int n) { this.n = n; }

    int run_reviews(int month, Customer *c) {
        for (int i = 0; i < this.n; i++) {
            int r = c->monthly_review(month);
            if (r < 0) return r;
        }
        return 0;
    }
};

class Country {
    Region *north;
    Region *south;

    Country(Region *north, Region *south) {
        this.north = north;
        this.south = south;
    }

    int nationwide_audit(int quarter, Customer *c) {
        printf("  nationwide audit Q%d\n", quarter);
        int r = this.north->run_reviews(quarter * 3, c);
        if (r < 0) return r;
        return this.south->run_reviews(quarter * 3, c);
    }
};

class Regulator {
    Country *country;
    int strictness;

    Regulator(Country *c, int strictness) {
        this.country = c;
        this.strictness = strictness;
    }

    int enforce(int quarter, Customer *cust) {
        printf("  enforcing regulations (strictness=%d)\n", this.strictness);
        return this.country->nationwide_audit(quarter, cust);
    }

    int annual_review(Customer *cust) {
        for (int q = 1; q <= 4; q++) {
            int r = this.enforce(q, cust);
            if (r < 0) return r;
        }
        return 0;
    }
};

class GovernmentAgency {
    Regulator *reg;
    int agency_id;

    GovernmentAgency(int id, Regulator *reg) {
        this.agency_id = id;
        this.reg = reg;
    }

    int commence_investigation(Customer *cust) {
        printf("  agency %d commencing investigation\n", this.agency_id);
        return this.reg->annual_review(cust);
    }
};

class Parliament {
    GovernmentAgency *agency;

    Parliament(GovernmentAgency *agency) { this.agency = agency; }

    int order_investigation(int case_num, Customer *cust) {
        printf("  parliament orders investigation, case #%d\n", case_num);
        return this.agency->commence_investigation(cust);
    }
};

int run_simulation(int case_number) {
    Logger *log = new Logger(3);
    Validator *val = new Validator(log);

    /* addr2 has street_len=0 → triggers crash deep in Logger::log_crash */
    Address *addr1 = new Address(12345, 10, val);
    Address *addr2 = new Address(54321, 0, val);

    Account *checking = new Account(1001, 5000, addr1);
    Account *savings  = new Account(1002, 20000, addr2);

    Bank *bank = new Bank(checking, savings);
    Customer *cust = new Customer(42, bank);

    Region *north = new Region(2);
    Region *south = new Region(1);
    Country *country = new Country(north, south);

    Regulator *reg = new Regulator(country, 9);
    GovernmentAgency *agency = new GovernmentAgency(7, reg);
    Parliament *parliament = new Parliament(agency);

    return parliament->order_investigation(case_number, cust);
}

int main(int argc, char **argv) {
    printf("=== Deep call-chain crash test ===\n\n");
    int result = run_simulation(2025);
    printf("result = %d\n", result);
    return 0;
}
