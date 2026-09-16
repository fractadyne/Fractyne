fr main() {
    // seeded, so this example's output is reproducible for `make test`
    random_seed(1);

    let i = 0;
    let all_in_range = true;
    while (i < 20) {
        let n = random_int(1, 6);
        if (n < 1 || n > 6) {
            all_in_range = false;
        }
        i += 1;
    }
    output(all_in_range);

    let f = random();
    output(f >= 0.0 && f < 1.0);

    random_seed(7);
    let a = random_int(1, 100);
    random_seed(7);
    let b = random_int(1, 100);
    output(a == b);
}
