import time


def implement(self):
    self.u2.app_stop("com.github.uiautomator")
    self.u2.uiautomator.start()
    self.wait_uiautomator_start()
    self.last_refresh_u2_time = time.time()
