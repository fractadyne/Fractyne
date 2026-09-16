fr main() {
    let a = 12;
    let b = 10;
    output(a & b);
    output(a | b);
    output(a ^ b);
    output(~a);
    output(a << 2);
    output(a >> 2);

    let mask = 1 << 3 | 1 << 1;
    output(mask);

    output(1 | 2 & 3);
    output((1 | 2) & 3);

    let xs = [1, 2, 3];
    let m = {"x": 1};
    output(len(xs) + len(m));

    let y = 12;
    y &= 10;
    output(y);
    y |= 5;
    output(y);
    y ^= 3;
    output(y);
    y <<= 2;
    output(y);
    y >>= 1;
    output(y);
}
