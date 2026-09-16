fr main() {
    let xs = [10, 20, 30];
    for (let x in xs) {
        output(x);
    }

    let sum = 0;
    for (let x in xs) {
        if (x == 20) {
            continue;
        }
        sum = sum + x;
    }
    output(sum);

    for (let x in xs) {
        if (x == 20) {
            break;
        }
        output(x);
    }

    for (let ch in "abc") {
        output(ch);
    }

    for (let x in xs) {
        for (let y in xs) {
            if (x == y) {
                output(x);
            }
        }
    }

    let names = ["Ana", "Bogdan"];
    for (let name in names) {
        output(name);
    }
}
