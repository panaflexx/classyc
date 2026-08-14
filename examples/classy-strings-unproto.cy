/* Unprototyped strlen/strcmp — Apple arm64 ABI regression.
 * Must pass with Phase 3 call-site protos (fixed actual args + vararg flag).
 * Do NOT add string.h here. */
int main() {
  String s = "hello";
  String a = "apple";
  String b = "apple";
  int n = (int)strlen(s);
  int n2 = (int)strlen("literal");
  int r = strcmp(a, b);
  printf("%d %d %d\n", n, n2, r);
  if (n != 5) return 1;
  if (n2 != 7) return 2;
  if (r != 0) return 3;
  return 0;
}
