struct Point {
    x: int,
    y: int
}

struct Line {
    start: Point,
    end: Point
}

fn dist_sq(a: Point, b: Point) -> int {
    let dx = a.x - b.x;
    let dy = a.y - b.y;
    return dx * dx + dy * dy;
}

fn make_point(x: int, y: int) -> Point {
    return Point { x: x, y: y };
}

fn main() {
    let p1 = Point { x: 1, y: 2 };
    let p2 = Point { y: 5, x: 4 };
    output(dist_sq(p1, p2));

    let line = Line { start: p1, end: p2 };
    output(line.start.x);
    output(line.end.y);

    let p3 = make_point(10, 20);
    p3.x = 99;
    output(p3.x);
}
