struct Point { x: int }

fr main() {
    let xs = [10, 20, 30, 40, 50];
    let mid = xs[1:3];
    output(len(mid));
    output(mid[0]);
    output(mid[1]);

    output(len(xs[2:1000]));
    output(len(xs[-5:3]));
    output(len(xs[3:1]));

    let words = ["a", "b", "c", "d"];
    let two = words[1:3];
    output(two[0]);
    output(two[1]);

    let ps = [Point { x: 1 }, Point { x: 2 }, Point { x: 3 }];
    let ptail = ps[1:len(ps)];
    output(len(ptail));
    output(ptail[0].x);
}
