def run_task_action(self, actions):
    for action in actions:
        operation = action["t"]
        if operation.startswith("click"):
            self.click(*action["p"])
        elif operation.startswith("exchange"):
            self.exchange()
        elif operation == "end-turn":
            self.end_turn()
        elif operation == "choose_and_change":
            self.choose_and_change(*action["p"])
