fr main() {
    let ages = {"ana": 30, "bogdan": 25};
    output(ages["ana"]);
    output(len(ages));

    ages["carmen"] = 40;
    output(len(ages));

    ages["ana"] = 31;
    output(ages["ana"]);

    output(ages["missing"]);

    output(contains(ages, "ana"));
    output(contains(ages, "missing"));

    let ks = keys(ages);
    sort(ks);
    output(len(ks));
    let i = 0;
    while (i < len(ks)) {
        output(ks[i]);
        i = i + 1;
    }
}
