struct Point {
    x: int,
    y: int
}

struct Polygon {
    vertices: list<Point>,
    name: string
}

fn sum_x(points: list<Point>) -> int {
    let total = 0;
    let i = 0;
    while (i < len(points)) {
        total += points[i].x;
        i += 1;
    }
    return total;
}

fn main() {
    let points = [Point { x: 1, y: 2 }, Point { x: 3, y: 4 }];
    output(sum_x(points));

    push(points, Point { x: 10, y: 20 });
    output(len(points));
    output(points[2].x);

    let tri = Polygon {
        vertices: [Point { x: 0, y: 0 }, Point { x: 1, y: 0 }, Point { x: 0, y: 1 }],
        name: "triangle"
    };
    output(tri.name);
    output(tri.vertices[2].y);
}
