fr main() {
    let n = 42;
    output("value is " + int_to_string(n));

    let pi = 3.5;
    output(float_to_string(pi));

    output(bool_to_string(true));

    let s = "123";
    output(string_to_int(s) + 1);

    output(int_to_float(3) + 0.5);
    output(float_to_int(9.9));
}
