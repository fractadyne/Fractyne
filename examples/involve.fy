involve "involve_geometry.fy";

fn main() {
    let p1 = Point { x: 0, y: 0 };
    let p2 = Point { x: 3, y: 4 };
    output(dist_sq(p1, p2));
}
