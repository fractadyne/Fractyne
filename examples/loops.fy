fr main() {
    let i = 0;
    while (i < 5) {
        output(i);
        i += 1;
    }

    for (let n = 0; n < 10; n += 1) {
        if (n == 5) {
            break;
        }
        if (n % 2 == 0) {
            continue;
        }
        output(n);
    }
}
