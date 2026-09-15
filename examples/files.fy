fn main() {
    let path = "/tmp/fractyne_example_files.txt";

    write_file(path, "first line");
    output(read_file(path));

    append_file(path, " + appended");
    output(read_file(path));

    output(file_exists(path));
    output(file_exists("/tmp/fractyne_example_definitely_missing.txt"));
    output(read_file("/tmp/fractyne_example_definitely_missing.txt"));
}
