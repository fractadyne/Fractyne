enum Color {
    Red,
    Green,
    Blue
}

fr describe(n: int) -> string {
    branch (n) {
        case 1, 2, 3 {
            return "small";
        }
        case 4, 5 {
            return "medium";
        }
        else {
            return "large";
        }
    }
}

fr main() {
    output(describe(2));
    output(describe(5));
    output(describe(99));

    let name = "bogdan";
    branch (name) {
        case "ana" {
            output("hi ana");
        }
        case "bogdan" {
            output("hi bogdan");
        }
    }

    let c = Color.Green;
    branch (c) {
        case Color.Red {
            output("warm");
        }
        case Color.Green, Color.Blue {
            output("cool-ish");
        }
    }

    branch (99) {
        case 1 {
            output("one");
        }
    }
    output("after no-match branch");
}
