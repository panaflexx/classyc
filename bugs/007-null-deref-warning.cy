/* bugs/007-null-deref-warning.cy
 *
 * Demonstrates the new compile-time warning for unproven null dereferences
 * (DEREF_GUARD_DEFAULT sites).
 *
 * Compile with:
 *   ./bin/classyc -g -I include bugs/007-null-deref-warning.cy -c
 *
 * Expected output (with warnings enabled):
 *   warning: possible null dereference (ownership analysis could not prove the pointer non-null)
 */

int main() {
    int* p;          /* uninitialized – ownership cannot prove non-null */
    *p = 42;         /* triggers the new warning */
    return 0;
}
