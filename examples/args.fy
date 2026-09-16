fr main() {
    let args = launch_args();
    output(len(args));
    let i = 0;
    while (i < len(args)) {
        output(args[i]);
        i = i + 1;
    }
}
