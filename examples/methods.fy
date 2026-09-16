struct Point {
    x: int,
    y: int
}

fr move(p: Point, dx: int, dy: int) -> Point {
    return Point { x: p.x + dx, y: p.y + dy };
}

fr magnitude_sq(p: Point) -> int {
    return p.x * p.x + p.y * p.y;
}

fr main() {
    let p = Point { x: 1, y: 2 };
    let moved = p.move(3, 4);
    output(moved.x);
    output(moved.y);
    output(moved.magnitude_sq());

    let chained = p.move(1, 1).move(1, 1);
    output(chained.x);
    output(chained.y);

    let i = 0;
    i++;
    i++;
    output(i);
    i--;
    output(i);

    for (let j = 0; j < 5; j++) {
        output(j);
    }

    for (let k = 10; k > 7; k--) {
        output(k);
    }
}
