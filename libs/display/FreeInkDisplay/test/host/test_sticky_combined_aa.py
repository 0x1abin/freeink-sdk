"""Exercise the real combined driver with allocation failures and a recording bus."""
import unittest
from test_ssd1677 import HERE, run_trace


class CombinedAaTest(unittest.TestCase):
    def test_sticky(self):
        run_trace(HERE / "test_sticky_combined_aa.cpp",
                  defines=("FREEINK_SSD1677_TEXT_ROUTING=1", "FREEINK_DEVICE_STICKY=1"))

    def test_papermono(self):
        run_trace(HERE / "test_sticky_combined_aa.cpp",
                  defines=("FREEINK_SSD1677_TEXT_ROUTING=0",))

    def test_image_text_image_switch(self):
        run_trace(HERE / "test_sticky_image_route.cpp",
                  defines=("FREEINK_SSD1677_TEXT_ROUTING=1", "FREEINK_DEVICE_STICKY=1"),
                  sources=("driver/Ssd1677Driver.cpp", "driver/PaperMonoDriver.cpp"))


if __name__ == "__main__":
    unittest.main()
