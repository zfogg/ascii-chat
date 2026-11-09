static int trace;
int f(int x) {
  defer(trace += x);
  if (x) {
    defer(trace++);
    return x;
  }
  return 0;
}
