fn sum(xs: list<int>) -> int {
    let total = 0;
    let i = 0;
    while (i < len(xs)) {
        total = total + xs[i];
        i = i + 1;
    }
    return total;
}

fn main() {
    let nums = [1, 2, 3, 4, 5];
    output(sum(nums));

    push(nums, 6);
    output(sum(nums));

    nums[0] = 100;
    output(nums[0]);

    let names = ["ana", "bogdan", "carmen"];
    output(names[1]);
    output(len(names));
}
