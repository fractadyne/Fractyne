struct Point {
    x: int,
    y: int
}

fn dist_sq(a: Point, b: Point) -> int {
    let dx = a.x - b.x;
    let dy = a.y - b.y;
    return dx * dx + dy * dy;
}
