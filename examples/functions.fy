fn add(a: int, b: int) -> int {
    return a + b;
}

fn fib(n: int) -> int {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

fn main() {
    output(add(3, 4));
    output(fib(10));
}
