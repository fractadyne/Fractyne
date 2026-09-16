fr main() {
    let ages = {"ana": 30, "bogdan": 25};
    output(ages["ana"]);
    output(len(ages));

    ages["carmen"] = 40;
    output(len(ages));

    ages["ana"] = 31;
    output(ages["ana"]);

    output(ages["missing"]);
}
