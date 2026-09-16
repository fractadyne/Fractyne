fr add(a: int, b: int) -> int {
    return a + b;
}

fr fib(n: int) -> int {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

fr main() {
    output(add(3, 4));
    output(fib(10));
}
