prefix = "sample"
path = "sample"
x_y_range = {
    "present": (0, 1, 2, 3),
    "missing": (4, 5, 6, 7),
    "placeholder": (),
}

# If a validator imported this fixture, the test would crash.
raise RuntimeError("mapping modules must not execute")
