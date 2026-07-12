/* test-entity-ops.cy — Verify generic EntityOps works
 * @expect: skip   (needs examples/Data.cy sibling + multi-file build)
 */

#include "Data.cy"

int main() {
    printf("=== Generic EntityOps Test ===\n\n");

    owned Sqlite* db = Sqlite.open(":memory:");
    defer delete db;

    db->execute("CREATE TABLE Patient (Id INTEGER PRIMARY KEY, ExternalEptId TEXT, FirstName TEXT, LastName TEXT, BirthDate TEXT, EmailAddress TEXT, PhoneNumber TEXT, Sex TEXT, SymptomList TEXT, SymptomDescription TEXT)");
    db->execute("CREATE TABLE QuestionAnswer (Id INTEGER PRIMARY KEY, QuestionId INTEGER, Answer TEXT, AnswerDate TEXT, PatientId INTEGER)");

    /* Patient */
    owned Patient* p = new Patient();
    p->externalEptId = "T001";
    p->firstName = "Ada";
    p->lastName = "Lovelace";
    p->birthDate = "1815-12-10";
    p->emailAddress = "ada@analytical.engine";
    p->phoneNumber = "555-0001";
    p->sex = "F";

    p->save(db);
    printf("Patient saved with id=%d\n", p->getId());

    p->emailAddress = "ada@new.com";
    p->update(db);
    printf("Patient updated\n");

    /* QuestionAnswer */
    owned QuestionAnswer* qa = new QuestionAnswer();
    qa->questionId = 5;
    qa->answer = "YES";
    qa->answerDate = "2025-06-01";
    qa->patientId = p->getId();

    qa->save(db);
    printf("QuestionAnswer saved with id=%d\n", qa->getId());

    qa->answer = "ALREADYTAKING";
    qa->update(db);
    printf("QuestionAnswer updated\n");

    /* Cleanup */
    qa->delete(db);
    p->delete(db);

    printf("\nAll generic EntityOps tests passed.\n");
    return 0;
}
