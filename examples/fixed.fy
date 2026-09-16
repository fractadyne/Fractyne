fixed PI_TIMES_100 = 314;

struct Point { x: int, y: int }

fr main() {
    fixed greeting = "hello";
    output(greeting);
    output(PI_TIMES_100);

    fixed origin = Point { x: 0, y: 0 };
    output(origin.x);

    fixed xs = [1, 2, 3];
    output(len(xs));
    output(contains(xs, 2));

    let normal = 1;
    normal = 2;
    output(normal);
}
