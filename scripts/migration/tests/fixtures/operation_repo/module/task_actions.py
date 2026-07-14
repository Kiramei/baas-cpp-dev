def dispatch(self, operation):
    if operation.startswith("click"):
        self.click(1, 2)
    elif operation == "end-turn":
        self.click(3, 4)
