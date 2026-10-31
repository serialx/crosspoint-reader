from pathlib import Path
import unittest
import prepare_wifi_extra
from test_oracle import run
HERE=Path(__file__).resolve().parent
class WifiOracle(unittest.TestCase):
 def test_original_entry_keyboard_touch_and_scan(self):
  self.assertEqual(run(prepare_wifi_extra.source()),(HERE/'wifi_extra.golden').read_bytes())
 def test_duplicate_strength_mutation(self):
  source=prepare_wifi_extra.source()
  changed=source.replace('rssi > it->rssi','rssi >= it->rssi')
  self.assertNotEqual(source,changed)
  self.assertNotEqual(run(changed),(HERE/'wifi_extra.golden').read_bytes())
 def test_scan_ssid_bound_mutation(self):
  source=prepare_wifi_extra.source()
  changed=source.replace('char ssid[33]','char ssid[34]')
  self.assertNotEqual(source,changed)
  self.assertNotEqual(run(changed),(HERE/'wifi_extra.golden').read_bytes())
if __name__=='__main__':unittest.main()
