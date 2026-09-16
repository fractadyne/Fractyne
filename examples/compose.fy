enum Color {
    Red,
    Green
}

fr main() {
    let name = "Ana";
    let age = 30;
    output(compose("Hello, {}! You are {} years old.", name, age));

    output(compose("pi is roughly {}", 3.14));
    output(compose("flag: {}", true));
    output(compose("color: {}", Color.Green));

    let n = 5;
    output(compose("{} squared is {}", n, n * n));
}
