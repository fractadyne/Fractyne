enum Color {
    Red,
    Green,
    Blue
}

struct Pixel {
    x: int,
    y: int,
    color: Color
}

fr describe(c: Color) -> string {
    if (c == Color.Red) {
        return "warm";
    } else if (c == Color.Blue) {
        return "cool";
    }
    return "neutral";
}

fr main() {
    let c = Color.Green;
    output(c);
    output(c == Color.Green);
    output(c == Color.Red);

    let p = Pixel { x: 1, y: 2, color: Color.Blue };
    output(p.color);
    output(describe(p.color));
    output(describe(Color.Red));
}
