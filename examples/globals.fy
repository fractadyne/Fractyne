let PI = 3.14159;
let GREETING = "hello from a global";

fr circle_area(r: float) -> float {
    return PI * r * r;
}

fr main() {
    output(GREETING);
    output(circle_area(2.0));
}
