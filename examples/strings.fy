fn greet(name: string) -> string {
    return "Hello, " + name + "!";
}

fn main() {
    output(greet("Fractyne"));
    let a = "foo";
    let b = "bar";
    output(a + b);

    let s = "Hello, Fractyne!";
    output(s[0]);
    output(substring(s, 7, 9));
    output(to_upper(s));
    output(to_lower(s));
    output(trim("   spaced out   "));
    output(index_of(s, "Fractyne"));
}
