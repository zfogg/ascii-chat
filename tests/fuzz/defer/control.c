void f(int x) {
  while (x--) {
    defer(x++);
    if (x == 2)
      continue;
    if (x == 1)
      break;
  }
}
