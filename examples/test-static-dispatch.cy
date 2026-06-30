/* test-static-dispatch.cy — verify ClassName.method() static dispatch */

#include <stdio.h>
#include <string.h>

class Patient {
    int id;
    String name;

    Patient() { this.id = 0; this.name = ""; }

    static String tableName() { return "Patient"; }
    static int defaultId() { return 1; }
};

class Consent {
    int id;
    int patientId;

    Consent() { this.id = 0; this.patientId = 0; }

    static String tableName() { return "Consent"; }
    static int defaultId() { return 1; }
};

/* Generic class that uses T.staticMethod() */
class EntityOps<T> {
    static String getTable() {
        return T.tableName();
    }

    static int getDefaultId() {
        return T.defaultId();
    }
};

int main() {
    printf("=== Static Dispatch Test ===\n\n");

    /* Direct static dispatch: ClassName.method() */
    String pt = Patient.tableName();
    printf("Patient.tableName() = %s\n", pt);

    String ct = Consent.tableName();
    printf("Consent.tableName() = %s\n", ct);

    int pi = Patient.defaultId();
    printf("Patient.defaultId() = %d\n", pi);

    /* Generic static dispatch: T.tableName() inside EntityOps<T> */
    String gpt = EntityOps<Patient>.getTable();
    printf("EntityOps<Patient>.getTable() = %s\n", gpt);

    String gct = EntityOps<Consent>.getTable();
    printf("EntityOps<Consent>.getTable() = %s\n", gct);

    int gpi = EntityOps<Patient>.getDefaultId();
    printf("EntityOps<Patient>.getDefaultId() = %d\n", gpi);

    /* Verify they match */
    int passed = 0, failed = 0;
    #define CHECK(c, msg) if(c) { printf("PASS %s\n", msg); passed++; } else { printf("FAIL %s\n", msg); failed++; }

    CHECK(strcmp(pt, "Patient") == 0, "Patient.tableName() returns 'Patient'");
    CHECK(strcmp(ct, "Consent") == 0, "Consent.tableName() returns 'Consent'");
    CHECK(strcmp(gpt, "Patient") == 0, "EntityOps<Patient>.getTable() returns 'Patient'");
    CHECK(strcmp(gct, "Consent") == 0, "EntityOps<Consent>.getTable() returns 'Consent'");
    CHECK(pi == 1, "Patient.defaultId() returns 1");
    CHECK(gpi == 1, "EntityOps<Patient>.getDefaultId() returns 1");

    printf("\n=== %d passed, %d failed ===\n", passed, failed);
    return failed;
}
