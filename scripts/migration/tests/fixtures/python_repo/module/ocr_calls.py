def inspect(self):
    self.ocr.recognize_int(self, (0, 0, 1, 1))
    self.ocr.recognize_number(self.latest_img_array, (0, 0, 1, 1), int, self.ratio)
