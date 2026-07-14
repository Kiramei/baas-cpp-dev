import core.picture as picture
from core.device.Control import Control as DeviceControl


class Child:
    def run(self, obj, factory, attribute_name):
        picture.co_detect(self, skip_first_screenshot=True)
        self.click(10, 20, wait_over=True)
        self.ocr.recognize(self, (0, 0, 1, 1))
        obj.member(1)
        super().run()
        factory().send("payload")
        getattr(self, attribute_name)()
        DeviceControl("adb", "serial")
