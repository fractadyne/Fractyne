fr classify(n: int) -> string {
    if (n < 0) {
        return "negative";
    } else if (n == 0) {
        return "zero";
    } else {
        return "positive";
    }
}

fr sign(n: int) -> string {
    return n < 0 ? "negative" : (n == 0 ? "zero" : "positive");
}

fr main() {
    output(classify(-5));
    output(classify(0));
    output(classify(42));

    output(sign(-5));
    output(sign(0));
    output(sign(42));
}
