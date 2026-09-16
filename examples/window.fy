// Not part of `make test` -- it opens a real window, which isn't something
// a stdout diff can verify. Run it yourself:
//   fractyne run examples/window.fy
// Click the window's close button, or press any key handled below, to quit.

fr main() {
    let ok = window_open(400, 300, "Fractyne Window Demo");
    if (!ok) {
        output("failed to open a window");
        return;
    }

    let x = 50;
    let dx = 3;

    while (!window_should_close()) {
        window_poll_events();
        if (key_down("escape")) {
            break;
        }

        window_clear(20, 20, 40);

        gfx_set_color(255, 100, 100);
        gfx_rect(x, 110, 60, 60);

        gfx_set_color(100, 255, 100);
        gfx_circle(mouse_x(), mouse_y(), 20);

        gfx_set_color(100, 100, 255);
        gfx_line(0, 150, 400, 150);

        window_present();

        x += dx;
        if (x < 0 || x > 340) {
            dx = -dx;
        }

        delay_ms(16);
    }

    window_close();
}
